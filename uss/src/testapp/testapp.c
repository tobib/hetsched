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
//basic
#include <stdlib.h>
#include <stdio.h>

//string
#include <string.h>
#include <sys/types.h>

//sleep
#include <unistd.h>

//USS
#include "../library/uss.h"

#define TESTDEBUG 1
#define BENCHMARK_MAIN 1

#if(BENCHMARK_MAIN == 1)
#include "../benchmark/dwatch.h"
#endif


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
	#if(TESTDEBUG == 1)	
	printf("myalgo_CPU_init()\n");
	#endif
	return 0;
}

int myalgo_cpu_main(void *md_void, void *mcp_void, int device_id)
{
	#if(TESTDEBUG == 1)	
	printf("myalgo_CPU_main()  sleeping for 1 sec  ");
	#endif
	sleep(1);
	struct meta_data *md = (struct meta_data*) md_void;
	struct meta_checkpoint *mcp = (struct meta_checkpoint*) mcp_void;
	
	int i;
	for(i = mcp->curr; i < (mcp->curr + md->inc_granularity) && i < (md->stop); i++)
	{
		md->host_C[i] = md->host_A[i] + 1; 
	}
	
	mcp->curr = i; 
	#if(TESTDEBUG == 1)	
	printf("exited main with: i = %i \n", i);
	#endif
	if(i == md->stop) {md->is_finished = 1;}
	
	return 0;
}

 int myalgo_cpu_free(void *md_void, void *mcp_void, int device_id)
 {
	//no cleanup for CPU
	#if(TESTDEBUG == 1)	
	printf("myalgo_CPU_free()\n");
	#endif
	return 0;
 }


//////////////////////////////////////////////
//											//
// MAIN (fills msi and calls libuss_start)	//
//											//
//////////////////////////////////////////////
int main(int argc, char *argv[])
 {
	//
	//parse input
	//
	int i, id = 0;
	int inc_granularity= 0;
	if(argc == 1)
	{
		//default mode
		printf("<<< test application for uss_library>>>\n");
		inc_granularity= 10;
	}
	else if(argc == 3)
	{
		//benchmark mode
		id = atoi(argv[1]);
		inc_granularity= atoi(argv[2]);
	}
	else
	{
		printf("bad nof input parameters\n");
		exit(-1);
	}
	
#if(BENCHMARK_MAIN == 1)
	init_dwatch();
#endif
	
	//
	//fill meta_sched_info struct
	//
	struct meta_sched_info msi;
	memset(&msi, 0, sizeof(struct meta_sched_info));
	
	struct meta_sched_info_element *element;
	
	/*
	//insert USS_ACCEL_TYPE_CPU
	msi.ptr[USS_ACCEL_TYPE_CPU] = (struct meta_sched_info_element*) malloc(sizeof(struct meta_sched_info_element));
	element = msi.ptr[USS_ACCEL_TYPE_CPU];
	if(!element) {printf("Error with malloc\n"); return -1;}
	
	element->affinity = 2;
	element->flags = 0;
	element->init = &myalgo_cpu_init;
	element->main = &myalgo_cpu_main;
	element->free = &myalgo_cpu_free;
	*/
	
	//insert USS_ACCEL_TYPE_CUDA
	msi.ptr[USS_ACCEL_TYPE_CUDA] = (struct meta_sched_info_element*) malloc(sizeof(struct meta_sched_info_element));
	element = msi.ptr[USS_ACCEL_TYPE_CUDA];
	if(!element) {printf("Error with malloc\n"); return -1;}
	
	element->affinity = 10;
	element->flags = 4;
	element->init = &myalgo_cpu_init;
	element->main = &myalgo_cpu_main;
	element->free = &myalgo_cpu_free;

	//insert USS_ACCEL_TYPE_STREAM
	msi.ptr[USS_ACCEL_TYPE_STREAM] = (struct meta_sched_info_element*) malloc(sizeof(struct meta_sched_info_element));
	element = msi.ptr[USS_ACCEL_TYPE_STREAM];
	if(!element) {printf("Error with malloc\n"); return -1;}
	
	element->affinity = 8;
	element->flags = 4;
	element->init = &myalgo_cpu_init;
	element->main = &myalgo_cpu_main;
	element->free = &myalgo_cpu_free;

	//insert USS_ACCEL_TYPE_FPGA
	msi.ptr[USS_ACCEL_TYPE_FPGA] = (struct meta_sched_info_element*) malloc(sizeof(struct meta_sched_info_element));
	element = msi.ptr[USS_ACCEL_TYPE_FPGA];
	if(!element) {printf("Error with malloc\n"); return -1;}
	
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
	md.inc_granularity= inc_granularity;
	md.is_finished = 0;

#if(TESTDEBUG == 1)
	//print our data
	printf("test data before algo:\n");
	for(i = 0; i < md.stop; i++) {printf("%i ", (int)md.host_C[i]);}
	printf("here\n");
#endif

	//
	//now ready to call library function
	//
	int run_on;
	int device_id;
	libuss_start(&msi, (void*)&md, (void*)&mcp, &(md.is_finished), &run_on, &device_id);
	
	
#if(TESTDEBUG == 1)
	//check our data
	printf("test data after algo:\n");
	for(i = 0; i < md.stop; i++) {printf("%i ", (int)md.host_C[i]);}
#endif
	
	//
	//free mem
	//
	//for each msi.ptr[x] != NULL {free}
	
#if(BENCHMARK_MAIN == 1)
	//returns id and total turnaround time in ms
	printf("%i %lf\n", id, diff_dwatch());
#endif
	
	return 0;
 }
