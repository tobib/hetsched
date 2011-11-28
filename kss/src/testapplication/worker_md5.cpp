/** \file worker_md5.cpp
 *
 *  \brief Source of the md5-hash-cracking worker
 *
 * This worker uses a brute-force algorithm to find the source string of a
 * predefined md5 hash.
 *
 * File taken from http://www.zedwood.com/article/121/cpp-md5-function
 *
 *  MD5
 *  - converted to C++ class by Frank Thilo (thilo@unix-ag.org)
 *  for bzflag (http://www.bzflag.org)
 *  - modified to use accelerator timesharing kernel by Tobias Wiersema in 2010
 * 
 *    based on:
 * 
 *    md5.h and md5.c
 *    reference implementation of RFC 1321
 * 
 *    Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.
 * 
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 * 
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 * 
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 * 
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 * 
 */


/* interface header */
#include "worker_md5.h"
#include "debug.h"

/* system implementation headers */
#include <stdio.h>
/* rand() function */
#include <stdlib.h>
/* memset & memcpy */
#include <string.h>

#include <cutil.h>
#include <cutil_inline.h>

//! Helper macro to walk all characters of a source string
#define for_each_word_position_i for (int i=0; i<ORIGINAL_WORD_LENGTH; ++i)


/** \brief Meta information generator for the class Worker_md5 - implements the virtual Worker::workerMetaInfo
 *
 * Generates the meta information needed by the kernel to correctly identify
 * appropriate computing units for the MD5 cracking algorithm. Writes the
 * following information into struct mi:
 *
 * - memory_to_copy: None
 * - memory_consumption: 1 MB
 * - parallel_efficiency_gain: Depending on size of search space
 * - type_affinity: Twice as affine to GPUs than to CPUs
 * 
 * \param [out]  mi  struct meta_info as defined in the kernel sources, will be
 *                   filled by this function with information about md5 cracking
 */
void Worker_md5::workerMetaInfo(struct meta_info *mi)
{
	/*
	 * In MB. The amount of memory which has to be transferred to and from the device.
	 * This will not be considered for the CPU implementations as they can access
	 * the main memory directly.
	 */
	mi->memory_to_copy = 0;
	
	/*
	 * In MB. The amount of memory the algorithm typically allocates on the device.
	 */
	mi->memory_consumption = 1;
	
	/*
	 * On a scale from 0 (not parallelisable or small problem scale; no speedup expected from parallelisation)
   * to 5 (completely parallelisable large scale problem)
	 */
	unsigned long long possibilities = (unsigned long long) pow(NUM_OF_CHARS, ORIGINAL_WORD_LENGTH);
	if (possibilities <= 1000)
		mi->parallel_efficiency_gain = 0;
	else if (possibilities <= 10000)
		mi->parallel_efficiency_gain = 1;
	else if (possibilities <= 20000)
		mi->parallel_efficiency_gain = 2;
	else if (possibilities <= 50000)
		mi->parallel_efficiency_gain = 3;
	else if (possibilities <= 100000)
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
	mi->type_affinity[CU_TYPE_CUDA] = 2;
	mi->type_affinity[CU_TYPE_FPGA] = 0;
	mi->type_affinity[CU_TYPE_CPU] = 1;
}






/**
 * \defgroup MD5CPUImpl CPU Implementation of MD5 cracking
 * 
 * CPU Implementation
 * @{
 */

/**
 * \brief Increases the current word in brute-force fashion
 *
 * Takes the current word and increases it in normal brute-force fashion:
 * aaaa -> aaab -> aaac and so on. Returns true as long as there were words
 * left to generate.
 *
 * \param [in,out]  word  char array representing the last word that the
 *                        algorithm checked, will be replaced with the next one
 * \param [in]       len  length of word
 *
 * \return      True if word contains the next word, false if it already
 *              contained the last word of the search space
 */
bool nextIndexWord(char* word, int len)
{
  int maxsize = NUM_OF_CHARS - 1;
  bool overflow = false;
  
  /* increase from lowest to highest bit */
  for (int i = len - 1; i >= 0; --i) {
    overflow = false;
    if (word[i] < maxsize) {
      word[i]++;
    } else {
      word[i] = 0;
      overflow = true;
    }
    /* stop if all overflow has been accounted for */
    if (!overflow)
      break;
  }
  /* if even the first bit overflowed, we have reached the end of our range... */
  return !overflow;
}

/**
 * \brief Converts a number to a string from the search space
 *
 * Takes the current word encoded as a number, assuming the search space to be 
 * numbered (e.g. 0="aaaa", 1="aaab", 3="aaac", ...). Calculates which source
 * string is represented by this number, and returns an array of indices into
 * the alphabet #MD5_POOL.
 *
 * NOTE: This function will fail badly (= terminate the application) if it is
 * called with a number that is too large to encode a string from the search
 * space, i.e.
 * \f$\mathrm{number} \geq \mathrm{NUM\_OF\_CHARS}^{(\mathrm{ORIGINAL\_WORD\_LENGTH}+1)}\f$
 *
 * \param [in]  number  index into the search space, representing a source
 *                      string
 * \param [out]   word  char array of length ORIGINAL_WORD_LENGTH, in which the
 *                      MD5_POOL-indices of the represented word will be
 *                      returned
 * \param [in]      id  id of the calling thread, will be used for the output
 *                      in the above mentioned failure case
 */
void number2indexword (unsigned long long number, char *word, int id)
{
	uint x=0;
	unsigned long long temp, step;
	step = (unsigned long long) pow(NUM_OF_CHARS, (ORIGINAL_WORD_LENGTH));
	if (number > step) {
		stringstream	stm("");
		stm << " Fatal error! Number " << number << " is too large (maximum should be " << step << ")";
		workerAnnounce(id, DBG_CRIT, stm.str()); stm.str("");
		exit(-1);
	}
	step /= NUM_OF_CHARS;

	/*
	 * Special case: Length of words is 0 or 1
	 * These cases can be determined at compile time and can therefore
	 * be optimized away by the compiler
	 */
	if (ORIGINAL_WORD_LENGTH < 1)
		return;
	else if (ORIGINAL_WORD_LENGTH == 1) {
		word[x] = number;
		return;
	}
	
	word[x] = 0;
	while (1) {
		if (number <= step) {
			x++;
			step /= NUM_OF_CHARS;
			if (x >= ORIGINAL_WORD_LENGTH-1)
				break;
			word[x] = 0;
			continue;
		}
		temp = (uint)floor(number/step);
		word[x] = temp;
		number -= ((unsigned long long) temp * step);
	}
	word[ORIGINAL_WORD_LENGTH-1] = number;
}

//! Dummy function to "initialize" a CPU - does nothing
void* Worker_md5::cpuinitFunc(computing_unit_shortinfo* cu)
{
  /* no need to copy anything for calculation on the cpu */
  return (void*) &(this->resources);
}
/**
 * \brief CPU version of the MD5 brute-force algorithm, running to the next checkpoint
 * 
 * This function searches through one batch of source strings of size
 * #WORDS_PER_BATCH, i.e. it computes uninterrupted to the next checkpoint.
 *
 * \param [in]      cu  unused for CPU implementation of MD5 cracking
 * \param [in,out] workingresources  unused for CPU implementation of MD5
 *                      cracking, as the CPU version has direct access to
 *                      Worker_md5::resources
 * \param [out] workstatus  flag to tell the main loop in class Worker if there
 *                      is still work to be done after this function returns
 */
void Worker_md5::cpumainFunc(computing_unit_shortinfo* cu, void* workingresources, signed long* workstatus)
{
  char md5pool[sizeof(MD5POOL)] = MD5POOL;
  char currentBruteforceIndex[ORIGINAL_WORD_LENGTH+1];
  char currentword[ORIGINAL_WORD_LENGTH+1];
  currentword[ORIGINAL_WORD_LENGTH] = 0;
  currentBruteforceIndex[ORIGINAL_WORD_LENGTH] = 0;
  std::string currentmd5;
  bool bWordsleft = false;
  
  *workstatus = true;
  /* initialize the current index word to the correct number */
  number2indexword(resources.currentWordNumber, &currentBruteforceIndex[0], this->id);

  /* Work to the next checkpoint */
  for (int words = 0; words < WORDS_PER_BATCH && *workstatus; ++words)
  {
    /* calculate the hash of the current word */
    init();
    for (int i=0; i<ORIGINAL_WORD_LENGTH; ++i)
    {
      currentword[i] = md5pool[currentBruteforceIndex[i]];
    }
    update(currentword, sizeof(currentword)-1);
    finalize();
    currentmd5 = hexdigest();
    
    /* update counters and flags for next iteration */
    bWordsleft = nextIndexWord(&(currentBruteforceIndex[0]), ORIGINAL_WORD_LENGTH);
    resources.currentWordNumber++;
    *workstatus = (bWordsleft && resources.hash_to_search != currentmd5);
  }
  
  /* produce some debug output */
	if (resources.currentWordNumber % 5000000 == 0)
	{
		stringstream	stm("");
		stm << " Currently working on cpu (" << resources.currentWordNumber << ")";
		WORKER_ANNOUNCE_CPUWORK(this->id, DBG_FINEST, stm.str());
	}
  
  /* check if the target string has been found */
  if (!*workstatus && bWordsleft)
  {
		stringstream	stm("");
		stm << " Found solution " << string(currentword) << " (" << resources.currentWordNumber << ")";
		WORKER_FOUND_SOLUTION(this->id, DBG_FINE, stm.str());
    resources.foundsolution = true;
  } else if (bWordsleft)
		*workstatus = resources.currentWordNumber;
}
//! Dummy function to "free" a CPU - does nothing
void Worker_md5::cpufreeFunc(computing_unit_shortinfo* cu, void* workingresources)
{
}

/**@}*/









/*
 *
 * CUDA Implementation
 *
 */
/**
 * \defgroup MD5CUDAImpl CUDA Implementation of MD5 cracking
 * 
 * CUDA Implementation
 * @{
 */

//! MD5 magic numbers. These will be loaded into on-device "constant" memory
static const uint k_cpu[64] =
{
  0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
  0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
  0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
  0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,

  0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
  0xd62f105d, 0x2441453,  0xd8a1e681, 0xe7d3fbc8,
  0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
  0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,

  0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
  0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
  0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x4881d05,
  0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,

  0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
  0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
  0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
  0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

//! MD5 magic numbers. These will be loaded into on-device "constant" memory
static const uint rconst_cpu[16] =
{
  7, 12, 17, 22,   5,  9, 14, 20,   4, 11, 16, 23,   6, 10, 15, 21
};



/**
 * \brief Function to copy over the constants to the gpu
 * 
 * Copies all constants that are needed by MD5 computation to the GPU
 * 
 * \param [in] cudaModule  pointer to the cuda module, as used in the driver API
 *                         and needed by the cuModuleGetGlobal calls
 */
void init_constants(CUmodule *cudaModule)
{
  CUdeviceptr dptr;
  unsigned int bytes;

  cutilDrvSafeCall( cuModuleGetGlobal( &dptr, &bytes, *cudaModule, "k" ));
  cutilDrvSafeCall( cuMemcpyHtoD(dptr,  k_cpu,  bytes));

  cutilDrvSafeCall( cuModuleGetGlobal( &dptr, &bytes, *cudaModule, "rconst" ));
  cutilDrvSafeCall( cuMemcpyHtoD(dptr,  rconst_cpu,  bytes));

  if (ORIGINAL_WORD_LENGTH > 0) {
    uint steps_cpu[ORIGINAL_WORD_LENGTH];
    steps_cpu[ORIGINAL_WORD_LENGTH-1] = 1;
    for (int i = ORIGINAL_WORD_LENGTH-2; i >= 0; --i)
      steps_cpu[i] = steps_cpu[i+1] * NUM_OF_CHARS;

    cutilDrvSafeCall( cuModuleGetGlobal( &dptr, &bytes, *cudaModule, "steps" ));
    cutilDrvSafeCall( cuMemcpyHtoD(dptr,  steps_cpu,  bytes));
  }
}



/**
 * \brief A helper to export the kernel call to C++ code not compiled with nvcc
 * 
 * Executes the cuda kernel using the CUDA driver API
 * 
 * \param [in] starting_number  number encoding the point in the search space
 *                         from which this kernel call should start searching,
 *                         i.e. the last checkpoint
 * \param [in]  resources  The Worker_md5::resources struct of this
 *                         worker, should contain valid CUDA data structures
 */
static void execute_kernel(unsigned long long starting_number, md5_resources_t *resources)
{
  //
  // The icky part: compute the optimal number of threads per block,
  // and the number of blocks
  //
  int dynShmemPerThread = 64;	// built in the algorithm
  int staticShmemPerBlock = 32;	// read from .cubin file
  //int words_per_call = 25000; // just a guess of what is efficiently possible
  int words_per_call = 1000000; // just a guess of what is efficiently possible
	int iterations = ceil(WORDS_PER_BATCH_GPU / words_per_call);

  int threads_per_block = min((double)512, floor((double)(resources->shared_mem_available - staticShmemPerBlock) / (double)dynShmemPerThread));
  int blocks_in_grid = ceil((double)words_per_call / (double)threads_per_block);

  //printf("words_per_call = %d, iterations = %d, threads_per_block = %d, blocks_in_grid = %d\n", words_per_call, iterations, threads_per_block, blocks_in_grid);

  // set kernel parameters
  // md5_search(uint starting_number, uint words_per_call, uint max_number, uint *succ)
  int offset = 0;
  offset = (offset + __alignof(starting_number) - 1) & ~(__alignof(starting_number) - 1); // adjust offset to meet alignment requirement
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &starting_number, sizeof(starting_number)));
  offset += sizeof(starting_number);

  offset = (offset + __alignof(words_per_call) - 1) & ~(__alignof(words_per_call) - 1);
  cutilDrvSafeCall(cuParamSeti(resources->cudaFunction, offset, words_per_call));
  offset += sizeof(words_per_call);

  offset = (offset + __alignof(iterations) - 1) & ~(__alignof(iterations) - 1);
  cutilDrvSafeCall(cuParamSeti(resources->cudaFunction, offset, iterations));
  offset += sizeof(iterations);

  unsigned long long max_number = (unsigned long long) pow(NUM_OF_CHARS, ORIGINAL_WORD_LENGTH);
  offset = (offset + __alignof(max_number) - 1) & ~(__alignof(max_number) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &max_number, sizeof(max_number)));
  offset += sizeof(max_number);

  void* ptr = (void*)(size_t)resources->dev_success;
  offset = (offset + __alignof(ptr) - 1) & ~(__alignof(ptr) - 1);
  cutilDrvSafeCallNoSync(cuParamSetv(resources->cudaFunction, offset, &ptr, sizeof(ptr)));
  offset += sizeof(ptr);

  ptr = (void*)(size_t)resources->dev_target;
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

  cutilDrvSafeCall(cuFuncSetSharedSize (resources->cudaFunction, threads_per_block*dynShmemPerThread));
  
  //cout << "Executing with " << starting_number << ", " << words_per_call << ", " << max_number << ", " << resources->dev_success << ", " << resources->dev_target << "\n";
  /* Launch the kernel */
  cutilDrvSafeCall(cuLaunchGridAsync( resources->cudaFunction, grid.x, grid.y, resources->cudastream ));

  /* Wait for its completion */
	cutilDrvSafeCall( cuCtxSynchronize() );
}



/**
 * \brief Allocates GPU memory and copies the last checkpoint to it
 * 
 * Prepares the GPU for (repeated) calls of the md5 cracking cuda kernel. Creates the context, allocates memory, copies constants, and loads the kernel from disk.
 *
 * \param [in]      cu  member api_device_number is used to determine the index
 *                      of the CUDA card which has been assigned by the kernel
 */
void* Worker_md5::cudainitFunc(computing_unit_shortinfo* cu)
{
  // Initialize the driver API
  CUdevice cudaDevice;
  CUresult status;
  cuInit(0);
  const char *step;
  
  /* Get the available number of shared memory per block */
  step = "cuDeviceGetAttribute";
  if (cuDeviceGetAttribute( &resources.shared_mem_available, CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK, cu->api_device_number ) != CUDA_SUCCESS)
    goto cleanup_and_fail;
  
  /* Create a context on the correct device */
  step = "cuDeviceGet";
  cutilDrvSafeCallNoSync(cuDeviceGet(&cudaDevice, cu->api_device_number));
  step = "cuCtxCreate";
  if ((status = cuCtxCreate( &resources.cudaContext, CU_CTX_BLOCKING_SYNC | CU_CTX_SCHED_YIELD , cudaDevice )) != CUDA_SUCCESS)
    goto cleanup_and_fail;

  /* Get the pointer to the function inside the cubin */
  step = "cuModuleLoad";
  if ((status = cuModuleLoad(&resources.cudaModule, "worker_md5.cubin")) != CUDA_SUCCESS)
    goto cleanup_and_fail;
  step = "cuModuleGetFunction";
  if ((status = cuModuleGetFunction(&resources.cudaFunction, resources.cudaModule, "md5_search")) != CUDA_SUCCESS)
    goto cleanup_and_fail;

  /* create a stream for async operations */
  cutilDrvSafeCall( cuStreamCreate (&resources.cudastream, 0) );
  
  /* copy over the constants */
  init_constants(&resources.cudaModule);
  
  // allocate GPU memory for match signal
  step = "MemAlloc & set";
  cutilDrvSafeCall( cuMemAlloc( &resources.dev_success, 4*sizeof(uint)));
  cutilDrvSafeCall( cuMemsetD8( resources.dev_success, 0, 4*sizeof(uint)));
  cutilDrvSafeCall( cuMemAlloc( &resources.dev_target, 4*sizeof(uint)));
  cutilDrvSafeCall( cuMemcpyHtoD (resources.dev_target, &resources.raw_hash_to_search[0], 4*sizeof(uint)));
  
  return (void*) &(this->resources);

cleanup_and_fail:
	stringstream	stm("");
	stm << "cudainitFunc() failed at " << step << " with error " << status;
	workerAnnounce(this->id, DBG_ERROR, stm.str());
  cutilDrvSafeCall(cuCtxDetach(resources.cudaContext));
  return NULL;
}



/**
 * \brief GPU version of the MD5 brute-force algorithm, running to the next checkpoint
 * 
 * This function searches through one batch of source strings of size
 * #WORDS_PER_BATCH_GPU, i.e. it computes uninterrupted and massively parallel
 * to the next checkpoint.
 *
 * Stores the next checkpoint in Worker_md5::resources.
 *
 * \param [in]      cu  unused for GPU implementation of MD5 cracking
 * \param [in,out] workingresources  pointer to Worker_md5::resources, which
 *                      contains the last checkpoint
 * \param [out] workstatus  flag to tell the main loop in class Worker if there
 *                      is still work to be done after this function returns
 */
void Worker_md5::cudamainFunc(computing_unit_shortinfo* cu, void* workingresources, signed long* workstatus)
{
  stringstream	stm("");
  *workstatus = true;
  bool bWordsleft = false;
  md5_resources_t* resources = (md5_resources_t*)workingresources;

  execute_kernel(resources->currentWordNumber, resources);
  resources->currentWordNumber += WORDS_PER_BATCH_GPU;
	stm << " Currently working on cuda (" << resources->currentWordNumber << ")";
	WORKER_ANNOUNCE_CPUWORK(this->id, DBG_FINEST, stm.str());
  bWordsleft = (resources->currentWordNumber < pow(NUM_OF_CHARS, ORIGINAL_WORD_LENGTH));
  *workstatus = (bWordsleft);
	if (bWordsleft)
		*workstatus = resources->currentWordNumber;

  uint ret[4];
  cutilDrvSafeCall( cuMemcpyDtoH (ret, resources->dev_success, sizeof(uint)*4));
  
  if (ret[3]) {
    /* word has been found */
    *workstatus = false;
    char md5pool[sizeof(MD5POOL)] = MD5POOL;
    char currentword[ORIGINAL_WORD_LENGTH+1];
    currentword[ORIGINAL_WORD_LENGTH] = 0;
		unsigned long long *temp = (unsigned long long *) &ret[0];
    number2indexword(*temp, &currentword[0], this->id);
//    number2indexword(resources->currentWordNumber-1, &currentword[0]);
    for_each_word_position_i
      currentword[i] = md5pool[currentword[i]];

		stm << " Found solution " << string(currentword) << " (" << *temp << ")";
		WORKER_FOUND_SOLUTION(this->id, DBG_FINE, stm.str());
    resources->foundsolution = true;
  }
}



//! Frees the memory on the GPU
void Worker_md5::cudafreeFunc(computing_unit_shortinfo* cu, void* workingresources)
{
  md5_resources_t* resources = (md5_resources_t*)workingresources;
  cutilDrvSafeCall(cuMemFree(resources->dev_success));
  cutilDrvSafeCall(cuMemFree(resources->dev_target));
  
  cutilDrvSafeCall(cuModuleUnload(resources->cudaModule));
  cutilDrvSafeCall(cuStreamDestroy(resources->cudastream) );
  cutilDrvSafeCall(cuCtxDestroy(resources->cudaContext));
}

/**@}*/









/**
 * \brief Implementation multiplexer for the class Worker_md5 - implements the virtual Worker::getImplementationFor
 * 
 * This function takes the type of the computing unit which has been
 * allocated by the kernel to this worker and fetches the corresponding
 * functions (#initFunc, #mainFunc and #freeFunc) for this unit. It writes
 * these functions as function pointers to the #accelerated_functions_t
 * struct af, and sets it's initialized member to true.
 *
 * The type of the computing unit can be copied directly from the
 * corresponding member of the struct computing_unit_shortinfo.
 *
 * \param [in]  type  The type of computing unit, for which the 
 *                    implementations should be returned - encoded in the
 *                    same way as in the struct computing_unit_shortinfo
 * \param [out]   af  A pointer to an #accelerated_functions_t, in which
 *                    this function should write the function pointers to
 *                    the actual implementations
 */
void Worker_md5::getImplementationFor(unsigned int type, accelerated_functions_t *af)
{
	switch(type) {
		case CU_TYPE_CPU:
			af->init = static_cast<initFunc Worker::*> (&Worker_md5::cpuinitFunc);
			af->main = static_cast<mainFunc Worker::*> (&Worker_md5::cpumainFunc);
			af->free = static_cast<freeFunc Worker::*> (&Worker_md5::cpufreeFunc);

			af->initialized = true;
			break;
		case CU_TYPE_CUDA:
			af->init = static_cast<initFunc Worker::*> (&Worker_md5::cudainitFunc);
			af->main = static_cast<mainFunc Worker::*> (&Worker_md5::cudamainFunc);
			af->free = static_cast<freeFunc Worker::*> (&Worker_md5::cudafreeFunc);

			af->initialized = true;
			break;
		default:
			af->initialized = false;
	}
}








// Constants for MD5Transform routine.
#define S11 7
#define S12 12
#define S13 17
#define S14 22
#define S21 5
#define S22 9
#define S23 14
#define S24 20
#define S31 4
#define S32 11
#define S33 16
#define S34 23
#define S41 6
#define S42 10
#define S43 15
#define S44 21

///////////////////////////////////////////////

//! F is a basic MD5 function: x&y | ~x&z
inline Worker_md5::uint4 Worker_md5::F(Worker_md5::uint4 x, Worker_md5::uint4 y, Worker_md5::uint4 z) {
  return x&y | ~x&z;
}

//! G is a basic MD5 function: x&z | y&~z
inline Worker_md5::uint4 Worker_md5::G(Worker_md5::uint4 x, Worker_md5::uint4 y, Worker_md5::uint4 z) {
  return x&z | y&~z;
}

//! H is a basic MD5 function: x^y^z
inline Worker_md5::uint4 Worker_md5::H(Worker_md5::uint4 x, Worker_md5::uint4 y, Worker_md5::uint4 z) {
  return x^y^z;
}

//! I is a basic MD5 function: y ^ (x | ~z)
inline Worker_md5::uint4 Worker_md5::I(Worker_md5::uint4 x, Worker_md5::uint4 y, Worker_md5::uint4 z) {
  return y ^ (x | ~z);
}

//! rotate_left rotates x left n bits
inline Worker_md5::uint4 Worker_md5::rotate_left(Worker_md5::uint4 x, int n) {
  return (x << n) | (x >> (32-n));
}

//! FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation.
inline void Worker_md5::FF(Worker_md5::uint4 &a, Worker_md5::uint4 b, Worker_md5::uint4 c, Worker_md5::uint4 d, Worker_md5::uint4 x, Worker_md5::uint4 s, Worker_md5::uint4 ac) {
  a = rotate_left(a+ F(b,c,d) + x + ac, s) + b;
}

//! FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation.
inline void Worker_md5::GG(Worker_md5::uint4 &a, Worker_md5::uint4 b, Worker_md5::uint4 c, Worker_md5::uint4 d, Worker_md5::uint4 x, Worker_md5::uint4 s, Worker_md5::uint4 ac) {
  a = rotate_left(a + G(b,c,d) + x + ac, s) + b;
}

//! FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation.
inline void Worker_md5::HH(Worker_md5::uint4 &a, Worker_md5::uint4 b, Worker_md5::uint4 c, Worker_md5::uint4 d, Worker_md5::uint4 x, Worker_md5::uint4 s, Worker_md5::uint4 ac) {
  a = rotate_left(a + H(b,c,d) + x + ac, s) + b;
}

//! FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation.
inline void Worker_md5::II(Worker_md5::uint4 &a, Worker_md5::uint4 b, Worker_md5::uint4 c, Worker_md5::uint4 d, Worker_md5::uint4 x, Worker_md5::uint4 s, Worker_md5::uint4 ac) {
  a = rotate_left(a + I(b,c,d) + x + ac, s) + b;
}

//////////////////////////////////////////////

//! Default ctor, initialize with id and random word
Worker_md5::Worker_md5(int id) : Worker(id)
{
  resources.foundsolution = false;
  resources.currentWordNumber = 0;

  init();
  srand ( (unsigned int)time ( NULL ) + rand());
  int r;
  char md5pool[sizeof(MD5POOL)] = MD5POOL;
  char originalword[ORIGINAL_WORD_LENGTH+1];
  
	/* even id's get to the end of the md5pool, odd ones to the beginning */
	int modifier = (id + ((id % 2) * (-2) * id)) * (-1); //modifier is -id or id
	modifier = -1 * id;
	r = (NUM_OF_CHARS - 1 + modifier) % NUM_OF_CHARS;
	while (r < 0) r+= NUM_OF_CHARS;
	
  for_each_word_position_i
  {
		/* old randomization code */
    //r = rand() * ( 1.0 / ( RAND_MAX + 1.0 )) * (sizeof(MD5POOL)-1);
		//r = (NUM_OF_CHARS / 2);
    originalword[i] = md5pool[r];
		r = (r + 1) % NUM_OF_CHARS;
  }
  originalword[ORIGINAL_WORD_LENGTH] = 0;
  update(originalword, sizeof(originalword)-1);
  finalize();
  resources.hash_to_search = hexdigest();
  for (int i=0; i<4; ++i)
    resources.raw_hash_to_search[i] = state[i];
  
	stringstream	stm("");
	stm << "Searching for " << string(originalword) << " with hash " << resources.hash_to_search;
	WORKER_ANNOUNCE_TARGET(this->id, DBG_FINE, stm.str());
  
  workingresources = (void*)&resources;
	this->initialized = true;
}

//////////////////////////////////////////////

//! Shortcut ctor, compute MD5 for string and finalize it right away
Worker_md5::Worker_md5(const std::string &text, int id) : Worker(id)
{
  init();
  update(text.c_str(), text.length());
  finalize();
}

//////////////////////////////

//! Resets this instance to an empty state
void Worker_md5::init()
{
  finalized=false;

  count[0] = 0;
  count[1] = 0;

  // load magic initialization constants.
  state[0] = 0x67452301;
  state[1] = 0xefcdab89;
  state[2] = 0x98badcfe;
  state[3] = 0x10325476;
}

//////////////////////////////

//! Decodes input (unsigned char) into output (Worker_md5::uint4). Assumes len is a multiple of 4.
void Worker_md5::decode(Worker_md5::uint4 output[], const Worker_md5::uint1 input[], size_type len)
{
  for (unsigned int i = 0, j = 0; j < len; i++, j += 4)
    output[i] = ((Worker_md5::uint4)input[j]) | (((Worker_md5::uint4)input[j+1]) << 8) |
      (((Worker_md5::uint4)input[j+2]) << 16) | (((Worker_md5::uint4)input[j+3]) << 24);
}

//////////////////////////////

//! Encodes input (Worker_md5::uint4) into output (unsigned char). Assumes len is a multiple of 4.
void Worker_md5::encode(Worker_md5::uint1 output[], const Worker_md5::uint4 input[], size_type len)
{
  for (size_type i = 0, j = 0; j < len; i++, j += 4) {
    output[j] = input[i] & 0xff;
    output[j+1] = (input[i] >> 8) & 0xff;
    output[j+2] = (input[i] >> 16) & 0xff;
    output[j+3] = (input[i] >> 24) & 0xff;
  }
}

//////////////////////////////

//! Apply MD5 algo on a block
void Worker_md5::transform(const Worker_md5::uint1 block[blocksize])
{
  Worker_md5::uint4 a = state[0], b = state[1], c = state[2], d = state[3], x[16];
  decode (x, block, blocksize);
  
  /* Round 1 */
  FF (a, b, c, d, x[ 0], S11, 0xd76aa478); /* 1 */
  FF (d, a, b, c, x[ 1], S12, 0xe8c7b756); /* 2 */
  FF (c, d, a, b, x[ 2], S13, 0x242070db); /* 3 */
  FF (b, c, d, a, x[ 3], S14, 0xc1bdceee); /* 4 */
  FF (a, b, c, d, x[ 4], S11, 0xf57c0faf); /* 5 */
  FF (d, a, b, c, x[ 5], S12, 0x4787c62a); /* 6 */
  FF (c, d, a, b, x[ 6], S13, 0xa8304613); /* 7 */
  FF (b, c, d, a, x[ 7], S14, 0xfd469501); /* 8 */
  FF (a, b, c, d, x[ 8], S11, 0x698098d8); /* 9 */
  FF (d, a, b, c, x[ 9], S12, 0x8b44f7af); /* 10 */
  FF (c, d, a, b, x[10], S13, 0xffff5bb1); /* 11 */
  FF (b, c, d, a, x[11], S14, 0x895cd7be); /* 12 */
  FF (a, b, c, d, x[12], S11, 0x6b901122); /* 13 */
  FF (d, a, b, c, x[13], S12, 0xfd987193); /* 14 */
  FF (c, d, a, b, x[14], S13, 0xa679438e); /* 15 */
  FF (b, c, d, a, x[15], S14, 0x49b40821); /* 16 */

  /* Round 2 */
  GG (a, b, c, d, x[ 1], S21, 0xf61e2562); /* 17 */
  GG (d, a, b, c, x[ 6], S22, 0xc040b340); /* 18 */
  GG (c, d, a, b, x[11], S23, 0x265e5a51); /* 19 */
  GG (b, c, d, a, x[ 0], S24, 0xe9b6c7aa); /* 20 */
  GG (a, b, c, d, x[ 5], S21, 0xd62f105d); /* 21 */
  GG (d, a, b, c, x[10], S22,  0x2441453); /* 22 */
  GG (c, d, a, b, x[15], S23, 0xd8a1e681); /* 23 */
  GG (b, c, d, a, x[ 4], S24, 0xe7d3fbc8); /* 24 */
  GG (a, b, c, d, x[ 9], S21, 0x21e1cde6); /* 25 */
  GG (d, a, b, c, x[14], S22, 0xc33707d6); /* 26 */
  GG (c, d, a, b, x[ 3], S23, 0xf4d50d87); /* 27 */
  GG (b, c, d, a, x[ 8], S24, 0x455a14ed); /* 28 */
  GG (a, b, c, d, x[13], S21, 0xa9e3e905); /* 29 */
  GG (d, a, b, c, x[ 2], S22, 0xfcefa3f8); /* 30 */
  GG (c, d, a, b, x[ 7], S23, 0x676f02d9); /* 31 */
  GG (b, c, d, a, x[12], S24, 0x8d2a4c8a); /* 32 */

  /* Round 3 */
  HH (a, b, c, d, x[ 5], S31, 0xfffa3942); /* 33 */
  HH (d, a, b, c, x[ 8], S32, 0x8771f681); /* 34 */
  HH (c, d, a, b, x[11], S33, 0x6d9d6122); /* 35 */
  HH (b, c, d, a, x[14], S34, 0xfde5380c); /* 36 */
  HH (a, b, c, d, x[ 1], S31, 0xa4beea44); /* 37 */
  HH (d, a, b, c, x[ 4], S32, 0x4bdecfa9); /* 38 */
  HH (c, d, a, b, x[ 7], S33, 0xf6bb4b60); /* 39 */
  HH (b, c, d, a, x[10], S34, 0xbebfbc70); /* 40 */
  HH (a, b, c, d, x[13], S31, 0x289b7ec6); /* 41 */
  HH (d, a, b, c, x[ 0], S32, 0xeaa127fa); /* 42 */
  HH (c, d, a, b, x[ 3], S33, 0xd4ef3085); /* 43 */
  HH (b, c, d, a, x[ 6], S34,  0x4881d05); /* 44 */
  HH (a, b, c, d, x[ 9], S31, 0xd9d4d039); /* 45 */
  HH (d, a, b, c, x[12], S32, 0xe6db99e5); /* 46 */
  HH (c, d, a, b, x[15], S33, 0x1fa27cf8); /* 47 */
  HH (b, c, d, a, x[ 2], S34, 0xc4ac5665); /* 48 */

  /* Round 4 */
  II (a, b, c, d, x[ 0], S41, 0xf4292244); /* 49 */
  II (d, a, b, c, x[ 7], S42, 0x432aff97); /* 50 */
  II (c, d, a, b, x[14], S43, 0xab9423a7); /* 51 */
  II (b, c, d, a, x[ 5], S44, 0xfc93a039); /* 52 */
  II (a, b, c, d, x[12], S41, 0x655b59c3); /* 53 */
  II (d, a, b, c, x[ 3], S42, 0x8f0ccc92); /* 54 */
  II (c, d, a, b, x[10], S43, 0xffeff47d); /* 55 */
  II (b, c, d, a, x[ 1], S44, 0x85845dd1); /* 56 */
  II (a, b, c, d, x[ 8], S41, 0x6fa87e4f); /* 57 */
  II (d, a, b, c, x[15], S42, 0xfe2ce6e0); /* 58 */
  II (c, d, a, b, x[ 6], S43, 0xa3014314); /* 59 */
  II (b, c, d, a, x[13], S44, 0x4e0811a1); /* 60 */
  II (a, b, c, d, x[ 4], S41, 0xf7537e82); /* 61 */
  II (d, a, b, c, x[11], S42, 0xbd3af235); /* 62 */
  II (c, d, a, b, x[ 2], S43, 0x2ad7d2bb); /* 63 */
  II (b, c, d, a, x[ 9], S44, 0xeb86d391); /* 64 */

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;

  // Zeroize sensitive information.
  memset(x, 0, sizeof x);
}

//////////////////////////////

//! MD5 block update operation. Continues an MD5 message-digest operation, processing another message block
void Worker_md5::update(const unsigned char input[], size_type length)
{
  // compute number of bytes mod 64
  size_type index = count[0] / 8 % blocksize;

  // Update number of bits
  if ((count[0] += (length << 3)) < (length << 3))
    count[1]++;
  count[1] += (length >> 29);

  // number of bytes we need to fill in buffer
  size_type firstpart = 64 - index;

  size_type i;

  // transform as many times as possible.
  if (length >= firstpart)
  {
    // fill buffer first, transform
    memcpy(&buffer[index], input, firstpart);
    transform(buffer);

    // transform chunks of blocksize (64 bytes)
    for (i = firstpart; i + blocksize <= length; i += blocksize)
      transform(&input[i]);

    index = 0;
  }
  else
    i = 0;

  // buffer remaining input
  memcpy(&buffer[index], &input[i], length-i);
}

//////////////////////////////

//! Convenience version of updates with signed char
void Worker_md5::update(const char input[], size_type length)
{
  update((const unsigned char*)input, length);
}

//////////////////////////////

//! MD5 finalization. Ends an MD5 message-digest operation, writing the message digest and zeroizing the context.
Worker_md5& Worker_md5::finalize()
{
  static unsigned char padding[64] = {
    0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };

  if (!finalized) {
    // Save number of bits
    unsigned char bits[8];
    encode(bits, count, 8);

    // pad out to 56 mod 64.
    size_type index = count[0] / 8 % 64;
    size_type padLen = (index < 56) ? (56 - index) : (120 - index);
    update(padding, padLen);

    // Append length (before padding)
    update(bits, 8);
    
    // Store state in digest
    encode(digest, state, 16);

    // Zeroize sensitive information.
    memset(buffer, 0, sizeof buffer);
    memset(count, 0, sizeof count);

    finalized=true;
  }

  return *this;
}

//////////////////////////////

//! Return hex representation of digest as string
std::string Worker_md5::hexdigest() const
{
  if (!finalized)
    return "";

  char buf[33];
  for (int i=0; i<16; i++)
    sprintf(buf+i*2, "%02x", digest[i]);
  buf[32]=0;

  return std::string(buf);
}

//////////////////////////////

//! Return hex representation of digest into a stream
std::ostream& operator<<(std::ostream& out, Worker_md5 md5)
{
  return out << md5.hexdigest();
}

//////////////////////////////

/*
std::string md5(const std::string str)
{
    Worker_md5 md5 = Worker_md5(str);

    return md5.hexdigest();
}
*/
