/*
 * this is a testapplication filling using the
 * library calls offered by uss_library
 */
 
 /*
  * CURRENT EXAMPLE
  * prime factorization
  *
  * start with: testappprime <benchmark-id> <target-value>
  *
  */
  
//USS
#include "../library/uss.h"
#include <cuda.h>

#define TESTDEBUG 1
#define BENCHMARK_MAIN 1

#if(BENCHMARK_MAIN == 1)
#include "../benchmark/dwatch.h"
#endif

//////////////////////////////////////////////
//											//
// own user-defined USS structures			//
//											//
//////////////////////////////////////////////

#define FACTORS_TO_FIND 1000
#define CANDIDATES_PER_BATCH 1000
//#define CANDIDATES_PER_BATCH_GPU 100000
#define CANDIDATES_PER_BATCH_GPU 1000
// 99488307847707803 is prime!
#define BASE_NUMBER 99488307847707802

typedef unsigned int uint;
typedef unsigned long long ullong;

#include <string>
#include <iostream>
#include <math.h>
#include <cutil.h>
#include <cutil_inline.h>

//define your personal meta_data and meta_checkpoint
struct meta_checkpoint
{
	int is_finished;
	signed long *workstatus;
	
	unsigned long long numberToTest;
	unsigned long long remainder;
	unsigned long long currentDivisor;
	unsigned long long currentSquareRoot;

	unsigned int nextIndex;
	unsigned long long primes[FACTORS_TO_FIND];
	unsigned long long exponents[FACTORS_TO_FIND];
	bool foundsolution;

	int shared_mem_available;
	CUdeviceptr dev_success;
	CUdeviceptr dev_nextIndex;
	CUdeviceptr dev_primes;
	CUdeviceptr dev_exponents;
	CUdeviceptr dev_currentSquareRoot;
	CUdeviceptr dev_remainder;
	CUstream cudastream;
	CUcontext cudaContext;
	CUfunction cudaFunction;
	CUmodule cudaModule;

};
  
struct meta_data
{

};
 
//////////////////////////////////////////////
//											//
// helpers for all implementations			//
//											//
//////////////////////////////////////////////
void checkResults(struct meta_checkpoint *resources)
{
	*(resources->workstatus) = (resources->nextIndex < FACTORS_TO_FIND && resources->remainder > 1 && (resources->currentDivisor-2) <= resources->currentSquareRoot);
  if (!*(resources->workstatus))
  {
		/*****************\
		* added by Daniel *
		\*****************/
		resources->is_finished = 1;
  
		/* is remainder prime? */
		if (resources->remainder != 1 && resources->currentDivisor > resources->currentSquareRoot && resources->nextIndex < FACTORS_TO_FIND) 
		{
			resources->primes[resources->nextIndex] = resources->remainder;
			resources->exponents[resources->nextIndex] += 1;
			resources->nextIndex++;
			resources->remainder = 1;
		}
#if(TESTDEBUG == 1)		
		if (resources->nextIndex == 1) 
		{
			printf("%lld is prime!\n", resources->numberToTest);
		} 
		else 
		{
			if (resources->remainder == 1)
			{
				printf("Found ALL\n");
				for(int i = 0; i<FACTORS_TO_FIND; i++)
				{
					printf("prime: %lld multiplicity: %lld\n", resources->primes[i], resources->exponents[i]);
				}
			}	
			else
			{
				printf("Found the first\n");
			}
			printf("%i prime factors of %lld\n", resources->nextIndex, resources->numberToTest);
			if (resources->nextIndex > 0)
				printf(" (largest divisor is %lld)", resources->primes[resources->nextIndex - 1]);
			resources->foundsolution = true;
			printf("(%lld)", resources->currentDivisor);
		}
#endif

  } 
  else
	{
	*(resources->workstatus) = resources->currentDivisor;
	}
}
 

 
//////////////////////////////////////////////
//											//
// CPU implementation						//
//											//
//////////////////////////////////////////////
/* allocate cu memory and copy resources to cu */
void testCandidate(struct meta_checkpoint *resources)
{
	bool isDivisor = false;
	while ((resources->remainder % resources->currentDivisor) == 0) {
		resources->remainder /= resources->currentDivisor;
		resources->primes[resources->nextIndex] = resources->currentDivisor;
		resources->exponents[resources->nextIndex] += 1;
		isDivisor = true;
	}
	if (isDivisor) {
		resources->currentSquareRoot = ceil(sqrt((double)(resources->remainder)));
		resources->nextIndex++;
	}
}

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
	printf("myalgo_CPU_main()");
#endif
	//struct meta_data *md = (struct meta_data*) md_void;
	struct meta_checkpoint *resources = (struct meta_checkpoint*) mcp_void;

	*(resources->workstatus) = true;
	
	for (int candidates = 0; candidates < CANDIDATES_PER_BATCH && *(resources->workstatus); ++candidates)
	{
		testCandidate(resources);
		resources->currentDivisor += 2; //even numbers are filtered in init
		checkResults(resources);
	}

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
/* copies the current results from the device to main memory */
static void copy_checkpoint(struct meta_checkpoint *resources)
{
	//uint *nextIndex
  cutilDrvSafeCall( cuMemcpyDtoH (&resources->nextIndex, resources->dev_nextIndex, sizeof(uint)));
	//ullong *primes
  cutilDrvSafeCall( cuMemcpyDtoH (resources->primes, resources->dev_primes, FACTORS_TO_FIND*sizeof(ullong)));
	//ullong *exponents
  cutilDrvSafeCall( cuMemcpyDtoH (resources->exponents, resources->dev_exponents, FACTORS_TO_FIND*sizeof(ullong)));
	//ullong *currentSquareRoot
  cutilDrvSafeCall( cuMemcpyDtoH (&resources->currentSquareRoot, resources->dev_currentSquareRoot, sizeof(ullong)));
	//ullong *remainder
  cutilDrvSafeCall( cuMemcpyDtoH (&resources->remainder, resources->dev_remainder, sizeof(ullong)));
}


static void execute_kernel(struct meta_checkpoint *resources)
{
  //
  // The icky part: compute the optimal number of threads per block,
  // and the number of blocks
  //
  int number_of_threads = min(CANDIDATES_PER_BATCH_GPU, 1000000); // just a guess of what is efficiently possible
	int iterations = ceil(CANDIDATES_PER_BATCH_GPU / number_of_threads);

  int threads_per_block = 512;
  int blocks_in_grid = ceil((double)number_of_threads / (double)threads_per_block);

  // set kernel parameters
	// prime_factor(ullong currentDivisor, uint number_of_threads, uint iterations, uint *succ, ullong *primes, ullong *exponents, uint *nextIndex, ullong *currentSquareRoot, ullong *remainder)
  int offset = 0;
  offset = (offset + __alignof(resources->currentDivisor) - 1) & ~(__alignof(resources->currentDivisor) - 1); // adjust offset to meet alignment requirement
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &resources->currentDivisor, sizeof(resources->currentDivisor)));
  offset += sizeof(resources->currentDivisor);

  offset = (offset + __alignof(number_of_threads) - 1) & ~(__alignof(number_of_threads) - 1);
  cutilDrvSafeCall(cuParamSeti(resources->cudaFunction, offset, number_of_threads));
  offset += sizeof(number_of_threads);

  offset = (offset + __alignof(iterations) - 1) & ~(__alignof(iterations) - 1);
  cutilDrvSafeCall(cuParamSeti(resources->cudaFunction, offset, iterations));
  offset += sizeof(iterations);

  void* ptr = (void*)(size_t)resources->dev_success;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)resources->dev_primes;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)resources->dev_exponents;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)resources->dev_nextIndex;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)resources->dev_currentSquareRoot;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)resources->dev_remainder;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  cutilDrvSafeCall(cuParamSetSize(resources->cudaFunction, offset));

  // set execution configuration
  dim3 block;
  block.x = threads_per_block; block.y = 1;
  cutilDrvSafeCall(cuFuncSetBlockShape(resources->cudaFunction, block.x, block.y, 1 ));

  dim3 grid;
  grid.x = blocks_in_grid; grid.y = 1;

  cutilDrvSafeCall(cuFuncSetSharedSize (resources->cudaFunction, 0));
  
  /* Launch the kernel */
  cutilDrvSafeCall(cuLaunchGridAsync( resources->cudaFunction, grid.x, grid.y, resources->cudastream ));

  /* Wait for its completion */
	cutilDrvSafeCall( cuCtxSynchronize() );
}


int myalgo_cuda_init(void *md_void, void *mcp_void, int device_id)
{
#if(TESTDEBUG == 1)
	printf("myalgo_CUDA_init()\n");
#endif
	//struct meta_data *md = (struct meta_data*) md_void;
	struct meta_checkpoint *resources = (struct meta_checkpoint*) mcp_void;
	
	/*****************\
	* added by Daniel *
	\*****************/
	/*Tobias implementation wanted a computing_unit_shortinfo
	 *containing info about available devices
	 *-> here use first device => 0
	 */
	// Initialize the driver API
	CUdevice cudaDevice;
	CUresult status;
	cuInit(0);
	const char *step;

	/* Get the available number of shared memory per block */
	step = "cuDeviceGetAttribute";
	if (cuDeviceGetAttribute( &resources->shared_mem_available, CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK, 0 ) != CUDA_SUCCESS)
	goto cleanup_and_fail;

	/* Create a context on the correct device */
	step = "cuDeviceGet";
	cutilDrvSafeCallNoSync(cuDeviceGet(&cudaDevice,0));
	step = "cuCtxCreate";
	if ((status = cuCtxCreate( &resources->cudaContext, /*Daniel: omitting param bof error CU_CTX_BLOCKING_SYNC | CU_CTX_SCHED_YIELD*/ 0 , cudaDevice )) != CUDA_SUCCESS)
	goto cleanup_and_fail;

	/* Get the pointer to the function inside the cubin */
	step = "cuModuleLoad";
	if ((status = cuModuleLoad(&resources->cudaModule, "./testapp/prime.cubin")) != CUDA_SUCCESS)
	goto cleanup_and_fail;
	step = "cuModuleGetFunction";
	if ((status = cuModuleGetFunction(&resources->cudaFunction, resources->cudaModule, "prime_factor")) != CUDA_SUCCESS)
	goto cleanup_and_fail;

	/* create a stream for async operations */
	cutilDrvSafeCall( cuStreamCreate (&resources->cudastream, 0) );

	// allocate GPU memory for match signal
	//uint *succ
	step = "Memory: dev_success";
	cutilDrvSafeCall( cuMemAlloc( &resources->dev_success, sizeof(uint)));
	cutilDrvSafeCall( cuMemsetD8( resources->dev_success, 0, sizeof(uint)));
	//uint *nextIndex
	step = "Memory: dev_nextIndex";
	cutilDrvSafeCall( cuMemAlloc( &resources->dev_nextIndex, sizeof(uint)));
	cutilDrvSafeCall( cuMemcpyHtoD (resources->dev_nextIndex, &resources->nextIndex, sizeof(uint)));
	//ullong *primes
	step = "Memory: dev_primes";
	cutilDrvSafeCall( cuMemAlloc( &resources->dev_primes, FACTORS_TO_FIND*sizeof(ullong)));
	cutilDrvSafeCall( cuMemcpyHtoD (resources->dev_primes, resources->primes, FACTORS_TO_FIND*sizeof(ullong)));
	//ullong *exponents
	step = "Memory: dev_exponents";
	cutilDrvSafeCall( cuMemAlloc( &resources->dev_exponents, FACTORS_TO_FIND*sizeof(ullong)));
	cutilDrvSafeCall( cuMemcpyHtoD (resources->dev_exponents, resources->exponents, FACTORS_TO_FIND*sizeof(ullong)));
	//ullong *currentSquareRoot
	step = "Memory: dev_currentSquareRoot";
	cutilDrvSafeCall( cuMemAlloc( &resources->dev_currentSquareRoot, sizeof(ullong)));
	cutilDrvSafeCall( cuMemcpyHtoD (resources->dev_currentSquareRoot, &resources->currentSquareRoot, sizeof(ullong)));
	//ullong *remainder
	step = "Memory: dev_remainder";
	cutilDrvSafeCall( cuMemAlloc( &resources->dev_remainder, sizeof(ullong)));
	cutilDrvSafeCall( cuMemcpyHtoD (resources->dev_remainder, &resources->remainder, sizeof(ullong)));

	return 0;

	cleanup_and_fail:
	printf("myalgo_cuda_init() failed at step ' %s ' with error ' %i ' \n", step, (int)status);
	cutilDrvSafeCall(cuCtxDetach(resources->cudaContext));	
	return 0;
}
 
int myalgo_cuda_main(void *md_void, void *mcp_void, int device_id)
{
#if(TESTDEBUG == 1)
	printf("myalgo_CUDA_main()\n");
#endif
	struct meta_checkpoint *resources = (struct meta_checkpoint*) mcp_void;

	*(resources->workstatus) = true;

	execute_kernel(resources);
	resources->currentDivisor += CANDIDATES_PER_BATCH_GPU;

	//stm << "     cuda returned after word #" << resources->currentWordNumber << endl;
	//DBG_FINE << stm.str(); stm.str("");
	uint ret = 0;
	cutilDrvSafeCall( cuMemcpyDtoH (&ret, resources->dev_success, sizeof(uint)));

	if (ret) {
	/* All factors have been found, get the data */
		copy_checkpoint(resources);
		
		checkResults(resources);
	if (*(resources->workstatus)) {
			printf("Bug: CUDA thinks all factors would have been found\n");
		}
	resources->foundsolution = true;
	}

	return 0;
}
 
int myalgo_cuda_free(void *md_void, void *mcp_void, int device_id)
{
#if(TESTDEBUG == 1)
	printf("myalgo_CUDA_free()\n");
#endif
	struct meta_checkpoint *resources = (struct meta_checkpoint*) mcp_void;

	/* copy checkpoint to main memory (only necessary if solution printing did not already) */
	if (!resources->foundsolution)
		copy_checkpoint(resources);

	/* free device resources */
	cutilDrvSafeCall(cuMemFree(resources->dev_success));
	cutilDrvSafeCall(cuMemFree(resources->dev_nextIndex));
	cutilDrvSafeCall(cuMemFree(resources->dev_primes));
	cutilDrvSafeCall(cuMemFree(resources->dev_exponents));
	cutilDrvSafeCall(cuMemFree(resources->dev_currentSquareRoot));
	cutilDrvSafeCall(cuMemFree(resources->dev_remainder));

	/* destroy execution context */
	cutilDrvSafeCall(cuModuleUnload(resources->cudaModule));
	cutilDrvSafeCall(cuStreamDestroy(resources->cudastream) );
	cutilDrvSafeCall(cuCtxDestroy(resources->cudaContext));

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
	int id = 0;
	long prime = 0;
	if(argc == 1)
	{
		//default mode: just use own BASE_NUMBER
		printf("<<< test application for uss_library>>>\n");
		prime = BASE_NUMBER;
	}
	else if(argc == 3)
	{
		id = atoi(argv[1]);
		long t = atol(argv[2]);
		if(t == 0)
		{
			//tobias mode: use id to increase base prime
			prime = BASE_NUMBER + id;
		}
		else
		{
			//manual mode: second param is user-defined prime value
			prime = t;
		}
	}
	else
	{
		printf("bad nof input parameters\n");
		exit(-1);
	}
	
	#if(BENCHMARK_MAIN == 1)
	init_dwatch();
	#endif
	
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
	struct meta_checkpoint resources;

	resources.foundsolution = false;
	resources.numberToTest = prime + 0;// * 111;  
	resources.remainder = resources.numberToTest;
	resources.currentDivisor = 3;
	resources.nextIndex = 0;
	while (resources.remainder > 1 && (resources.remainder % 2) == 0) {
		resources.remainder /= 2;
		resources.primes[0] = 2;
		resources.exponents[0] += 1;
		resources.nextIndex = 1;
	}
	resources.currentSquareRoot = ceil(sqrt((double)(resources.remainder))); 
  
	/*this variable by tobias is design to hold the status
	 *
	 *0: nothing to do any more => finished
	 *X: holds currentDivisor
	 *
	 */
	signed long workstatus = true;
	resources.workstatus = &workstatus;
	
	/*this variable is my own finished indicator
	 *0=still work to do
	 *1=finished
	 */
	resources.is_finished = 0;
	
/***************************************\
* fill meta data (MD)					*
\***************************************/
	struct meta_data md;
	//empty in this example
	
/***************************************\
* call libuss_start to enter			*
\***************************************/
	int run_on;
	int device_id;
	libuss_start(&msi, (void*)&md, (void*)&resources, &(resources.is_finished), &run_on, &device_id);
	
/***************************************\
* free meta sched info (MSI) memory		*
\***************************************/
	libuss_free_msi(&msi);
	

	#if(BENCHMARK_MAIN == 1)
	//returns id and total turnaround time in ms
	printf("%i %ld\n", id, (long)diff_dwatch());
	#endif
	return 0;
 }
