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
  
 //USS
#include "../library/uss.h"

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
#define MYEXAMPLE_ARRAY 100

#define TESTDEBUG 1

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
	for(i = mcp->curr; i < (mcp->curr + md->inc_granularity && i < (md->stop); i++)
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
// CUDA	implementation						//
//											//
//////////////////////////////////////////////
__global__ void VecAdd(float* A, float* C, int offset)
{
	int i = threadIdx.x;
	C[i+offset] = A[i+offset] + 2;
}

int myalgo_cuda_init(void *md_void, void *mcp_void, int device_id)
{
	#if(TESTDEBUG == 1)	
	printf("myalgo_CUDA_init()\n");
	#endif
	sleep(1);
	struct meta_data *md = (struct meta_data*) md_void;
	cudaError_t retruntime;
	
	retruntime = cudaSetDevice(0);	
	if (retruntime != cudaSuccess) {printf("SetDevice Error%i\n", (int)retruntime); exit(1);}
	
	//get space on cuda
	retruntime = cudaMalloc(&(md->dev_A), md->size);
	if (retruntime != cudaSuccess) {printf("Error malloc: %i\n", (int)retruntime); exit(1);}
	retruntime = cudaMalloc(&(md->dev_C), md->size);
	if (retruntime != cudaSuccess) {printf("Error malloc %i\n", (int)retruntime); exit(1);}
	
	//copy original data and modified vector completely
	retruntime = cudaMemcpy(md->dev_A, md->host_A, md->size, cudaMemcpyHostToDevice);
	if (retruntime != cudaSuccess) {printf("Error Memcopy %i\n", (int)retruntime); exit(1);}
	retruntime = cudaMemcpy(md->dev_C, md->host_C, md->size, cudaMemcpyHostToDevice);
	if (retruntime != cudaSuccess) {printf("Error Memcopy %i\n", (int)retruntime); exit(1);}
	
	return 0;
}
 
int myalgo_cuda_main(void *md_void, void *mcp_void, int device_id)
{
	#if(TESTDEBUG == 1)	
	printf("myalgo_CUDA_main()  sleeping for 1 sec  ");
	#endif
	sleep(1);
	struct meta_data *md = (struct meta_data*) md_void;
	struct meta_checkpoint *mcp = (struct meta_checkpoint*) mcp_void;
	//
	//run kernel	
	//
	int threadsPerBlock, blocksPerGrid, N;
	//run for md->inc_granularityor less if we are close to stop
	if(mcp->curr + md->inc_granularity< md->stop)
	{
		N = md->inc_granularity;
	}
	else
	{
		N = md->stop - mcp->curr + 1;
	}
	
	
	threadsPerBlock = 256;
	blocksPerGrid = (N + threadsPerBlock -1) / threadsPerBlock;
	VecAdd<<<blocksPerGrid, threadsPerBlock>>>(md->dev_A, md->dev_C, mcp->curr);

	if(mcp->curr + md->inc_granularity< md->stop)
	{
		mcp->curr += (N); 
	}
	else
	{
		mcp->curr += (N-1);
	}
	
	#if(TESTDEBUG == 1)	
	printf("exited main with: mcp->curr = %i \n", mcp->curr);
	#endif
	
	if(mcp->curr == md->stop) {md->is_finished = 1;}
	if(mcp->curr >  md->stop) {printf("Error reached value greater than md->stop\n"); exit(-1);}
	
	return 0;
}
 
int myalgo_cuda_free(void *md_void, void *mcp_void, int device_id)
{
	#if(TESTDEBUG == 1)	
	printf("myalgo_CUDA_free()\n");
	#endif
	struct meta_data *md = (struct meta_data*) md_void;
	cudaError_t retruntime;
	
	//copy result back
	retruntime = cudaMemcpy(md->host_C, md->dev_C, md->size, cudaMemcpyDeviceToHost);
	if (retruntime != cudaSuccess) {printf("Error malloc \n"); exit(1);}
	
	//free device memory
	if(md->dev_C) cudaFree(md->dev_C);

	return 0;
}
 
//////////////////////////////////////////////
//											//
// MAIN (fills msi and calls libuss_start)	//
//											//
//////////////////////////////////////////////
int main(int argc, char *argv[])
 {
/***************************************\
* parse testapp input					*
\***************************************/
	int i, id = 0;
	int inc_granularity= 10;
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
	
/***************************************\
* fill meta sched info (MSI) struct		*
\***************************************/
	int ret = 0;
	struct meta_sched_info msi;
	memset(&msi, 0, sizeof(struct meta_sched_info));
	
	//insert USS_ACCEL_TYPE_CPU
	ret = libuss_fill_msi(&msi, USS_ACCEL_TYPE_CPU, 2, 0, 
					&myalgo_cpu_init, 
					&myalgo_cpu_main, 
					&myalgo_cpu_free);
	if(ret == -1) {printf("Error with malloc\n"); return -1;}
	

	//insert USS_ACCEL_TYPE_CUDA
		ret = libuss_fill_msi(&msi, USS_ACCEL_TYPE_CUDA, 3, 0, 
					&myalgo_cuda_init, 
					&myalgo_cuda_main, 
					&myalgo_cuda_free);
	if(ret == -1) {printf("Error with malloc\n"); return -1;}

/***************************************\
* fill meta checkpoint (MCP)			*
\***************************************/
	struct meta_checkpoint mcp;
	mcp.curr = 0;
	
/***************************************\
* fill meta data (MD)					*
\***************************************/
	struct meta_data md;
	
	md.host_A = (float*) malloc(MYEXAMPLE_ARRAY*sizeof(float)); if(!(md.host_A)) {printf("Error with malloc\n"); exit(0);}
	memset(md.host_A, 0, MYEXAMPLE_ARRAY*sizeof(float));
	md.host_C = (float*) malloc(MYEXAMPLE_ARRAY*sizeof(float)); if(!(md.host_C)) {printf("Error with malloc\n"); exit(0);}
	memset(md.host_A, 0, MYEXAMPLE_ARRAY*sizeof(float));
	
	md.size = sizeof(float)*MYEXAMPLE_ARRAY;
	md.stop = MYEXAMPLE_ARRAY - 1;
	md.inc_granularity= inc_granularity;
	md.is_finished = 0;
	
/***************************************\
* call libuss_start to enter			*
\***************************************/
	#if(TESTDEBUG == 1)
	printf("test data before algo:\n");
	for(i = 0; i < md.stop; i++) {printf("%i ", (int)md.host_C[i]);}
	#endif
	
	int run_on;
	int device_id;
	libuss_start(&msi, (void*)&md, (void*)&mcp, &(md.is_finished), &run_on, &device_id);
	
	#if(TESTDEBUG == 1)
	printf("test data after algo:\n");
	for(i = 0; i < md.stop; i++) {printf("%i ", (int)md.host_C[i]);}
	#endif
	
/***************************************\
* free meta sched info (MSI) memory		*
\***************************************/
	libuss_free_msi(&msi);
	
	return 0;
 }
