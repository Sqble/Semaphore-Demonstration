#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define BUFFER_SIZE 10
#define PRODUCE_COUNT 20

// type of shared memory
typedef int item_t;

typedef struct {
    item_t buffer[BUFFER_SIZE];
    int in, out; //indexes for in and out queue
    bool done;
    sem_t empty; // available slots count for producer to fill
    sem_t full; // filled slots count for consumer to read from 
    sem_t mutex; // ensure for mutual exclusion
} shared_data_t;

static void die(const char *msg) { perror(msg); exit(EXIT_FAILURE); }

int main(void) {
    srand(time(NULL));

    //shared memory
    shared_data_t *shm = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shm == MAP_FAILED) {
        die("mmap");
    }

    shm->in = 0;
    shm->out = 0;
    shm->done = false;
    sem_init(&shm->empty, 1, BUFFER_SIZE); // (semaphore, if !=0 shared between processes, init_value)
    sem_init(&shm->full, 1, 0);
    sem_init(&shm->mutex, 1, 1);

    pid_t pid = fork();

    if (pid < 0) {
        die("fork"); // no new process was created
    } else if (pid == 0) {
        //consumer (child)
        printf("[Consumer %d] started\n", getpid());
        bool running = true;
        while (running) {
            sem_wait(&shm->full); // wait for filled space to be available
            sem_wait(&shm->mutex); // wait for mutual exclusion

            // if done producing and done consuming
            if (shm->done && shm->in == shm->out) { 
                sem_post(&shm->mutex);
                //wake any other consumers that would be waiting forever
                sem_post(&shm->full); 
                break;
            }

            int item = shm->buffer[shm->out];
            shm->out = (shm->out + 1) % BUFFER_SIZE;

            sem_post(&shm->mutex);
            sem_post(&shm->empty);

            printf("[Consumer %d] consumed: %d\n", getpid(), item);
            fflush(stdout);
            usleep(150000); // make consumer wait longer so producer finishes first
        }
        
        printf("[Consumer %d] exiting\n", getpid());
        _exit(0);

    } else {
        //producer (parent)
        printf("[Producer %d] started\n", getpid());

        for (int i = 0; i < PRODUCE_COUNT; ++i) {
            sem_wait(&shm->empty); // wait for empty space to be available
            sem_wait(&shm->mutex); // wait for this thread's turn

            // critical section
            shm->buffer[shm->in] = i;
            shm->in = (shm->in + 1) % BUFFER_SIZE; // current index in buffer

            // exited critical section, send signal
            sem_post(&shm->mutex);
            sem_post(&shm->full); // add to filled count

            printf("[Producer %d] produced: %d\n", getpid(), i);
            fflush(stdout);
            usleep(100000);
        }

        sem_wait(&shm->mutex); //wait for mutex
        
        // critical section
        shm->done = true;

        //exit critical section
        sem_post(&shm->mutex);
        // avoid deadlock by waking consumer
        sem_post(&shm->full);

        printf("[Producer %d] finished producing\n", getpid());

        wait(NULL); //parent will wait for child process to finish 
        printf("Cleanup\n");

        sem_destroy(&shm->empty);
        sem_destroy(&shm->full);
        sem_destroy(&shm->mutex);
        munmap(shm, sizeof(*shm));
    }
}