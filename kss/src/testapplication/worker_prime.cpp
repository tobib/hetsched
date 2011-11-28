/* interface header */
#include "worker_prime.h"
#include "debug.h"

/* system implementation headers */
#include <stdio.h>
/* rand() function */
#include <stdlib.h>
/* memset & memcpy */
#include <string.h>

#include <cutil.h>
#include <cutil_inline.h>



/*
 *
 * Meta information methods
 *
 */
void Worker_prime::workerMetaInfo(struct meta_info *mi)
{
	/*
	 * In MB. The amount of memory which has to be transferred to and from the device.
	 * This will not be considered for the CPU implementations as they can access
	 * the main memory directly.
	 */
	mi->memory_to_copy = (int) floor(((double)((FACTORS_TO_FIND * 2 + 3) * 8) /(double)1024) / 1024 + 0.5);
	
	/*
	 * In MB. The amount of memory the algorithm typically allocates on the device.
	 */
	mi->memory_consumption = mi->memory_to_copy;
	
	/*
	 * On a scale from 0 (not parallelisable or small problem scale; no speedup expected from parallelisation)
   * to 5 (completely parallelisable large scale problem)
	 */
	if (this->resources.numberToTest <= 1000)
		mi->parallel_efficiency_gain = 0;
	else if (this->resources.numberToTest <= 10000)
		mi->parallel_efficiency_gain = 1;
	else if (this->resources.numberToTest <= 20000)
		mi->parallel_efficiency_gain = 2;
	else if (this->resources.numberToTest <= 50000)
		mi->parallel_efficiency_gain = 3;
	else if (this->resources.numberToTest <= 100000)
		mi->parallel_efficiency_gain = 4;
	else
		mi->parallel_efficiency_gain = 5;
	
	/*
	 * Array holding the affinity of the application towards the cu types.
	 * Affinity 0 means that no implementation is available for that type,
	 * otherwise higher values correspond to higher affinity.
	 * The programmer can use this to encode information about which type
	 * of cu has the best implementation for this problem, or only which
	 * implementations are given and which are not.
	 * Do not include parallelisability here as this is being considered by
	 * the scheduler itself.
	 */
	mi->type_affinity[CU_TYPE_CUDA] = 1;
	mi->type_affinity[CU_TYPE_FPGA] = 0;
	mi->type_affinity[CU_TYPE_CPU] = 1;

	}



void Worker_prime::checkResults(signed long* workstatus)
{
	stringstream stm("");
	
	*workstatus = (resources.nextIndex < FACTORS_TO_FIND && resources.remainder > 1 && (resources.currentDivisor-2) <= resources.currentSquareRoot);
  if (!*workstatus)
  {
		/* is remainder prime? */
		if (resources.remainder != 1 && resources.currentDivisor > resources.currentSquareRoot && resources.nextIndex < FACTORS_TO_FIND) {
			resources.primes[resources.nextIndex] = resources.remainder;
			resources.exponents[resources.nextIndex] += 1;
			resources.nextIndex++;
			resources.remainder = 1;
		}
		
    stm << " ";
		
		if (resources.nextIndex == 1) {
			stm << resources.numberToTest << " is prime!";
		} else {
			if (resources.remainder == 1)
				stm << "Found all ";
			else
				stm << "Found the first ";
			stm << resources.nextIndex << " prime factors of " << resources.numberToTest;
			if (resources.nextIndex > 0)
				stm << " (largest divisor is " << resources.primes[resources.nextIndex - 1] << ")";
			//WORKER_FOUND_SOLUTION(this->id, DBG_FINE, stm.str()); stm.str("");
			resources.foundsolution = true;
			
			stm << " (" << resources.currentDivisor << ")";
			
			/*
			stm << "  " << resources.numberToTest;
			char op = '=';
			for (int i=0; i < resources.nextIndex; ++i) {
				stm << " " << op << " " << resources.primes[i];
				if (resources.exponents[i] > 1)
					stm << "^" << resources.exponents[i];
				op = '*';
			}
			*/
		}
		WORKER_FOUND_SOLUTION(this->id, DBG_FINER, stm.str());
		
  } else
		*workstatus = resources.currentDivisor;
}





/*
 *
 * CPU Implementation
 *
 */

/* allocate cu memory and copy resources to cu */
void Worker_prime::testCandidate()
{
	bool isDivisor = false;
	while ((resources.remainder % resources.currentDivisor) == 0) {
		resources.remainder /= resources.currentDivisor;
		resources.primes[resources.nextIndex] = resources.currentDivisor;
		resources.exponents[resources.nextIndex] += 1;
		isDivisor = true;
	}
	if (isDivisor) {
		resources.currentSquareRoot = ceil(sqrt((double)(resources.remainder)));
		resources.nextIndex++;
	}
}

/* allocate cu memory and copy resources to cu */
void* Worker_prime::cpuinitFunc(computing_unit_shortinfo* cu)
{
  /* no need to copy anything for calculation on the cpu */
  return (void*) &(this->resources);
}
/* run algorithm from checkpoint to checkpoint */
void Worker_prime::cpumainFunc(computing_unit_shortinfo* cu, void* workingresources, signed long* workstatus)
{
  *workstatus = true;
	
  for (int candidates = 0; candidates < CANDIDATES_PER_BATCH && *workstatus; ++candidates)
  {
		testCandidate();
    resources.currentDivisor += 2; //even numbers are filtered in init
		checkResults(workstatus);
  }
}
/* copy resources back to main memory and free cu memory */
void Worker_prime::cpufreeFunc(computing_unit_shortinfo* cu, void* workingresources)
{
}





/*
 *
 * CUDA Implementation
 *
 */

/* copies the current results from the device to main memory */
static void copy_checkpoint(prime_cuda_resources_t *cudares, prime_resources_t *resources)
{
	//uint *nextIndex
  cutilDrvSafeCall( cuMemcpyDtoH (&resources->nextIndex, cudares->dev_nextIndex, sizeof(uint)));
	//ullong *primes
  cutilDrvSafeCall( cuMemcpyDtoH (resources->primes, cudares->dev_primes, FACTORS_TO_FIND*sizeof(ullong)));
	//ullong *exponents
  cutilDrvSafeCall( cuMemcpyDtoH (resources->exponents, cudares->dev_exponents, FACTORS_TO_FIND*sizeof(ullong)));
	//ullong *currentSquareRoot
  cutilDrvSafeCall( cuMemcpyDtoH (&resources->currentSquareRoot, cudares->dev_currentSquareRoot, sizeof(ullong)));
	//ullong *remainder
  cutilDrvSafeCall( cuMemcpyDtoH (&resources->remainder, cudares->dev_remainder, sizeof(ullong)));
}



static void execute_kernel(prime_cuda_resources_t *cudares, prime_resources_t *resources)
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
  cutilDrvSafeCallNoSync(cuParamSetv(cudares->cudaFunction, offset, &resources->currentDivisor, sizeof(resources->currentDivisor)));
  offset += sizeof(resources->currentDivisor);

  offset = (offset + __alignof(number_of_threads) - 1) & ~(__alignof(number_of_threads) - 1);
  cutilDrvSafeCall(cuParamSeti(cudares->cudaFunction, offset, number_of_threads));
  offset += sizeof(number_of_threads);

  offset = (offset + __alignof(iterations) - 1) & ~(__alignof(iterations) - 1);
  cutilDrvSafeCall(cuParamSeti(cudares->cudaFunction, offset, iterations));
  offset += sizeof(iterations);

  void* ptr = (void*)(size_t)cudares->dev_success;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(cudares->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)cudares->dev_primes;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(cudares->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)cudares->dev_exponents;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(cudares->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)cudares->dev_nextIndex;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(cudares->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)cudares->dev_currentSquareRoot;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(cudares->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)cudares->dev_remainder;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(cudares->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  cutilDrvSafeCall(cuParamSetSize(cudares->cudaFunction, offset));

  // set execution configuration
  dim3 block;
  block.x = threads_per_block; block.y = 1;
  cutilDrvSafeCall(cuFuncSetBlockShape(cudares->cudaFunction, block.x, block.y, 1 ));

  dim3 grid;
  grid.x = blocks_in_grid; grid.y = 1;

  cutilDrvSafeCall(cuFuncSetSharedSize (cudares->cudaFunction, 0));
  
  /* Launch the kernel */
  cutilDrvSafeCall(cuLaunchGridAsync( cudares->cudaFunction, grid.x, grid.y, cudares->cudastream ));

  /* Wait for its completion */
	cutilDrvSafeCall( cuCtxSynchronize() );
}


/* allocate cu memory and copy resources to cu */
void* Worker_prime::cudainitFunc(computing_unit_shortinfo* cu)
{
  // Initialize the driver API
  CUdevice cudaDevice;
  CUresult status;
  cuInit(0);
  const char *step;
  
  /* Get the available number of shared memory per block */
  step = "cuDeviceGetAttribute";
  if (cuDeviceGetAttribute( &cudares.shared_mem_available, CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK, cu->api_device_number ) != CUDA_SUCCESS)
    goto cleanup_and_fail;
  
  /* Create a context on the correct device */
  step = "cuDeviceGet";
  cutilDrvSafeCallNoSync(cuDeviceGet(&cudaDevice, cu->api_device_number));
  step = "cuCtxCreate";
  if ((status = cuCtxCreate( &cudares.cudaContext, CU_CTX_BLOCKING_SYNC | CU_CTX_SCHED_YIELD , cudaDevice )) != CUDA_SUCCESS)
    goto cleanup_and_fail;

  /* Get the pointer to the function inside the cubin */
  step = "cuModuleLoad";
  if ((status = cuModuleLoad(&cudares.cudaModule, "worker_prime.cubin")) != CUDA_SUCCESS)
    goto cleanup_and_fail;
  step = "cuModuleGetFunction";
  if ((status = cuModuleGetFunction(&cudares.cudaFunction, cudares.cudaModule, "prime_factor")) != CUDA_SUCCESS)
    goto cleanup_and_fail;

  /* create a stream for async operations */
  cutilDrvSafeCall( cuStreamCreate (&cudares.cudastream, 0) );
  
  // allocate GPU memory for match signal
	//uint *succ
  step = "Memory: dev_success";
  cutilDrvSafeCall( cuMemAlloc( &cudares.dev_success, sizeof(uint)));
  cutilDrvSafeCall( cuMemsetD8( cudares.dev_success, 0, sizeof(uint)));
	//uint *nextIndex
  step = "Memory: dev_nextIndex";
  cutilDrvSafeCall( cuMemAlloc( &cudares.dev_nextIndex, sizeof(uint)));
  cutilDrvSafeCall( cuMemcpyHtoD (cudares.dev_nextIndex, &resources.nextIndex, sizeof(uint)));
	//ullong *primes
  step = "Memory: dev_primes";
  cutilDrvSafeCall( cuMemAlloc( &cudares.dev_primes, FACTORS_TO_FIND*sizeof(ullong)));
  cutilDrvSafeCall( cuMemcpyHtoD (cudares.dev_primes, resources.primes, FACTORS_TO_FIND*sizeof(ullong)));
	//ullong *exponents
  step = "Memory: dev_exponents";
  cutilDrvSafeCall( cuMemAlloc( &cudares.dev_exponents, FACTORS_TO_FIND*sizeof(ullong)));
  cutilDrvSafeCall( cuMemcpyHtoD (cudares.dev_exponents, resources.exponents, FACTORS_TO_FIND*sizeof(ullong)));
	//ullong *currentSquareRoot
  step = "Memory: dev_currentSquareRoot";
  cutilDrvSafeCall( cuMemAlloc( &cudares.dev_currentSquareRoot, sizeof(ullong)));
  cutilDrvSafeCall( cuMemcpyHtoD (cudares.dev_currentSquareRoot, &resources.currentSquareRoot, sizeof(ullong)));
	//ullong *remainder
  step = "Memory: dev_remainder";
  cutilDrvSafeCall( cuMemAlloc( &cudares.dev_remainder, sizeof(ullong)));
  cutilDrvSafeCall( cuMemcpyHtoD (cudares.dev_remainder, &resources.remainder, sizeof(ullong)));
  
  return (void*) &(this->resources);

cleanup_and_fail:
	stringstream	stm("");
	stm << "cudainitFunc() failed at " << step << " with error " << status;
	workerAnnounce(this->id, DBG_ERROR, stm.str());
  cutilDrvSafeCall(cuCtxDetach(cudares.cudaContext));
  return NULL;
}



/* run algorithm from checkpoint to checkpoint */
void Worker_prime::cudamainFunc(computing_unit_shortinfo* cu, void* workingresources, signed long* workstatus)
{
  *workstatus = true;
	
  execute_kernel(&cudares, &resources);
  resources.currentDivisor += CANDIDATES_PER_BATCH_GPU;
	
  //stm << "     cuda returned after word #" << resources->currentWordNumber << endl;
  //DBG_FINE << stm.str(); stm.str("");
  uint ret = 0;
  cutilDrvSafeCall( cuMemcpyDtoH (&ret, cudares.dev_success, sizeof(uint)));
  
  if (ret) {
    /* All factors have been found, get the data */
		copy_checkpoint(&cudares, &resources);
		
		checkResults(workstatus);
    if (*workstatus) {
			stringstream	stm("");
			stm << "Bug: CUDA thinks all factors would have been found (" << ret << " vs " << *workstatus << ")";
			workerAnnounce(this->id, DBG_WARN, stm.str());
		}
    resources.foundsolution = true;
  }
}



/* copy resources back to main memory and free cu memory */
void Worker_prime::cudafreeFunc(computing_unit_shortinfo* cu, void* workingresources)
{
	/* copy checkpoint to main memory (only necessary if solution printing did not already) */
	if (!resources.foundsolution)
		copy_checkpoint(&cudares, &resources);
	
	/* free device resources */
  cutilDrvSafeCall(cuMemFree(cudares.dev_success));
  cutilDrvSafeCall(cuMemFree(cudares.dev_nextIndex));
  cutilDrvSafeCall(cuMemFree(cudares.dev_primes));
  cutilDrvSafeCall(cuMemFree(cudares.dev_exponents));
  cutilDrvSafeCall(cuMemFree(cudares.dev_currentSquareRoot));
  cutilDrvSafeCall(cuMemFree(cudares.dev_remainder));
  
  /* destroy execution context */
	cutilDrvSafeCall(cuModuleUnload(cudares.cudaModule));
  cutilDrvSafeCall(cuStreamDestroy(cudares.cudastream) );
  cutilDrvSafeCall(cuCtxDestroy(cudares.cudaContext));
}
















/* implementation multiplexer - must be overriden */
void Worker_prime::getImplementationFor(unsigned int type, accelerated_functions_t *af)
{
	//TODO honor type
	switch(type) {
		case CU_TYPE_CPU:
			af->init = static_cast<initFunc Worker::*> (&Worker_prime::cpuinitFunc);
			af->main = static_cast<mainFunc Worker::*> (&Worker_prime::cpumainFunc);
			af->free = static_cast<freeFunc Worker::*> (&Worker_prime::cpufreeFunc);

			af->initialized = true;
			break;
		case CU_TYPE_CUDA:
			af->init = static_cast<initFunc Worker::*> (&Worker_prime::cudainitFunc);
			af->main = static_cast<mainFunc Worker::*> (&Worker_prime::cudamainFunc);
			af->free = static_cast<freeFunc Worker::*> (&Worker_prime::cudafreeFunc);

			af->initialized = true;
			break;
		default:
			af->initialized = false;
	}
}









//////////////////////////////////////////////

// default ctor, initailize with random word
Worker_prime::Worker_prime(int id) : Worker(id)
{
  resources.foundsolution = false;
	/*
  srand ( (unsigned int)time ( NULL ) + rand());
  unsigned long long r;
  r = rand() * ( 1.0 / ( RAND_MAX + 1.0 )) * (ULONG_MAX);
	*/
	resources.numberToTest = BASE_NUMBER + id;// * 111;
	
  init();
	
	stringstream	stm("");
	stm << "Factorizing " << resources.numberToTest;
	WORKER_ANNOUNCE_TARGET(this->id, DBG_FINE, stm.str());
  
  workingresources = (void*)&resources;
	this->initialized = true;
}

//////////////////////////////

void Worker_prime::init()
{
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
}
