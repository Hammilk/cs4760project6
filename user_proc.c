#include<stdio.h>
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
#define termChance 5 //defines the chance that a process terminates within a loop

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
    int memoryRequest; 
    pid_t pid;
} msgbuffer;

int main(int argc, char** argv){ //at some point, add bound parameter
    //Arg[1] bound

    //Set up message queue stuff
    msgbuffer buff;
    buff.mtype = 1;
    int msqid;
    buff.pid = 0;
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

    int exitFlag = 0;
    int requestCount = 0;
    
    //Work section
    while(exitFlag == 0){
        
        int pageNumber = rand() % 64;
        int offset = rand() % 1024;
        int memoryLocation = (pageNumber * 1024) + offset;
        int readWrite = rand() % 100;
        //Determine Read/Write Status
        if(readWrite < 10){
            memoryLocation = memoryLocation * -1;
        }

        buff.mtype = getppid();
        buff.memoryRequest = memoryLocation;
        buff.pid = getpid();
        if(msgsnd(msqid, &buff, sizeof(buff) - sizeof(long), 0) == -1){
            perror("Message Sent failure in child\n");
            exit(1);
        }
        /*
        printf("Child Sent Message pid: %d\n", buff.pid);
        printf("Child Sent Message Memory: %d\n", buff.memoryRequest);
        printf("Child Sent Message mtype: %li\n", buff.mtype);
        */
        //check if time to request
        if(msgrcv(msqid, &buff, sizeof(buff)- sizeof(long), getpid(), 0) == -1){
            perror("Message Received failure in child\n");
            exit(1);
        }
        /*
        else{
            printf("Child Received Message pid: %d\n", buff.pid);
            printf("Child Received Message Memory: %d\n", buff.memoryRequest);
            printf("Child Received Message mtype: %li\n", buff.mtype);
        }
        */
        requestCount++;
        if(requestCount % 1000 == 0){
            int terminate = rand() % 100;
            if(terminate < termChance){
                exitFlag = 1;
            }
        }
        
    }
    
    EXIT_SUCCESS;
}
