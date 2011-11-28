/*
 * this is a testapplication filling using the
 * library calls offered by uss_library
 */
 
 /*
  * CURRENT EXAMPLE
  * MD5 cracking
  *
  * start with: testappmd5 <benchmark-id> <target-value>
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
//               0         1         2         3         4         5         6 
//               01234567890123456789012345678901234567890123456789012345678901
//#define MD5POOL "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
//#define MD5POOL "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define MD5POOL "abcdefghijklmnopqrstuvwxyz0123456789"
#define ORIGINAL_WORD_LENGTH 6
#define WORDS_PER_BATCH 500
//#define WORDS_PER_BATCH_GPU 1000000000
//#define WORDS_PER_BATCH_GPU 100000000
#define WORDS_PER_BATCH_GPU 1000000

#define NUM_OF_CHARS (sizeof(MD5POOL) - 1)

using namespace std;

typedef unsigned int uint;
typedef unsigned long long ullong;

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <string.h>
#include <iostream>
#include <math.h>
#include <cutil.h>
#include <cutil_inline.h>

#define for_each_word_position_i for (int i=0; i<ORIGINAL_WORD_LENGTH; ++i)

#define uint4_type unsigned int
typedef unsigned int size_type; // must be 32bit
typedef unsigned char md5uint1; //  8bit
typedef uint4_type    md5uint4;  // 32bit
  
//////////////////////////////////////////////
//											//
// header like additional definitions		//
//											//
//////////////////////////////////////////////


//define your personal meta_data and meta_checkpoint
struct meta_checkpoint
{
	int is_finished;
	signed long *workstatus;
	
	std::string hash_to_search;
	uint4_type raw_hash_to_search[4];
	
	unsigned long long currentWordNumber;
	bool foundsolution;
	
	int shared_mem_available;
	CUdeviceptr dev_success;
	CUdeviceptr dev_target;
	CUstream cudastream;
	CUcontext cudaContext;
	CUfunction cudaFunction;
	CUmodule cudaModule;
};
  
struct meta_data
{
	int empty;
};

enum {blocksize = 64}; // VC6 won't eat a const static int here
bool finalized;
md5uint1 buffer[blocksize]; // bytes that didn't fit in last 64 byte chunk
md5uint4 count[2];   // 64bit counter for number of bits (lo, hi)
md5uint4 state[4];   // digest so far
md5uint1 digest[16]; // the result

void init();
void transform(const md5uint1 block[blocksize]);
static void decode(md5uint4 output[], const md5uint1 input[], size_type len);
static void encode(md5uint1 output[], const md5uint4 input[], size_type len);
void update(const unsigned char *buf, size_type length);
void update(const char *buf, size_type length);
void finalize();
string hexdigest();	 
//////////////////////////////////////////////
//											//
// helpers for all implementations			//
//											//
//////////////////////////////////////////////
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

// F, G, H and I are basic MD5 functions.
inline md5uint4 F(md5uint4 x, md5uint4 y, md5uint4 z) {
  return x&y | ~x&z;
}

inline md5uint4 G(md5uint4 x, md5uint4 y, md5uint4 z) {
  return x&z | y&~z;
}

inline md5uint4 H(md5uint4 x, md5uint4 y, md5uint4 z) {
  return x^y^z;
}

inline md5uint4 I(md5uint4 x, md5uint4 y, md5uint4 z) {
  return y ^ (x | ~z);
}

// rotate_left rotates x left n bits.
inline md5uint4 rotate_left(md5uint4 x, int n) {
  return (x << n) | (x >> (32-n));
}

// FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
// Rotation is separate from addition to prevent recomputation.
inline void FF(md5uint4 &a, md5uint4 b, md5uint4 c, md5uint4 d, md5uint4 x, md5uint4 s, md5uint4 ac) {
  a = rotate_left(a+ F(b,c,d) + x + ac, s) + b;
}

inline void GG(md5uint4 &a, md5uint4 b, md5uint4 c, md5uint4 d, md5uint4 x, md5uint4 s, md5uint4 ac) {
  a = rotate_left(a + G(b,c,d) + x + ac, s) + b;
}

inline void HH(md5uint4 &a, md5uint4 b, md5uint4 c, md5uint4 d, md5uint4 x, md5uint4 s, md5uint4 ac) {
  a = rotate_left(a + H(b,c,d) + x + ac, s) + b;
}

inline void II(md5uint4 &a, md5uint4 b, md5uint4 c, md5uint4 d, md5uint4 x, md5uint4 s, md5uint4 ac) {
  a = rotate_left(a + I(b,c,d) + x + ac, s) + b;
}

 
//////////////////////////////////////////////
//											//
// CPU implementation						//
//											//
//////////////////////////////////////////////
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
  /* if even the first bit overflowed we have reached the end of our range... */
  return !overflow;
}

/* 
 * NOTE: This function will fail badly if it is called with a number >= steps[0] * NUM_OF_CHARS
 */
void number2indexword (unsigned long long number, char *word, int id)
{
	uint x=0;
	unsigned long long temp, step;
	step = (unsigned long long) pow(NUM_OF_CHARS, (ORIGINAL_WORD_LENGTH));
	if (number > step) {
		printf("Fatal error! Number %lld is too large", number);
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

	char md5pool[sizeof(MD5POOL)] = MD5POOL;
	char currentBruteforceIndex[ORIGINAL_WORD_LENGTH+1];
	char currentword[ORIGINAL_WORD_LENGTH+1];
	currentword[ORIGINAL_WORD_LENGTH] = 0;
	currentBruteforceIndex[ORIGINAL_WORD_LENGTH] = 0;
	std::string currentmd5;
	bool bWordsleft = false;

	*(resources->workstatus) = true;
	/* initialize the current index word to the correct number */
	number2indexword(resources->currentWordNumber, &currentBruteforceIndex[0], /*Daniel: replaced unused this->id*/ 0);

	for (int words = 0; words < WORDS_PER_BATCH && *(resources->workstatus); ++words)
	{
	init();
	for (int i=0; i<ORIGINAL_WORD_LENGTH; ++i)
	{
	  currentword[i] = md5pool[currentBruteforceIndex[i]];
	}
	update(currentword, sizeof(currentword)-1);
	finalize();
	currentmd5 = hexdigest();
	bWordsleft = nextIndexWord(&(currentBruteforceIndex[0]), ORIGINAL_WORD_LENGTH);
	resources->currentWordNumber++;
	*(resources->workstatus) = (bWordsleft && resources->hash_to_search != currentmd5);
	}
	if (resources->currentWordNumber % 5000000 == 0)
	{
		#if(TESTDEBUG == 1)
		printf("Currently working on cpu with ( %lld ) \n", resources->currentWordNumber);
		#endif
	}
	if (!*(resources->workstatus) && bWordsleft)
	{
		#if(TESTDEBUG == 1)
		printf("Found solution -string- ( %lld )\n", resources->currentWordNumber);
		#endif
		resources->foundsolution = true;
		resources->is_finished = 1;
	} else if (bWordsleft)
		*(resources->workstatus) = resources->currentWordNumber;
		
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
//
// MD5 magic numbers. These will be loaded into on-device "constant" memory
//
static const uint k_cpu[64] =
{
  0xd76aa478, 	0xe8c7b756,	0x242070db,	0xc1bdceee,
  0xf57c0faf,	0x4787c62a, 	0xa8304613,	0xfd469501,
  0x698098d8,	0x8b44f7af,	0xffff5bb1,	0x895cd7be,
  0x6b901122, 	0xfd987193, 	0xa679438e,	0x49b40821,

  0xf61e2562,	0xc040b340, 	0x265e5a51, 	0xe9b6c7aa,
  0xd62f105d,	0x2441453,	0xd8a1e681,	0xe7d3fbc8,
  0x21e1cde6,	0xc33707d6, 	0xf4d50d87, 	0x455a14ed,
  0xa9e3e905,	0xfcefa3f8, 	0x676f02d9, 	0x8d2a4c8a,

  0xfffa3942,	0x8771f681, 	0x6d9d6122, 	0xfde5380c,
  0xa4beea44, 	0x4bdecfa9, 	0xf6bb4b60, 	0xbebfbc70,
  0x289b7ec6, 	0xeaa127fa, 	0xd4ef3085,	0x4881d05,
  0xd9d4d039, 	0xe6db99e5, 	0x1fa27cf8, 	0xc4ac5665,

  0xf4292244, 	0x432aff97, 	0xab9423a7, 	0xfc93a039,
  0x655b59c3, 	0x8f0ccc92, 	0xffeff47d, 	0x85845dd1,
  0x6fa87e4f, 	0xfe2ce6e0, 	0xa3014314, 	0x4e0811a1,
  0xf7537e82, 	0xbd3af235, 	0x2ad7d2bb, 	0xeb86d391,
};

static const uint rconst_cpu[16] =
{
  7, 12, 17, 22,   5,  9, 14, 20,   4, 11, 16, 23,   6, 10, 15, 21
};


/* Function to copy over the constants to the gpu */
void init_constants(CUmodule *cudaModule)
{
  CUdeviceptr dptr;
  /*Daniel changed unsigned int bytes to size_t bytes (maybe CUDA4.0 wants this?)*/
  size_t bytes;

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

/* A helper to export the kernel call to C++ code not compiled with nvcc */
static void execute_kernel(unsigned long long starting_number, struct meta_checkpoint *resources)
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
  //cutilDrvSafeCall( cuStreamSynchronize(resources->cudastream) );
	cutilDrvSafeCall( cuCtxSynchronize() );
}



int myalgo_cuda_init(void *md_void, void *mcp_void, int device_id)
{
#if(TESTDEBUG == 1)
	printf("myalgo_CUDA_init()\n");
#endif
	//struct meta_data *md = (struct meta_data*) md_void;
	struct meta_checkpoint *resources = (struct meta_checkpoint*) mcp_void;

  // Initialize the driver API
  CUdevice cudaDevice;
  CUresult status;
  cuInit(0);
  const char *step;
  
  /* Get the available number of shared memory per block */
  step = "cuDeviceGetAttribute";
  /*Daniel: replaces cu->api_device_number with device_id (this is my notation of which GPU to run on)*/
  if (cuDeviceGetAttribute( &resources->shared_mem_available, CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK, device_id ) != CUDA_SUCCESS)
    goto cleanup_and_fail;
  
  /* Create a context on the correct device */
  step = "cuDeviceGet";
  cutilDrvSafeCallNoSync(cuDeviceGet(&cudaDevice, device_id));
  step = "cuCtxCreate";
  if ((status = cuCtxCreate( &resources->cudaContext, /* ommitting bof error: CU_CTX_BLOCKING_SYNC | CU_CTX_SCHED_YIELD*/ 0, cudaDevice )) != CUDA_SUCCESS)
    goto cleanup_and_fail;

  /* Get the pointer to the function inside the cubin */
  step = "cuModuleLoad";
  if ((status = cuModuleLoad(&resources->cudaModule, "./testapp/md5.cubin")) != CUDA_SUCCESS)
    goto cleanup_and_fail;
  step = "cuModuleGetFunction";
  if ((status = cuModuleGetFunction(&resources->cudaFunction, resources->cudaModule, "md5_search")) != CUDA_SUCCESS)
    goto cleanup_and_fail;

  /* create a stream for async operations */
  cutilDrvSafeCall( cuStreamCreate (&resources->cudastream, 0) );
  
  /* copy over the constants */
  init_constants(&resources->cudaModule);
  
  // allocate GPU memory for match signal
  step = "MemAlloc & set";
  cutilDrvSafeCall( cuMemAlloc( &resources->dev_success, 4*sizeof(uint)));
  cutilDrvSafeCall( cuMemsetD8( resources->dev_success, 0, 4*sizeof(uint)));
  cutilDrvSafeCall( cuMemAlloc( &resources->dev_target, 4*sizeof(uint)));
  cutilDrvSafeCall( cuMemcpyHtoD (resources->dev_target, &resources->raw_hash_to_search[0], 4*sizeof(uint)));
  
	return 0;

cleanup_and_fail:
	printf("cudainitFunc() failed at %s with error %i\n", step, (int)status);
  cutilDrvSafeCall(cuCtxDetach(resources->cudaContext));

	return -1;
}
 
int myalgo_cuda_main(void *md_void, void *mcp_void, int device_id)
{
	#if(TESTDEBUG == 1)
	printf("myalgo_CUDA_main()\n");
	#endif
	struct meta_checkpoint *resources = (struct meta_checkpoint*) mcp_void;

	*(resources->workstatus) = true;
	bool bWordsleft = false;

	execute_kernel(resources->currentWordNumber, resources);
	resources->currentWordNumber += WORDS_PER_BATCH_GPU;
	#if(TESTDEBUG == 1)
	printf("Currently working on cuda ( %lld )\n", resources->currentWordNumber);
	#endif
	bWordsleft = (resources->currentWordNumber < pow(NUM_OF_CHARS, ORIGINAL_WORD_LENGTH));
	*(resources->workstatus) = (bWordsleft);
	if (bWordsleft)
		*(resources->workstatus) = resources->currentWordNumber;

	uint ret[4];
	cutilDrvSafeCall( cuMemcpyDtoH (ret, resources->dev_success, sizeof(uint)*4));

	if (ret[3]) {
	/* word has been found */
	*(resources->workstatus) = false;
	char md5pool[sizeof(MD5POOL)] = MD5POOL;
	char currentword[ORIGINAL_WORD_LENGTH+1];
	currentword[ORIGINAL_WORD_LENGTH] = 0;
		unsigned long long *temp = (unsigned long long *) &ret[0];
	number2indexword(*temp, &currentword[0], /*Daniel: replaced unused this->id*/ 0);
	//    number2indexword(resources->currentWordNumber-1, &currentword[0]);
	for_each_word_position_i
	  currentword[i] = md5pool[currentword[i]];

	#if(TESTDEBUG == 1)
	printf("Found solution -string- ( &llu )", *temp);
	#endif
	resources->foundsolution = true;
	resources->is_finished = 1;
	}

	return 0;
}
 
int myalgo_cuda_free(void *md_void, void *mcp_void, int device_id)
{
#if(TESTDEBUG == 1)
	printf("myalgo_CUDA_free()\n");
#endif
	struct meta_checkpoint *resources = (struct meta_checkpoint*) mcp_void;

	cutilDrvSafeCall(cuMemFree(resources->dev_success));
	cutilDrvSafeCall(cuMemFree(resources->dev_target));

	cutilDrvSafeCall(cuModuleUnload(resources->cudaModule));
	cutilDrvSafeCall(cuStreamDestroy(resources->cudastream) );
	cutilDrvSafeCall(cuCtxDestroy(resources->cudaContext));

	return 0;
}

//////////////////////////////////////////////
//											//
// methods for preparating data for main() 	//
//											//
//////////////////////////////////////////////
void init()
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

// decodes input (unsigned char) into output (Worker_md5::md5uint4). Assumes len is a multiple of 4.
void decode(md5uint4 output[], const md5uint1 input[], size_type len)
{
  for (unsigned int i = 0, j = 0; j < len; i++, j += 4)
    output[i] = ((md5uint4)input[j]) | (((md5uint4)input[j+1]) << 8) |
      (((md5uint4)input[j+2]) << 16) | (((md5uint4)input[j+3]) << 24);
}

//////////////////////////////

// encodes input (Worker_md5::md5uint4) into output (unsigned char). Assumes len is
// a multiple of 4.
void encode(md5uint1 output[], const md5uint4 input[], size_type len)
{
  for (size_type i = 0, j = 0; j < len; i++, j += 4) {
    output[j] = input[i] & 0xff;
    output[j+1] = (input[i] >> 8) & 0xff;
    output[j+2] = (input[i] >> 16) & 0xff;
    output[j+3] = (input[i] >> 24) & 0xff;
  }
}

// apply MD5 algo on a block
void transform(const md5uint1 block[blocksize])
{
  md5uint4 a = state[0], b = state[1], c = state[2], d = state[3], x[16];
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

// MD5 block update operation. Continues an MD5 message-digest
// operation, processing another message block
void update(const unsigned char input[], size_type length)
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

// for convenience provide a verson with signed char
void update(const char input[], size_type length)
{
  update((const unsigned char*)input, length);
}

// MD5 finalization. Ends an MD5 message-digest operation, writing the
// the message digest and zeroizing the context.
void finalize()
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
}

// return hex representation of digest as string
string hexdigest()
{
  if (!finalized)
    return "";

  char buf[33];
  for (int i=0; i<16; i++)
    sprintf(buf+i*2, "%02x", digest[i]);
  buf[32]=0;

  return std::string(buf);
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
	int id = 1;
	int modifier; //determines length
	if(argc == 1)
	{
		//default mode
		printf("<<< test application for uss_library>>>\n");
		//md5 just needs id and this is 1
	}
	else if(argc == 2)
	{
		//tobias mode (modifier is set below)
		id = atoi(argv[1]);
		//modifier = id;
		//md5 just needs id because it produces its keys depending on id
	}
	else if(argc == 3)
	{
		//tobias mode (modifier is set below)
		id = atoi(argv[1]);
		//modifier = atoi(argv[2]);
		//md5 just needs id because it produces its keys depending on id
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
	resources.currentWordNumber = 0;

	init();
	int r;
	char md5pool[sizeof(MD5POOL)] = MD5POOL;
	char originalword[ORIGINAL_WORD_LENGTH+1];

	/* even id's get to the end of the md5pool, odd ones to the beginning */
	modifier = (id + ((id % 2) * (-2) * id)) * (-1); //modifier is -id or id
	//modifier = -1 * id;
	r = (NUM_OF_CHARS - 1 + modifier) % NUM_OF_CHARS;
	while (r < 0) r+= NUM_OF_CHARS;

	for_each_word_position_i
	{
		originalword[i] = md5pool[r];
		r = (r + 1) % NUM_OF_CHARS;
	}
	
	originalword[ORIGINAL_WORD_LENGTH] = 0;
	update(originalword, sizeof(originalword)-1);
	finalize();
	resources.hash_to_search = hexdigest();
	for (int i=0; i<4; ++i)
		resources.raw_hash_to_search[i] = state[i];

	/*this variable by tobias is design to hold the status
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
	//is empty here
	
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
