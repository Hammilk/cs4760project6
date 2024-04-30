#include<stdio.h>
#include<math.h>
#include<sys/shm.h>
#include<sys/ipc.h>
#include<unistd.h>
#include<stdlib.h>
#include<sys/types.h>
#include<sys/msg.h>

//Various Macros
#define SHMKEY1 2031535
#define SHMKEY2 2031536
#define BUFF_SZ sizeof(int)
#define PERMS 0644
#define reqChance 5 //defines the chance that the process makes a request
#define termChance 1 //defines the chance that a process terminates within a loop

//Randomizer Section

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
    if(!randomize_helper(fopen("/dev/urandom", "r")))
        return 0;
    if(!randomize_helper(fopen("/dev/arandom", "r")))
        return 0;
    if(!randomize_helper(fopen("/dev/random", "r")))
        return 0;
    return -1;
}

//Set up struct for message queue
typedef struct{
    long mtype;
    int resource; //+ for request, - for giving
    pid_t pid;
    int action;
} msgbuffer;

int main(int argc, char** argv){ //at some point, add bound parameter
    //Arg[1] bound

    //Set up message queue stuff
    msgbuffer buff;
    buff.mtype = 1;
    int msqid;
    buff.pid = 0;
    buff.action = 0;
    key_t key;

    if((key = ftok("oss.c", 1)) == -1){
        perror("ftok");
        exit(1);
    }
    if((msqid = msgget(key, PERMS)) == -1){
        perror("msgget in child");
        exit(1); 
    }

    //Set up shared memory pointers
    int shm_id = shmget(SHMKEY1, BUFF_SZ, IPC_CREAT | 0666);
    if(shm_id <= 0){
        fprintf(stderr, "Shared memory get failed.\n");
        exit(1);
    }
    int* sharedSeconds = shmat(shm_id, 0, 0);

    shm_id = shmget(SHMKEY2, BUFF_SZ, IPC_CREAT | 0666);
    if(shm_id <= 0){
        fprintf(stderr, "Shared memory get failed.\n");
        exit(1);
    }
    int* sharedNano = shmat(shm_id, 0, 0);

    //Parse out current SysClock and SysNano time
    int sysClockS = *sharedSeconds; //Starting seconds
    int sysClockNano = *sharedNano; //Starting nanoseconds
    
    //Seed random 
    if(randomize()){
        fprintf(stderr, "Warning: No sources for randomness.\n");
    }

    //Initialize bound
    int bound = rand() % (atoi(argv[1]) + 1);
    
    //Initalize request time
    int requestNano = sysClockNano + bound;
    int requestSecond = sysClockS;
    if(requestNano > pow(10, 9)){
        requestNano -= (pow(10, 9));
        requestSecond++;
    }

    //Set up array to track resources
    int resourceArray[10];
    for(int i = 0; i < 10; i++){
        resourceArray[i] = 0;
    }
    //Set up control for work loop
    int termFlag = 0;

    int resourceCount = 0;

    //Work section
    while(termFlag == 0){
        //check if time to request
        if(*sharedSeconds > requestSecond || (requestSecond == *sharedSeconds) && (*sharedNano > requestNano)){
            //Determine whether to request or release 
            int requestGenerate = rand() % 201;
            buff.pid = getpid();
            if(resourceCount == 0 || requestGenerate > reqChance){ //if requestGenerate is higher than reqchance, request
                buff.resource = rand() % 10;
                buff.action = 1;
                buff.mtype = getppid();
                while(resourceArray[buff.resource] > 20){
                    buff.mtype = getppid();
                    buff.resource = rand() % 10;
                    buff.action = 1; 
                }
                printf("Child process: %d requesting for Resource %d\n",getpid(), buff.resource);
            }
            else{ //Release section
                buff.resource = (rand() % 10);
                buff.action = -1;
                buff.mtype = getppid();
                while(resourceArray[buff.resource] == 0){ //Checks to make sure child doesn't request more processes then what exits
                    buff.mtype = getppid();
                    buff.resource = (rand() % 10);
                    buff.action = -1;
                }
                printf("Child releasing resource %d\n", buff.resource);
            }
            //Send request/release
            if(msgsnd(msqid, &buff, sizeof(buff) - sizeof(long), 0) == -1){
                perror("msgsnd to parent failed\n");
                exit(1);
            }
            
            //If sent request message, blocking recieved until child receives resource
            if(buff.action > 0){
                if(msgrcv(msqid, &buff, sizeof(buff) - sizeof(long), getpid(), 0) == -1){
                    perror("msgrcv to parent failed");
                    exit(1);
                }
                if(buff.action == -100){
                    printf("Child process %d is terminating early from deadlock detection algorithm\n", getpid());
                    break;
                }
                else{
                    //Increment resource in resource array to track
                    resourceArray[buff.resource]++;
                    resourceCount++;
                    //Reset bounds 
                    bound = rand() % (atoi(argv[1]) + 1);
                    requestNano = sysClockNano + bound;
                    requestSecond = requestSecond;
                    if(requestNano > pow(10, 9)){
                        requestNano -= (pow(10, 9));
                        requestSecond++;
                    }
                }
            }
            else{
                //Decrement to indicate releasing resource
                resourceArray[buff.resource]--;
                resourceCount--;
            }
        }

        //Determinte if process should terminate
        int terminateGenerate = rand() % 201;
        if(terminateGenerate < termChance){

            printf("Process %d is terminating\n", getpid());
            termFlag = 1;
            buff.action = -100;
            buff.resource = -100;
            buff.pid = getpid();
            buff.mtype = getppid();
            if(msgsnd(msqid, &buff, sizeof(buff) - sizeof(long), 0) == -1){
                perror("msgsnd to parent failed\n");
                exit(1);
            }
        }
    }
    if(msgctl(msqid, IPC_RMID, NULL) == -1){
        perror("msgctl");
        exit(1);
    }
    EXIT_SUCCESS;
}
