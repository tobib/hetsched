#ifndef WORKER_PRIME_H
#define WORKER_PRIME_H

#include "worker.h"

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

class Worker_prime;

#include <cuda.h>
typedef struct prime_resources {
	unsigned long long numberToTest;
	unsigned long long remainder;
	unsigned long long currentDivisor;
	unsigned long long currentSquareRoot;
	
	unsigned int nextIndex;
	unsigned long long primes[FACTORS_TO_FIND];
	unsigned long long exponents[FACTORS_TO_FIND];
	bool foundsolution;
} prime_resources_t;

typedef struct prime_cuda_resources {
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
} prime_cuda_resources_t;


/*
 * A small class to calculate the prime factorization of a number
 * Only finds the first FACTORS_TO_FIND factors with exponents
 */
class Worker_prime : public Worker
{
public:

  Worker_prime(int id);

  prime_resources_t resources;
	prime_cuda_resources_t cudares;

  /* implementation multiplexer - must be overriden */
  void* getImplementationFor(unsigned int type, accelerated_functions_t *af);

	/* meta_info generator for the algorithm - must be overriden */
	void workerMetaInfo(struct meta_info *mi);
	
  /* allocate cu memory and copy resources to cu */
  void* cpuinitFunc(computing_unit_shortinfo* cu);
  /* run algorithm from checkpoint to checkpoint */
  void cpumainFunc(computing_unit_shortinfo* cu, void* workingresources, signed long* workstatus);
  /* copy resources back to main memory and free cu memory */
  void cpufreeFunc(computing_unit_shortinfo* cu, void* workingresources);
  
  /* allocate cu memory and copy resources to cu */
  void* cudainitFunc(computing_unit_shortinfo* cu);
  /* run algorithm from checkpoint to checkpoint */
  void cudamainFunc(computing_unit_shortinfo* cu, void* workingresources, signed long* workstatus);
  /* copy resources back to main memory and free cu memory */
  void cudafreeFunc(computing_unit_shortinfo* cu, void* workingresources);

private:
  void init();
	void testCandidate();
	void checkResults(signed long* workstatus);
};

#endif /* WORKER_MD5_H */
