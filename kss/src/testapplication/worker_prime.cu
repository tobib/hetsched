#include <stdio.h>
#include "cutil.h"
#include <cutil_inline.h>
#include "worker_prime.h"

#define count_t ullong 

int __device__ isPrime(const ullong currentDivisor)
{
	ullong root = ((ullong)sqrt((double) currentDivisor))+1;
	
	for (ullong i=2; i< root; i++){
		/* in this case we are useless */
		if ((currentDivisor % i) == 0) {
			return 0;
		}
	}
	return 1;
}


void __device__ testCandidate(const ullong currentDivisor, ullong *currentSquareRoot, ullong *remainder, ullong *primes, ullong *exponents, uint *nextIndex)
{
	ullong oldRemainder = *remainder;
	ullong newRemainder = oldRemainder;
	ullong temp = oldRemainder;
	uint exponent = 0;
	
	while ((oldRemainder % currentDivisor) == 0) {
		newRemainder = oldRemainder / currentDivisor;
		
		/* write the calculated remainder - if this fails repeat the previous iteration with the new remainder */
		temp = atomicCAS(remainder, oldRemainder, newRemainder);
		if (temp != oldRemainder) {
			/* repeat previous iteration with the externally calculated new remainder */
			oldRemainder = temp;
		} else {
			/* our remainder got through, continue */
			exponent += 1;
			oldRemainder = newRemainder;
		}
	}
	
	if (exponent > 0) {
		/* get a unique index */
		uint ourIndex = 0;
		uint oldIndex;
get_an_index:
		oldIndex = *nextIndex;
		ourIndex = atomicCAS(nextIndex, oldIndex, oldIndex+1);
		
		/* if somebody intervened - try again */
		if (ourIndex != oldIndex)
			goto get_an_index;
		
		/* we have an index: write out result */
		primes[ourIndex] = currentDivisor;
		exponents[ourIndex] = exponent;
		
		/* write a root which is consistent with the new remainder */
write_new_root:
		oldRemainder = *remainder;
		temp = ceil(sqrt((double)(oldRemainder)));
		atomicExch(currentSquareRoot, temp);
		if (oldRemainder != *remainder)
			goto write_new_root;
	}
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

// The kernel (this is the entrypoint of GPU code)
// calls the calculation routine, compares to target and flags if a match is found
extern "C"
__global__ void prime_factor(ullong currentDivisor, uint number_of_threads, uint iterations, uint *succ, ullong *primes, ullong *exponents, uint *nextIndex, ullong *currentSquareRoot, ullong *remainder)
{
	count_t linidx = (count_t)(gridDim.x*blockIdx.y + blockIdx.x)*blockDim.x + threadIdx.x; // assuming blockDim.y = 1 and threadIdx.y = 0, always
	if(linidx >= number_of_threads) { return; }
	linidx += (count_t)currentDivisor;
	
	uint have_to_avoid_multiples = 0;
	
	/* currentDivisor * 2 > currentDivisor + (number_of_threads * iterations)
	 * means that even the smallest possible multiple is outside this kernels scope.
	 * We can then skip the prime check because no thread processes the multiple of another.
	 */
	if (currentDivisor <= ((ullong) number_of_threads * (ullong)iterations))
		have_to_avoid_multiples = 1;

	for (int i=0 ; i < iterations && linidx < *currentSquareRoot && *remainder > 1 && *nextIndex < FACTORS_TO_FIND; ++i) {
		if (!have_to_avoid_multiples || isPrime(linidx))
			testCandidate(linidx, currentSquareRoot, remainder, primes, exponents, nextIndex);

		__syncthreads();
		linidx += (count_t)number_of_threads;
	}
	
	/* correct last loop */
	linidx -= (count_t)number_of_threads;
	
	__syncthreads();
	if (*nextIndex >= FACTORS_TO_FIND || *remainder <= 1 || linidx > *currentSquareRoot)
		*succ = 1;
}
