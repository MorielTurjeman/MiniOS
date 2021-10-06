// Coral Rubilar 316392877
// Moriel Turjeman 308354968

#define _GNU_SOURCE

#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>
#include <signal.h>

#include "MiniOS.h"


#define MODE_SLEEP_PRIORITY 1
#define MODE_SLEEP_INDEX

int priorityArray[] = {0,1,2,3,4,5,6,7,8,9};

int sched_mode = MODE_SLEEP_PRIORITY;

typedef struct task
{
    int id;
    int priority;
    char name[19];
    int assigned_core;
    struct timeval tv;
    time_t suspensionTime;
    pthread_t linux_thread;

}task;



#define MAX_LEN 19
#define MAX_CORES 8

char buffer[MAX_LEN+1];

typedef struct {
    bool locked;
} Mutex;

typedef struct {
    int size;
    int count;

    Mutex m;
} Semaphore;


Mutex memMutex;

void MutexAcquire(Mutex* m, task* task) {
    while (!__sync_bool_compare_and_swap(&m->locked, 0, 1)) {
        usleep(200);
    }
    __sync_synchronize();
}

void MutexRelease(Mutex* m, task* task) {
    while (!__sync_bool_compare_and_swap(&m->locked, 1, 0)) {
        usleep(200);
    }
    __sync_synchronize();
}

//array to hold all the taks
task task_list[256];
//how many cores are active
int active_cores;
//how many tasks are active
int active_tasks;
//the schedule array, current_tasks[i] holds the id of the task currently running on core i
int current_tasks[MAX_CORES];


//setup the static variables, note that there are maximum 256 tasks, this is just a random number !
void initTasksStructure(int tasks, int cores) {
  active_tasks = tasks;
  active_cores = cores;

  for (int i = 0; i < MAX_CORES; i++) {
    current_tasks[i] = -1;
  }
}

task* getTask(int id){
  return &task_list[id];
}



char *taskGetName(int id)
{
    task *task = getTask(id);
    return task->name;
}

char* taskGetMem(int id) {
  MutexAcquire(&memMutex, getTask(id));
  return buffer;
}

void taskReleaseMem(int id) {
  MutexRelease(&memMutex, getTask(id));
}

void taskSetMem(int id, char* buffer) {
  task* task = getTask(id);
  memcpy(buffer, task->name, 19);
}

//perform the actual sleep, sleep until the core is supposed to execute task with task->id
void taskSleep(task* task) {
  current_tasks[task->assigned_core] = abs(task->id + active_cores) % active_tasks;

  do {
    usleep(1000); // we sleep in blocks of one second until we need to wake up;
  } while (task->id != current_tasks[task->assigned_core]);
}


//taskShouldSuspend will check the following:
//  if the core is scheudaled to execute the task with my id, (check the current_tasks array), if not return True
//  if the task last executed + suspenstion time > current time, than the task should still sleep and therefore is suspended.
bool taskShouldSuspend(int id){
  task *task = getTask(id);
  struct timeval tv = {0};
  gettimeofday(&tv, NULL);
  return current_tasks[task->assigned_core] != id || task->tv.tv_sec + task->suspensionTime > tv.tv_sec;
}

//taskWait will set the last time the thread executed
//it will store the amount of time it should be suspended (in seconds)
//then will call taskSleep to forfit the CPU core for another task
//the task does not actually sleep time seconds 'straight' but actually
//it will only sleep 1000 micro seconds, this is done only to forfit the cpu
//it will continue to sleep the entire time remaining in the taskSuspend function

void taskWait(int id, int time) {
  task* task = getTask(id);
  task->suspensionTime = time;
  gettimeofday(&task->tv, NULL);

  taskSleep(task);
}

//task suspend is similar to taskWait, but here the task is suspended until it should resume.
void taskSuspend(int id){
    task *task= getTask(id);
    while(taskShouldSuspend(id))
      taskSleep(task);
}

//the signal handler switches between schedulaing modes
void schedChangeHandler(int s) {
  printf("Changing sched mode");
  sched_mode = !sched_mode;
}

//this is the psuedo code from the assignment
void* taskMain(void* id_ptr) {
  int id = *(int*)id_ptr;
  task* task = getTask(id); //get the task from the array
  do {
    //see if the task should be schedualed, or suspended
    if (taskShouldSuspend(id)) {
      taskSuspend(id);
    }

    printf("Task %d attemps to take memory\n", task->id);
    char* ptr = taskGetMem(id);
    printf("Task %d attemps to took memory\n", task->id);
    taskSetMem(id, ptr);
    taskReleaseMem(id);

    //eiter set the sleep to 'priority' seconds, or set it to the 'id' seconds.
    if (sched_mode == MODE_SLEEP_PRIORITY)
      taskWait(id, 10 - task->priority);
    else
      taskWait(id, task->id + 1); //add one since id == 0 won't sleep

  } while(1);
}


void taskCreate(int id, char* name, int priority, int core) {
  //init task struct
  // set index, name and priority
  // set the core so we can easily switch between tasks on the same core
  task_list[id].id = id;
  memcpy(task_list[id].name, name, 19);
  task_list[id].priority = priority;
  task_list[id].assigned_core = core;

  // init tv to be now.
  gettimeofday(&task_list[id].tv, NULL);

  //lock task to the core it is supposed to run on
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);
  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setaffinity_np(&at, sizeof(cpuset), &cpuset);

  //if no task has been schedualed yet on the CPU, scheduale this task
  if (current_tasks[core] == -1)
    current_tasks[core] = id;

  // create the actual thread for the task, pass ptr to the id of the task as param
  pthread_create(&task_list[id].linux_thread, &at, taskMain, &task_list[id].id);
}



int main(int argc, char* argv[]) {
  if (argc <3)
	{
		printf("Error: Should send two arguments");
		return 0;
	}

	int tasks = atoi(argv[1]); //number of tasks
	int cores = atoi(argv[2]); //number of cores
  if(tasks<1 || tasks>9 || cores<1 || cores>3|| tasks%cores != 0){
       printf("Error: Invalid inpute\n");
       printf("tasks between 1-9, cores between 1-3 and number tasks should be a multiple of cores\n");
       return 0;
    }
  //set the static variables
  initTasksStructure(tasks, cores);

  //register the signal handler, this will make the function schedChangeHandler when the user issues a signal SIGUSR1 to our application
  if (signal(SIGUSR1, schedChangeHandler) == SIG_ERR)
    exit(-1);

  //srand(time(NULL));
  for (int i = 0; i < tasks; i++) {
    char out[20] = "";
    sprintf(out, "Task %d", i);
    //create the task, the values which will be stored are:
    taskCreate(i, out, priorityArray[i], i % cores);
  }

  //wait for all tasks to finish, this will never happen since the tasks have no end, so this will sleep forever
  for(int i = 0; i < tasks; i++) {
    pthread_join(task_list[i].linux_thread, NULL);
  }
}
