/*
 *Project Title: Project 5 - Resource Management
 *Author: David Pham
 * 3/7/2024
 */

#include<time.h>
#include<stdio.h>
#include<sys/types.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<math.h>
#include<signal.h>
#include<sys/time.h>
#include<getopt.h>
#include<string.h>
#include<sys/msg.h>
#include<stdarg.h>
#include<errno.h>

//Macros
#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define BUFF_SZ sizeof (int)
#define MAXDIGITS 3
#define PERMS 0644
#define BOUND 20

//Globals

struct page{
    int frameNumber;
    int occupied;
};

struct frame{
    int occupied;
    int pageNumber;
    pid_t pid;
    int secondChance;
    int dirtyBit;
    int nextFrame;
};
 
struct PCB{
    int occupied; //Either true or false
    pid_t pid; //process id of child
    int startSeconds; //time when it was forked
    int startNano; //time when it was forked
    int blockSeconds; //time until unblock
    int blockNano; //time until unblock
    struct page pageTable[64];
};

//Message struct
typedef struct {
    long mtype;
    int memoryRequest;
    pid_t pid;
} msgbuffer;

//Optarg struct
typedef struct{
    int proc;
    int simul;
    int interval;
    char logfile[20];
} options_t;

//Shared Memory pointers
int *sharedSeconds;
int *sharedNano;
int shmidSeconds;
int shmidNano;

//Process Table
struct PCB processTable[20];

//Message ID
int msqid;

struct QNode{
    int key;
    int request;
    struct QNode* next;
};

struct Queue{
    struct QNode *front, *rear;
};

//Function prototypes
void printFrameTable(int SysClockS, int SysClockNano, struct frame frameTable[256], int nextFrame);
void clearFrame(struct frame frameTable[256], pid_t pid);
int terminateCheck();
int req_lt_avail(const int *req, const int *avail, const int pnum, const int num_res);
int addProcessTable(struct PCB processTable[20], pid_t pid);
struct QNode* newNode(int k, int r);
struct Queue* createQueue();
void enQueue(struct Queue* q, int k, int r);
void deQueue(struct Queue* q);
int lfprintf(FILE *stream,const char *format, ... );
static int setupinterrupt(void);
static int setupitimer(void);
void print_usage(const char * app);
void printProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20]);
void fprintProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20], FILE *fptr);
void incrementClock(int *seconds, int *nano, int increment);
static int randomize_helper(FILE *in);
static int randomize(void);
void clearResources();
int clearProcessTable(struct PCB processTable[20], pid_t pid);
int fillPageTable(pid_t, int, int);
void fillFrameTable(pid_t, int, int, struct frame frameTable[256]);
int secondChance(struct frame FrameTable[256]);



int main(int argc, char* argv[]){

    
    //Seed random
    if(randomize()){
        fprintf(stderr, "Warning: No source for randomness.\n");
    }

    //Set up shared memory
    shmidSeconds = shmget(SHMKEY1, BUFF_SZ, 0666 | IPC_CREAT);
    if(shmidSeconds == -1){
        fprintf(stderr, "error in shmget 1.0\n");
        exit(1);
    }
    sharedSeconds = shmat(shmidSeconds, 0, 0);
    
    //Attach shared memory to nano
    shmidNano = shmget(SHMKEY2, BUFF_SZ, 0777 | IPC_CREAT);
    if(shmidNano == -1){
        fprintf(stderr, "error in shmget 2.0\n");
        exit(1);
    }
    sharedNano=shmat(shmidNano, 0, 0);

    //Set up structs defaults
    for(int i = 0; i < 20; i++){
        processTable[i].occupied = 0;
        processTable[i].pid = 0;
        processTable[i].startSeconds = 0;
        processTable[i].startNano = 0;
        processTable[i].blockSeconds = 0;
        processTable[i].blockNano = 0;
        for(int i2 = 0; i2 < 64; i2++){
            processTable[i].pageTable[i2].occupied = 0;
            processTable[i].pageTable[i2].frameNumber = 0;
        }
    }

    struct frame frameTable[256];
    for(int i = 0; i < 256; i++){
        frameTable[i].pageNumber = 0;
        frameTable[i].pid = 0;
        frameTable[i].occupied = 0;
        frameTable[i].dirtyBit = 0;
        frameTable[i].secondChance = 0;
        frameTable[i].nextFrame = 0;
    }

    //Set up user parameters defaults
    options_t options;
    options.proc = 80; //n
    options.simul = 18; //s
    options.interval = 0; //i
    strcpy(options.logfile, "msgq.txt"); //f

    //Set up user input
    const char optstr[] = "hn:s:i:f:";
    char opt;
    while((opt = getopt(argc, argv, optstr))!= -1){
        switch(opt){
            case 'h':
                print_usage(argv[0]);
                return(EXIT_SUCCESS);
            case 'n':
                options.proc = atoi(optarg);
                break;
            case 's':
                options.simul = atoi(optarg);
                break;
            case 'i':
                options.interval = atoi(optarg);
                break;
            case 'f':
                strcpy(options.logfile, optarg);
                break;
            default:
                printf("Invalid options %c\n", optopt);
                print_usage(argv[0]);
                return(EXIT_FAILURE);
        }
    }
   
    //Set up variables;
    int seconds = 0;
    int nano = 0;
    *sharedSeconds = seconds;
    *sharedNano = nano;

    //Variables for message queue
    key_t key;
    msgbuffer buff;
    buff.mtype = 0;
    buff.pid = 0;
    buff.memoryRequest = 0;

    //Set up timers
    if(setupinterrupt() == -1){
        perror("Failed to set up handler for SIGPROF");
        return 1;
    }
    if(setupitimer() == -1){
        perror("Failed to set up the ITIMER_PROF interval timer");
        return 1;
    }

    //Set up file
    char commandString[20];
    strcpy(commandString, "touch "); 
    strcat(commandString, options.logfile);
    system(commandString);
    FILE *fptr;
    fptr = fopen(options.logfile, "w");
    if(fptr == NULL){
        fprintf(stderr, "Error: file has not opened.\n");
        exit(0);
    }

    //get a key for message queue
    if((key = ftok("oss.c", 1)) == -1){
        perror("ftok");
        exit(1);
    }

    //create our message queue
    if((msqid = msgget(key, PERMS | IPC_CREAT)) == -1){
        perror("msgget in parent");
        exit(1);
    }
    
    //Variables
    int simulCount = 0;
    int childrenFinishedCount = 0;
    int nextIntervalSecond = 0;
    int nextIntervalNano = 0;
    int childLaunchedCount = 0;
    pid_t terminatedChild = 0;
    int status = 0; //Where status of terminated pid will be stored
    struct frame *nextFrame;
    nextFrame = &frameTable[0];
    int frameInterval = 0;
    int secondChanceIndex = 0;

    struct Queue *blockQueue = createQueue();

    //Stats
    //Section Work
    while(childrenFinishedCount < options.proc){

        int terminatedChild = 0;
        if(simulCount > 0 && (terminatedChild = terminateCheck()) < 0){
            perror("Wait for PID failed\n");
        }
        else if(terminatedChild > 0){
            fprintf(fptr, "OSS: P%d has terminated at time %d:%d\n", terminatedChild, *sharedSeconds, *sharedNano);
            printf("OSS: P%d has terminated at time %d:%d\n", terminatedChild, *sharedSeconds, *sharedNano);
            simulCount--;
            childrenFinishedCount++;
            clearProcessTable(processTable, terminatedChild);
            clearFrame(frameTable, terminatedChild);
        }

        pid_t child = 0;
        //launch child
        
        if(childLaunchedCount < options.proc && 
            simulCount < options.simul &&
            simulCount < 18 &&
            (*sharedSeconds > nextIntervalSecond || *sharedSeconds == nextIntervalNano && 
            *sharedNano > nextIntervalSecond) &&
            (child = fork()) == 0){
                        
            //Child Launch Section

            char * args[] = {"./user_proc"};
            
            //Run Executable
            execlp(args[0], args[0], NULL);
            printf("Exec failed\n");
            exit(1);
        }
        if(child > 0){
            if(addProcessTable(processTable, child) == -1){
                perror("Add process table failed");
                exit(1);
            }
            printf("OSS: Process %d has launched at %d seconds and %d nano\n", child, *sharedSeconds, *sharedNano);
            fprintf(fptr, "Process %d has launched at %d seconds and %d nano\n", child, *sharedSeconds, *sharedNano);
            simulCount++;
            childLaunchedCount++;
            nextIntervalSecond += 1;
        }

        //Check Queues
        //Changed if to while
        while(blockQueue->front != NULL &&
            (((*sharedSeconds) > processTable[blockQueue->front->key].blockSeconds) ||
            ((*sharedSeconds) == processTable[blockQueue->front->key].blockSeconds &&
            (*sharedNano) > processTable[blockQueue->front->key].blockNano))
        ){
            int queuedRequest = blockQueue->front->request;
            int queuedProcessIndex = blockQueue->front->key;
            pid_t queuedProcess = processTable[queuedProcessIndex].pid;

            if(processTable[queuedProcessIndex].occupied == 0){
                deQueue(blockQueue);
            }
            else{
                int next = -1;
                while(next < 0){
                    if((*(nextFrame + secondChanceIndex)).secondChance == 1){
                        nextFrame->secondChance = 0;
                        secondChanceIndex++;
                        if(secondChanceIndex > 255){
                            secondChanceIndex = 0;
                        }
                    }
                    else{
                        if(nextFrame->occupied == 1){
                            printf("oss: Clearing frame %d and swapping in P%d page %d\n", secondChanceIndex, queuedProcess, abs(queuedRequest/1024));
                        }
                        nextFrame->secondChance = 1;
                        secondChanceIndex++;
                        if(secondChanceIndex > 255){
                            secondChanceIndex = 0;
                        }
                        fillPageTable(queuedProcess, queuedRequest, secondChanceIndex-1);
                        fillFrameTable(queuedProcess, queuedRequest, secondChanceIndex-1, frameTable);
                        next = secondChanceIndex;
                    }
                } 

                //Send message to release child process
                if(queuedRequest > 0){
                    printf("oss: Address %d in page %d in frame %d, giving data to P%d at time %d:%d\n", 
                        queuedRequest, abs(queuedRequest)/1024, secondChanceIndex-1, queuedProcess, *sharedSeconds, *sharedNano);
                }
                else{
                    printf("oss: Address %d in page %d in frame %d, writing data to frame at time %d:%d\n", 
                        queuedRequest, abs(queuedRequest)/1024, secondChanceIndex-1, *sharedSeconds, *sharedNano);
                    printf("OSS: Dirty bit of frame %d set, adding additional time to clock\n", secondChanceIndex);
                }
                buff.pid = getpid();
                buff.mtype = queuedProcess;
                buff.memoryRequest = 0;
                if(msgsnd(msqid, &buff, sizeof(buff)-sizeof(long), 0) == -1){
                    perror("queued message send failed\n");
                    exit(1);
                }
                printf("oss: Indicated to P%d that write has happened to address %d\n", queuedProcess, abs(queuedRequest));
                //Clear blocked info
                deQueue(blockQueue);
                processTable[queuedProcessIndex].blockSeconds = 0;
                processTable[queuedProcessIndex].blockNano = 0;

            }
        }

        //check messages and make memory request
        if(simulCount > 0){
            if(msgrcv(msqid, &buff, sizeof(buff) - sizeof(long), getpid(), IPC_NOWAIT)==-1){
                if(errno == ENOMSG){
                    incrementClock(sharedSeconds, sharedNano, 1000);
                }
                else{
                    printf("MSQID: %li\n", buff.mtype);
                    printf("Parent: %d Child: %d\n", getpid(), buff.pid);
                    perror("Msgrcv in parent error\n");
                    exit(1);
                }
            }
            else{ //By design, if you're in this block, a message has been received
                int address = abs(buff.memoryRequest);                
                
                printf("oss: P%d requesting ", buff.pid);
                if(buff.memoryRequest > 0){
                    printf("read");
                }
                else{
                    printf("write");
                }
                printf(" of address %d at time %d:%d\n", abs(buff.memoryRequest), *sharedSeconds, *sharedNano);
                

                //Creates a process number index for future use 
                int processNumber = -1;
                for(int i = 0; i < 20; i++){
                    if(processTable[i].pid == buff.pid){
                        processNumber = i;
                    }
                }

                //Checks for no page fault
                if(processTable[processNumber].pageTable[address/1024].occupied == 1){
                    int frameReference = abs(processTable[processNumber].pageTable[address/1024].frameNumber);
                    frameTable[frameReference].secondChance = 1;
                    //I don't actually know if the following is needed
                    if(buff.memoryRequest < 0){
                        frameTable[frameReference].dirtyBit = 1;
                    }
                    incrementClock(sharedSeconds, sharedNano, 100);
                    printf("oss: Address %d in frame %d, giving data to P%d at time %d:%d\n", 
                        abs(buff.memoryRequest), frameReference, buff.pid, *sharedSeconds, *sharedNano);

                    buff.mtype = buff.pid;
                    buff.pid = getpid();
                    buff.memoryRequest = 0;
                    if(msgsnd(msqid, &buff, sizeof(buff) - sizeof(long), 0) == -1){
                        perror("Msgsnd failed\n");
                        exit(1);
                    }
                }
                else{
                    printf("oss: Address %d is not in a frame, pagefault\n", buff.memoryRequest);
                    enQueue(blockQueue, processNumber, buff.memoryRequest);
                    processTable[processNumber].blockSeconds = *sharedSeconds;
                    processTable[processNumber].blockNano = (*sharedNano) + 14 * pow(10, 6);
                    if(processTable[processNumber].blockNano > pow(10, 9)){
                        processTable[processNumber].blockNano -= pow(10, 9);
                        processTable[processNumber].blockSeconds++;
                    }
                }
            }
        }
        //printf("TestLoop %d\n", childrenFinishedCount);
        if((*sharedSeconds) > frameInterval){
            frameInterval++;
            printFrameTable(*sharedSeconds, *sharedNano, frameTable, secondChanceIndex);
        }
        incrementClock(sharedSeconds, sharedNano, 5000); 
    }

    printf("Out of loop\n");
    //Remove message queues 
    if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl to get rid of queue in parent failed");
        exit(1);
    }
    
    //Remove shared memory
    shmdt(sharedSeconds);
    shmdt(sharedNano);

    //Close file
    fclose(fptr);
    return 0;

}


//Section function

void clearFrame(struct frame frameTable[256], pid_t pid){
    for(int i = 0; i < 256; i++){
        if(frameTable[i].pid == pid){
            frameTable[i].pid = 0;
            frameTable[i].secondChance = 0;
            frameTable[i].nextFrame = 0;
            frameTable[i].dirtyBit = 0;
            frameTable[i].occupied = 0;
            frameTable[i].pageNumber = 0;
        }
    }
}


int secondChance(struct frame frameTable[256]){
    int nextFrame = -1;
    int i = 0;
    while(nextFrame < 0){
        if(frameTable[i].secondChance == 1){
            frameTable[i].secondChance = 0;
        }
        else{
            frameTable[i].nextFrame = 1;
            nextFrame = i;
            break;
        }
        i = (i + 1) % 256;
    }
    return i;
}



int fillPageTable(pid_t pid, int request, int frame){
    int pageNumber = abs(request) / 1024;
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            processTable[i].pageTable[pageNumber].frameNumber = frame;
            processTable[i].pageTable[pageNumber].occupied = 1;
        }
    } 
    return 0;
}

void fillFrameTable(pid_t pid, int request, int frame, struct frame frameTable[256]){
    int pageNumber = abs(request) / 1024;
    frameTable[frame].pid = pid;
    frameTable[frame].pageNumber = pageNumber;
    frameTable[frame].occupied = 1;
    frameTable[frame].secondChance = 1;
    if(request < 0){
        frameTable[frame].dirtyBit = 1;
    }
    else{
        frameTable[frame].dirtyBit = 0;
    }
}

int terminateCheck(){
    int status = 0;
    pid_t terminatedChild = waitpid(0, &status, WNOHANG);
    if(terminatedChild > 0){
        clearProcessTable(processTable, terminatedChild);
        return terminatedChild;
    }
    else if(terminatedChild == 0){
        return 0; 
    }
    else{
        return -1;    
    }


}

// print no more than 10k lines to a file
int lfprintf(FILE *stream,const char *format, ... ) {
    static int lineCount = 0;
    lineCount++;
    if (lineCount > 10000)
        return 1;
    va_list args;
    va_start(args, format);
    vfprintf(stream,format, args);
    va_end(args);
    return 0;
}

static void myhandler(int s){
    printf("Got signal, terminated\n");
    for(int i = 0; i < 20; i++){
        if(processTable[i].occupied == 1){
            kill(processTable[i].pid, SIGTERM);
        }
    }
    if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl to get rid of queue in parent failed");
        exit(1);
    }
    shmdt(sharedSeconds);
    shmdt(sharedNano);
    shmctl(shmidSeconds, IPC_RMID, NULL); 
    shmctl(shmidNano, IPC_RMID, NULL);
    exit(1);
}

static int setupinterrupt(void){
    struct sigaction act;
    act.sa_handler = myhandler;
    act.sa_flags = 0;
    return(sigemptyset(&act.sa_mask) || sigaction(SIGINT, &act, NULL) || sigaction(SIGPROF, &act, NULL));
}

static int setupitimer(void){
    struct itimerval value;
    value.it_interval.tv_sec = 60;
    value.it_interval.tv_usec = 0;
    value.it_value = value.it_interval;
    return (setitimer(ITIMER_PROF, &value, NULL));
}

void print_usage(const char * app){
    fprintf(stderr, "usage: %s [-h] [-n proc] [-s simul] [-t timeLimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile]\n", app);
    fprintf(stderr, "   proc is the total amount of children.\n");
    fprintf(stderr, "   simul is how many children can run simultaneously.\n");
    fprintf(stderr, "   timeLimitForChildren is the bound of time that a child process should be launched for.\n");
    fprintf(stderr, "   intervalInMsToLaunchChildren specifies how often you should launch a child.\n");
    fprintf(stderr, "   logfile is the input for the name of the logfile for oss to write into.\n");
}

void printFrameTable(int SysClockS, int SysClockNano, struct frame frameTable[256], int nextFrame){
    printf("Current memory layout at time %d:%d is: \n", SysClockS, SysClockNano);
    printf("         Occupied   DirtyBit   SecondChance   NextFrame\n");
    for(int i = 0; i < 256; i++){
        printf("Frame %d", i);
        printf(" ");
        if(frameTable[i].occupied == 1){
            printf("Yes      ");
        }
        else{
            printf("No       ");
        }
        printf("%d          ", frameTable[i].dirtyBit);
        printf("%d               ", frameTable[i].secondChance);
        if(i == nextFrame){
            printf("*\n");
        }
        else{
            printf("\n");
        }
    }
}


void printProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20]){
    printf("OSS PID %d SysClockS: %d SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    printf("Process Table:\n");
    printf("Entry     Occupied  PID       StartS    Startn     Blocked\n"); 
    for(int i = 0; i<20; i++){
        if((processTable[i].occupied) == 1){
            printf("%d         %d         %d         %d         %d\n", i, processTable[i].occupied,
                   processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
        }
    } 
}

void fprintProcessTable(int PID, int SysClockS, int SysClockNano, struct PCB processTable[20], FILE *fptr){
    lfprintf(fptr, "OSS PID %d SysClockS: %d SysClockNano: %d\n", PID, SysClockS, SysClockNano);
    lfprintf(fptr, "Process Table:\n");
    lfprintf(fptr, "Entry     Occupied  PID       StartS    Startn      Blocked\n"); 
    for(int i = 0; i<20; i++){
        if((processTable[i].occupied) == 1){
            lfprintf(fptr, "%d         %d         %d         %d\n", i, processTable[i].occupied, 
                     processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
        }
    } 
}

void printResourceTable(int allocatedTable[20][10]){
    printf("  ");
    for(int i = 0; i < 10; i++){
        printf("R%d  ", i);
    }
    printf("\n");
    for(int i = 0; i < 20; i++){
        printf("P%d  ", i);
        for(int j = 0; j < 10; j++){
            printf("%d  ", allocatedTable[i][j]);
        }
        printf("\n");
    }
}

void incrementClock(int *seconds, int *nano, int increment){
    (*nano) += increment;
    if((*nano) >= (pow(10, 9))){
         (*nano) -= (pow(10, 9));
         (*seconds)++;
    }
}

static int randomize_helper(FILE *in){
    unsigned int seed;
    if(!in) return -1;
    if(fread(&seed, sizeof seed, 1, in) == 1){
        fclose(in);
        srand(seed);
        return 0;
    }
    fclose(in);
    return -1;
}

static int randomize(void){
    if(!randomize_helper(fopen("/dev/urandom", "r"))) return 0;
    if(!randomize_helper(fopen("/dev/arandom", "r"))) return 0;
    if(!randomize_helper(fopen("/dev/random", "r"))) return 0;
    return -1;
}

int clearProcessTable(struct PCB processTable[20], pid_t pid){
    for(int i = 0; i < 20; i++){
        if(processTable[i].pid == pid){
            processTable[i].occupied = 0;
            processTable[i].pid = 0;
            processTable[i].startSeconds = 0;
            processTable[i].startNano = 0;
            processTable[i].blockSeconds = 0;
            processTable[i].blockNano = 0;
            for(int i2 = 0; i2 < 64; i2++){
                processTable[i].pageTable[i2].occupied = 0;
                processTable[i].pageTable[i2].frameNumber = 0;
            }
            return 0; 
        }
    }
    return -1;
}

int addProcessTable(struct PCB processTable[20], pid_t pid){
    for(int i = 0; i < 20; i++){
        if(!processTable[i].occupied){
            processTable[i].occupied = 1;
            processTable[i].pid = pid;
            processTable[i].startNano = *sharedNano;
            processTable[i].startSeconds = *sharedSeconds;
            return 0;
        }
    }
    return -1;
}

//Create new node function
struct QNode* newNode(int k, int r){
    struct QNode* temp = (struct QNode*)malloc(sizeof(struct QNode));
    temp->key = k;
    temp->request = r;
    temp->next = NULL;
    return temp;
}

//Create Queue function
struct Queue* createQueue(){
    struct Queue* q = (struct Queue*)malloc(sizeof(struct Queue));
    q->front = q->rear = NULL;
    return q;
}

//The function to add a key k to q
void enQueue(struct Queue* q, int k, int r){
    struct QNode* temp = newNode(k, r);
    if(q->rear == NULL){
        q->front = q->rear = temp;
        return;
    }

    q->rear->next = temp;
    q->rear = temp;
}

//Function to remove a key from given queue q
void deQueue(struct Queue* q)
{
    if(q->front == NULL){
        return;
    }

    struct QNode* temp = q->front;

    q->front = q->front->next;

    if(q->front == NULL){
        q->rear = NULL;
    }
    free(temp);
}








