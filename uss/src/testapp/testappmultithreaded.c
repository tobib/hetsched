//basic
#include <stdlib.h>
#include <stdio.h>

//string
#include <string.h>
#include <sys/types.h>

//sleep
#include <unistd.h>

//threads
#include <pthread.h>

//USS
#include "../library/uss.h"

/*
 * this is a testapplication filling using the
 * library calls offered by uss_library
 */
 
 /*
  * CURRENT EXAMPLE
  * 
  * increment each value of an array by one
  *
  */
#define MYEXAMPLE_ARRAY 100

//////////////////////////////////////////////
//											//
// own user-defined USS structures			//
//											//
//////////////////////////////////////////////

//define your personal meta_data and meta_checkpoint
struct meta_checkpoint
{
	int curr;
};
  
struct meta_data
{
	size_t size;
	float *host_A;
	float *host_C;
	float *dev_A;
	float *dev_C;
	int start, stop, inc_granularity, is_finished;
};
 
 
//////////////////////////////////////////////
//											//
// CPU implementation						//
//											//
//////////////////////////////////////////////
int myalgo_cpu_init(void *md_void, void *mcp_void, int device_id)
{
	//no init for CPU
	printf("myalgo_CPU_init()\n");
	return 0;
}

int myalgo_cpu_main(void *md_void, void *mcp_void, int device_id)
{
	printf("myalgo_CPU_main()  sleeping for 1 sec  ");
	sleep(1);
	struct meta_data *md = (struct meta_data*) md_void;
	struct meta_checkpoint *mcp = (struct meta_checkpoint*) mcp_void;
	
	int i;
	for(i = mcp->curr; i < (mcp->curr + md->inc_granularity) && i < (md->stop); i++)
	{
		md->host_C[i] = md->host_A[i] + 1; 
	}
	
	mcp->curr = i; printf("exited main with: i = %i \n", i);
	
	if(i == md->stop) {md->is_finished = 1;}
	
	return 0;
}

 int myalgo_cpu_free(void *md_void, void *mcp_void, int device_id)
 {
	//no cleanup for CPU
	printf("myalgo_CPU_free()\n");
	return 0;
 }


//////////////////////////////////////////////
//											//
// MAIN (fills msi and calls libuss_start)	//
//											//
//////////////////////////////////////////////

void* use_uss(void* arg)
{
	//detach so this needn't be joined anyhow
	//pthread_detach(pthread_self());
	printf("\nnew thread spawned to simulate multiple thread in one process \n");
	
	int i;
	//
	//fill meta_sched_info struct
	//
	struct meta_sched_info msi;
	memset(&msi, 0, sizeof(struct meta_sched_info));
	
	struct meta_sched_info_element *element;
	memset(&element, 0, sizeof(struct meta_sched_info_element));
	
	//insert USS_ACCEL_TYPE_CPU
	msi.ptr[USS_ACCEL_TYPE_CPU] = (struct meta_sched_info_element*) malloc(sizeof(struct meta_sched_info_element));
	element = msi.ptr[USS_ACCEL_TYPE_CPU];
	if(!element) {printf("Error with malloc\n"); return NULL;}
	
	//element->accelerator_type = USS_ACCEL_TYPE_CPU;
	element->affinity = 2;
	element->flags = 0;
	element->init = &myalgo_cpu_init;
	element->main = &myalgo_cpu_main;
	element->free = &myalgo_cpu_free;

	//insert USS_ACCEL_TYPE_CUDA
	msi.ptr[USS_ACCEL_TYPE_CUDA] = (struct meta_sched_info_element*) malloc(sizeof(struct meta_sched_info_element));
	element = msi.ptr[USS_ACCEL_TYPE_CUDA];
	if(!element) {printf("Error with malloc\n"); return NULL;}
	
	//element->accelerator_type = USS_ACCEL_TYPE_CUDA;
	element->affinity = 3;
	element->flags = 4;
	element->init = &myalgo_cpu_init;
	element->main = &myalgo_cpu_main;
	element->free = &myalgo_cpu_free;

	//
	//fill meta_checkpoint struct
	//
	struct meta_checkpoint mcp;
	mcp.curr = 0;
	
	//
	//fill meta_data struct
	//
	struct meta_data md;
	
	md.host_A = (float*) malloc(MYEXAMPLE_ARRAY*sizeof(float)); if(!(md.host_A)) {printf("Error with malloc\n"); exit(0);}
	memset(md.host_A, 0, MYEXAMPLE_ARRAY*sizeof(float));
	md.host_C = (float*) malloc(MYEXAMPLE_ARRAY*sizeof(float)); if(!(md.host_C)) {printf("Error with malloc\n"); exit(0);}
	memset(md.host_A, 0, MYEXAMPLE_ARRAY*sizeof(float));
	
	md.size = sizeof(float)*MYEXAMPLE_ARRAY;
	md.stop = MYEXAMPLE_ARRAY - 1;
	md.inc_granularity= 15;
	md.is_finished = 0;

	//
	//print our data
	//
	printf("test data before algo:\n");
	for(i = 0; i < md.stop; i++) {printf("%i ", (int)md.host_C[i]);}
	printf("\n");
	//
	//now ready to call library function
	//
	int run_on;
	int device_id;
	libuss_start(&msi, (void*)&md, (void*)&mcp, &(md.is_finished), &run_on, &device_id);
	
	//
	//check our data
	//
	printf("test data after algo:\n");
	for(i = 0; i < md.stop; i++) {printf("%i ", (int)md.host_C[i]);}
	printf("\n");
	
	//
	//free mem
	//
	//for each msi.ptr[x] != NULL {free}
	
	return NULL;
}

int main()
 {
	//
	//greet user
	//
	printf("\nUSER SPACE SCHEDULER - test application\n");
	printf("---------------------------------------------------------------------------\n");
	
	pthread_t t1, t2, t3, t4;
	
	pthread_create(&t1, NULL, use_uss, NULL);
	pthread_create(&t2, NULL, use_uss, NULL);
	pthread_create(&t3, NULL, use_uss, NULL);
	pthread_create(&t4, NULL, use_uss, NULL);
	pthread_join(t1, NULL);
	pthread_join(t2, NULL);
	pthread_join(t3, NULL);
	pthread_join(t4, NULL);

	return 0;
 }
