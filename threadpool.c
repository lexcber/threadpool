#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>


const int NUM;
typedef struct Task { 
	void (*func)(void* arg);
	void* arg;
}Task;

typedef struct ThreadPool {
	Task* task;
	int queueCapacity;
	int queueSize;
	int queueFront;
	int queueRear;
	
	pthread_t managerID;
	pthread_t* workerIDs;
	
	int maxThNum;// 最大线程数
	int minThNum;//最小线程数
	int busyThNum;// 忙碌线程数
	int aliveThNum;// 存活线程数
	int exitThNum;//
	
	pthread_mutex_t busyMetux;
	pthread_mutex_t poolMetux;

	pthread_cond_t notFull;
	pthread_cond_t notEmpty;
	
	int shutdown;

}ThreadPool;

void* worker(void* arg) {
	ThreadPool* pool = (ThreadPool*)arg;
	while (1) {
		pthread_mutex_lock(&pool->poolMetux);
		while (pool->queueSize == 0 && pool->shutdown == 0) {
			pthread_cond_wait(&pool->notEmpty, &pool->poolMetux);
			if (pool->exitThNum > 0) {
				pool->exitThNum--;
				if (pool->aliveThNum > pool->minThNum) {
					pool->aliveThNum--;
					pthread_mutex_unlock(&pool->poolMetux);
					threadExit(pool);
				}
			}
		}
		if (pool->shutdown == 1) {
			pthread_mutex_unlock(&pool->poolMetux);
			threadExit(NULL);
		}
		Task  task;
		task.func = pool->task[pool->queueFront].func;
		task.arg = pool->task[pool->queueFront].arg;
		pool->queueFront = (pool->queueFront + 1) % (pool->queueSize);
		pool->queueSize--;
		pthread_cond_signal(&pool->notFull);
		pthread_mutex_unlock(&pool->poolMetux);
		printf("thread %ld start working...\n", pthread_self());
		pthread_mutex_lock(&pool->busyMetux);
		pool->busyThNum++;
		pthread_mutex_unlock(&pool->busyMetux);

		task.func(task.arg);
		printf("thread %ld end working\n",pthread_self());

		pthread_mutex_lock(&pool->busyMetux);
		pool->busyThNum--;
		pthread_mutex_unlock(&pool->busyMetux);

		//free(task.arg);
		task.arg = NULL;
	}
	return NULL;
}


void threadExit(ThreadPool* pool) {
	pthread_t tid = pthread_self();
	int i;
	for (i = 0; i < pool->maxThNum; ++i) {
		if (tid == pool->workerIDs[i]) {
			pool->workerIDs[i] = 0;
			printf("thread_exit %ld \n", tid);
			break;
		}
	}
	pthread_exit(NULL);
	
	
}

void* mannger(void * arg) {
	ThreadPool* pool = (ThreadPool*)arg;
	while (pool->shutdown == 1){
		sleep(3);
		pthread_mutex_lock(&pool->poolMetux);
		int queueSize = pool->queueSize;
		int aliveThNum = pool->aliveThNum;
		pthread_mutex_unlock(&pool->poolMetux);
		
		pthread_mutex_lock(&pool->busyThNum);
		int busyThNum = pool->busyThNum;
		pthread_mutex_unlock(&pool->busyThNum);
		//add thread

		if (queueSize > aliveThNum && aliveThNum < pool->maxThNum) {
			pthread_mutex_lock(&pool->poolMetux);
			int counter = 0;
			for (int i = 0; (i < pool->maxThNum) && (counter < NUM) && (pool->aliveThNum < pool->maxThNum); ++i) {
				if (pool->workerIDs[i] == 0) {
					pthread_create(&pool->workerIDs[i], NULL, worker, pool);
				}
			
			}
			pthread_mutex_unlock(&pool->poolMetux);
		}

//delete thread
		if ((busyThNum * 2 < aliveThNum) && (aliveThNum > pool->minThNum)) {
			pthread_mutex_lock(&pool->poolMetux);
			pool->exitThNum --;
			for (int i = 0; i < pool->exitThNum; i++) {

				pthread_cond_signal(&pool->notEmpty);
			}
		}
		pthread_mutex_unlock(&pool->poolMetux);
		
	}
	return NULL;
}


ThreadPool* ThreadpoolCreate(int queueSize, int max, int min) {
	
		ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
		do {
		if (pool == NULL) {
			printf("malloc threadpool error\n");
			break;
		}

		pool->workerIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->workerIDs == NULL) {
			printf("malloc workerIDs error \n");
			break;
		}

		memset(pool->workerIDs, 0x00, sizeof(pthread_t) * max);
		pool->maxThNum = max;
		pool->minThNum;//最小线程数
		pool->busyThNum = 0;
		pool->aliveThNum = min;
		pool->exitThNum = 0;

		if (pthread_mutex_init(&pool->busyMetux, NULL) != 0 ||
			pthread_mutex_init(&pool->poolMetux, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL != 0)) {

			printf("init cond or mutex error\n");
		}
		pool->task = (Task*)malloc(sizeof(Task) * queueSize);
		if (pool->task == NULL) {
			break;
		}
		pool->queueCapacity = queueSize;
		pool->queueFront = 0;
		pool->queueRear = 0;
		pool->queueSize = 0;
		pool->shutdown = 0; //0 on 1 off

		pthread_create(&pool->managerID, NULL, mannger, pool);
		for (int i = 0; i < min; ++i) {
			pthread_create(&pool->workerIDs[i], NULL, worker, pool);
		}
		return pool;
	} while (0);
	
	if (pool && pool->workerIDs) {
		free(pool->workerIDs);
		pool->workerIDs = NULL;
	}
	if (pool && pool->task) {
		free(pool->task);
		pool->task = NULL;
	}
	if (pool) {
		free(pool);
		pool = NULL;
	}
	return NULL;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg) {
	pthread_mutex_lock(& pool->poolMetux);
	while (pool->queueSize == pool->queueCapacity && (pool->shutdown == 0)) {
		pthread_cond_wait(&pool->notFull, &pool->poolMetux);
	}
	if (pool->shutdown) {
		pthread_mutex_unlock(&pool->poolMetux);
		return;
	}

	pool->task[pool->queueRear].func = func;
	pool->task[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % (pool->queueCapacity);
	pool->queueSize++;
	pthread_cond_signal(&pool->notFull);
	pthread_mutex_unlock(&pool->poolMetux);
}

int getBusyThreadNum(ThreadPool* pool) {
	int busynum = 0;
	pthread_mutex_lock(&pool->busyMetux);
	busynum = pool->busyThNum;
	pthread_mutex_unlock(&pool->busyMetux);
	return busynum;

}

int getAliveThreadNum(ThreadPool* pool) {
	int alivenum = 0;
	pthread_mutex_lock(&pool->poolMetux);
	alivenum = pool->aliveThNum ;
	pthread_mutex_unlock(&pool->poolMetux);
	return alivenum;

}

int destroyThreadPool(ThreadPool* pool) {
	if (pool == NULL) {
		return -1;
	}
	pool->shutdown = 1;
	pthread_join(pool->managerID, NULL);
	pthread_mutex_lock(&pool->poolMetux);
	for (int i = 0; i < pool->aliveThNum; i++) {
		pthread_cond_signal(&pool->notEmpty);
	}
	pthread_mutex_unlock(&pool->poolMetux);
	if(pool->workerIDs != NULL){
		free(pool->workerIDs);
		pool->workerIDs = NULL;
	}
	if (pool->task != NULL) {
		free(pool->task);
		pool->task = NULL;
	}
	
	pthread_mutex_destroy(&pool->poolMetux);
	pthread_mutex_destroy(&pool->busyMetux);
	pthread_cond_destroy(&pool->notFull);
	pthread_cond_destroy(&pool->notEmpty);
	free(pool);
	pool = NULL;
	return 0;

}

void taskfun (void* arg) {
	int num = *(int*)arg;
	printf("thread: %ld is working , num: %ld \n", pthread_self(), num);
	usleep(1000);
}
int main() {
	ThreadPool* pool = ThreadpoolCreate(100, 10, 3);
	for (int i = 0; i < 100; i++) {
		int* arg = (int*)malloc(sizeof(int));
		static j = 0;
		*(arg) = j++;
		threadPoolAdd(pool, taskfun, arg);
	}
		
	sleep(20);
	destroyThreadPool(pool);

	return 0;
}