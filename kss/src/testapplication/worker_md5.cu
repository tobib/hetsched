/* http://majuric.org/software/cudamd5/ */

// CUDA MD5 hash calculation implementation (A: mjuric@ias.edu).
//
// A very useful link: http://people.eku.edu/styere/Encrypt/JS-MD5.html
//

#define RSA_KERNEL md5_v2

#include <stdio.h>
#include "cutil.h"
#include <cutil_inline.h>
#include "worker_md5.h"

#if ORIGINAL_WORD_LENGTH > 16*4
	#error "Word length too long for CUDA implementation"
#endif

#define count_t ullong 

typedef unsigned int uint;
typedef unsigned long long ullong;

//
// On-device variable declarations
//

extern __shared__ uint memory[];	// on-chip shared memory
__constant__ uint k[64], rconst[16];	// constants (in fast on-chip constant cache)
__constant__ uint steps[ORIGINAL_WORD_LENGTH];		// calculation helper to convert a number to a word using the MD5POOL

//
// MD5 routines (straight from Wikipedia's MD5 pseudocode description)
//

__device__ inline uint leftrotate (uint x, uint c)
{
	return (x << c) | (x >> (32-c));
}

__device__ inline uint r(const uint i)
{
	return rconst[(i / 16) * 4 + i % 4];
}

// Accessor for w[16] array. Naively, this would just be w[i]; however, this
// choice leads to worst-case-scenario access pattern wrt. shared memory
// bank conflicts, as the same indices in different threads fall into the
// same bank (as the words are 16 uints long). The packing below causes the
// same indices in different threads of a warp to map to different banks. In
// testing this gave a ~40% speedup.
//
// PS: An alternative solution would be to make the w array 17 uints long
// (thus wasting a little shared memory)
//
__device__ inline uint &getw(uint *w, const int i)
{
	return w[(i+threadIdx.x) % 16];
}

__device__ inline uint getw(const uint *w, const int i)	// const- version
{
	return w[(i+threadIdx.x) % 16];
}


__device__ inline uint getk(const int i)
{
	return k[i];	// Note: this is as fast as possible (measured)
}

__device__ void step(const uint i, const uint f, const uint g, uint &a, uint &b, uint &c, uint &d, const uint *w)
{
	uint temp = d;
	d = c;
	c = b;
	b = b + leftrotate((a + f + getk(i) + getw(w, g)), r(i));
	a = temp;
}

__device__ void inline md5(const uint *w, uint &a, uint &b, uint &c, uint &d)
{
	const uint a0 = 0x67452301;
	const uint b0 = 0xEFCDAB89;
	const uint c0 = 0x98BADCFE;
	const uint d0 = 0x10325476;

	//Initialize hash value for this chunk:
	a = a0;
	b = b0;
	c = c0;
	d = d0;

	uint f, g, i = 0;
	for(; i != 16; i++)
	{
		f = (b & c) | ((~b) & d);
		g = i;
		step(i, f, g, a, b, c, d, w);
	}

	for(; i != 32; i++)
	{
		f = (d & b) | ((~d) & c);
		g = (5*i + 1) % 16;
		step(i, f, g, a, b, c, d, w);
	}

	for(; i != 48; i++)
	{
		f = b ^ c ^ d;
		g = (3*i + 5) % 16;
		step(i, f, g, a, b, c, d, w);
	}

	for(; i != 64; i++)
	{
		f = c ^ (b | (~d));
		g = (7*i) % 16;
		step(i, f, g, a, b, c, d, w);
	}

	a += a0;
	b += b0;
	c += c0;
	d += d0;
}

/* 
 * prepare a 56-byte (maximum) wide md5 message by appending the 64-bit length
 * it will be padded with 0 and will contain the messaged 'packed' into an uint array
 *
 * NOTE: This function will fail badly if it is called with a number >= steps[0] * NUM_OF_CHARS
 *
 * word is assumed to be a w[16] array and is thus accessed via getw()
 */
__device__ void number2paddedword (count_t number, uint *word)
{
	int srciter=0;
	int dstiter=0;
	char md5pool[sizeof(MD5POOL)] = MD5POOL;
  char curChar;
  int shiftoffset = 0; /* current offset to shift the next char into the uint */
  uint nextArrayUint = 0;

  /*
	 * Special case: Length of words is 0 or 1
	 * These cases can be determined at compile time and can therefore
	 * be optimized away by the compiler
	 */
	if (ORIGINAL_WORD_LENGTH < 1)
		return;
	
	/* loop through the source word */
  for (srciter = 0; srciter < ORIGINAL_WORD_LENGTH; ++srciter) {
    /* Decide if we have to encode a specific char or just md5pool[0] */
		if (number >= steps[srciter] || srciter == ORIGINAL_WORD_LENGTH-1) {
      uint temp = (uint)((count_t)number / (count_t)steps[srciter]);
      curChar = md5pool[temp];
      number -= (count_t)((count_t)temp * (count_t)steps[srciter]);
		} else 
      curChar = md5pool[0];
    
    /* Encode current char for the destination word */
    nextArrayUint |= (curChar << shiftoffset);
    shiftoffset += 8;
    
    /* if we have packed 4 chars in the uint we have to write it to word */
    if (shiftoffset > 24) {
      getw(word, dstiter++) = nextArrayUint;
      shiftoffset = 0;
      nextArrayUint = 0;
    }
	}
  
  /* Append a single 1 bit after the message as needed by md5 */
  /* When arriving here shiftoffset is <= 24, so we can safely append one more char and encode it */
  nextArrayUint |= (0x80 << shiftoffset);
  getw(word, dstiter++) = nextArrayUint;
	
  /* zero the words padding */
  for (; dstiter < 16; ++dstiter)
  	getw(word, dstiter) = (uint)0;
	
	__syncthreads();
  
  /* write the message length in bits */
	getw(word, 14) = ORIGINAL_WORD_LENGTH * 8;
}

//////////////////////////////////////////////////////////////////////////////
/////////////       Ron Rivest's MD5 C Implementation       //////////////////
//////////////////////////////////////////////////////////////////////////////

/*
 **********************************************************************
 ** Copyright (C) 1990, RSA Data Security, Inc. All rights reserved. **
 **                                                                  **
 ** License to copy and use this software is granted provided that   **
 ** it is identified as the "RSA Data Security, Inc. MD5 Message     **
 ** Digest Algorithm" in all material mentioning or referencing this **
 ** software or this function.                                       **
 **                                                                  **
 ** License is also granted to make and use derivative works         **
 ** provided that such works are identified as "derived from the RSA **
 ** Data Security, Inc. MD5 Message Digest Algorithm" in all         **
 ** material mentioning or referencing the derived work.             **
 **                                                                  **
 ** RSA Data Security, Inc. makes no representations concerning      **
 ** either the merchantability of this software or the suitability   **
 ** of this software for any particular purpose.  It is provided "as **
 ** is" without express or implied warranty of any kind.             **
 **                                                                  **
 ** These notices must be retained in any copies of any part of this **
 ** documentation and/or software.                                   **
 **********************************************************************
 */


/* F, G and H are basic MD5 functions: selection, majority, parity */
#define F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | (~z))) 

/* ROTATE_LEFT rotates x left n bits */
#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/* FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4 */
/* Rotation is separate from addition to prevent recomputation */
#define FF(a, b, c, d, x, s, ac) \
  {(a) += F ((b), (c), (d)) + (x) + (uint)(ac); \
   (a) = ROTATE_LEFT ((a), (s)); \
   (a) += (b); \
  }
#define GG(a, b, c, d, x, s, ac) \
  {(a) += G ((b), (c), (d)) + (x) + (uint)(ac); \
   (a) = ROTATE_LEFT ((a), (s)); \
   (a) += (b); \
  }
#define HH(a, b, c, d, x, s, ac) \
  {(a) += H ((b), (c), (d)) + (x) + (uint)(ac); \
   (a) = ROTATE_LEFT ((a), (s)); \
   (a) += (b); \
  }
#define II(a, b, c, d, x, s, ac) \
  {(a) += I ((b), (c), (d)) + (x) + (uint)(ac); \
   (a) = ROTATE_LEFT ((a), (s)); \
   (a) += (b); \
  }


/* Basic MD5 step. Transform buf based on in.
 */
void inline __device__ md5_v2(const uint *in, uint &a, uint &b, uint &c, uint &d)
{
	const uint a0 = 0x67452301;
	const uint b0 = 0xEFCDAB89;
	const uint c0 = 0x98BADCFE;
	const uint d0 = 0x10325476;

	//Initialize hash value for this chunk:
	a = a0;
	b = b0;
	c = c0;
	d = d0;

  /* Round 1 */
#define S11 7
#define S12 12
#define S13 17
#define S14 22
  FF ( a, b, c, d, getw(in,  0), S11, 3614090360); /* 1 */
  FF ( d, a, b, c, getw(in,  1), S12, 3905402710); /* 2 */
  FF ( c, d, a, b, getw(in,  2), S13,  606105819); /* 3 */
  FF ( b, c, d, a, getw(in,  3), S14, 3250441966); /* 4 */
  FF ( a, b, c, d, getw(in,  4), S11, 4118548399); /* 5 */
  FF ( d, a, b, c, getw(in,  5), S12, 1200080426); /* 6 */
  FF ( c, d, a, b, getw(in,  6), S13, 2821735955); /* 7 */
  FF ( b, c, d, a, getw(in,  7), S14, 4249261313); /* 8 */
  FF ( a, b, c, d, getw(in,  8), S11, 1770035416); /* 9 */
  FF ( d, a, b, c, getw(in,  9), S12, 2336552879); /* 10 */
  FF ( c, d, a, b, getw(in, 10), S13, 4294925233); /* 11 */
  FF ( b, c, d, a, getw(in, 11), S14, 2304563134); /* 12 */
  FF ( a, b, c, d, getw(in, 12), S11, 1804603682); /* 13 */
  FF ( d, a, b, c, getw(in, 13), S12, 4254626195); /* 14 */
  FF ( c, d, a, b, getw(in, 14), S13, 2792965006); /* 15 */
  FF ( b, c, d, a, getw(in, 15), S14, 1236535329); /* 16 */
 
  /* Round 2 */
#define S21 5
#define S22 9
#define S23 14
#define S24 20
  GG ( a, b, c, d, getw(in,  1), S21, 4129170786); /* 17 */
  GG ( d, a, b, c, getw(in,  6), S22, 3225465664); /* 18 */
  GG ( c, d, a, b, getw(in, 11), S23,  643717713); /* 19 */
  GG ( b, c, d, a, getw(in,  0), S24, 3921069994); /* 20 */
  GG ( a, b, c, d, getw(in,  5), S21, 3593408605); /* 21 */
  GG ( d, a, b, c, getw(in, 10), S22,   38016083); /* 22 */
  GG ( c, d, a, b, getw(in, 15), S23, 3634488961); /* 23 */
  GG ( b, c, d, a, getw(in,  4), S24, 3889429448); /* 24 */
  GG ( a, b, c, d, getw(in,  9), S21,  568446438); /* 25 */
  GG ( d, a, b, c, getw(in, 14), S22, 3275163606); /* 26 */
  GG ( c, d, a, b, getw(in,  3), S23, 4107603335); /* 27 */
  GG ( b, c, d, a, getw(in,  8), S24, 1163531501); /* 28 */
  GG ( a, b, c, d, getw(in, 13), S21, 2850285829); /* 29 */
  GG ( d, a, b, c, getw(in,  2), S22, 4243563512); /* 30 */
  GG ( c, d, a, b, getw(in,  7), S23, 1735328473); /* 31 */
  GG ( b, c, d, a, getw(in, 12), S24, 2368359562); /* 32 */

  /* Round 3 */
#define S31 4
#define S32 11
#define S33 16
#define S34 23
  HH ( a, b, c, d, getw(in,  5), S31, 4294588738); /* 33 */
  HH ( d, a, b, c, getw(in,  8), S32, 2272392833); /* 34 */
  HH ( c, d, a, b, getw(in, 11), S33, 1839030562); /* 35 */
  HH ( b, c, d, a, getw(in, 14), S34, 4259657740); /* 36 */
  HH ( a, b, c, d, getw(in,  1), S31, 2763975236); /* 37 */
  HH ( d, a, b, c, getw(in,  4), S32, 1272893353); /* 38 */
  HH ( c, d, a, b, getw(in,  7), S33, 4139469664); /* 39 */
  HH ( b, c, d, a, getw(in, 10), S34, 3200236656); /* 40 */
  HH ( a, b, c, d, getw(in, 13), S31,  681279174); /* 41 */
  HH ( d, a, b, c, getw(in,  0), S32, 3936430074); /* 42 */
  HH ( c, d, a, b, getw(in,  3), S33, 3572445317); /* 43 */
  HH ( b, c, d, a, getw(in,  6), S34,   76029189); /* 44 */
  HH ( a, b, c, d, getw(in,  9), S31, 3654602809); /* 45 */
  HH ( d, a, b, c, getw(in, 12), S32, 3873151461); /* 46 */
  HH ( c, d, a, b, getw(in, 15), S33,  530742520); /* 47 */
  HH ( b, c, d, a, getw(in,  2), S34, 3299628645); /* 48 */

  /* Round 4 */
#define S41 6
#define S42 10
#define S43 15
#define S44 21
  II ( a, b, c, d, getw(in,  0), S41, 4096336452); /* 49 */
  II ( d, a, b, c, getw(in,  7), S42, 1126891415); /* 50 */
  II ( c, d, a, b, getw(in, 14), S43, 2878612391); /* 51 */
  II ( b, c, d, a, getw(in,  5), S44, 4237533241); /* 52 */
  II ( a, b, c, d, getw(in, 12), S41, 1700485571); /* 53 */
  II ( d, a, b, c, getw(in,  3), S42, 2399980690); /* 54 */
  II ( c, d, a, b, getw(in, 10), S43, 4293915773); /* 55 */
  II ( b, c, d, a, getw(in,  1), S44, 2240044497); /* 56 */
  II ( a, b, c, d, getw(in,  8), S41, 1873313359); /* 57 */
  II ( d, a, b, c, getw(in, 15), S42, 4264355552); /* 58 */
  II ( c, d, a, b, getw(in,  6), S43, 2734768916); /* 59 */
  II ( b, c, d, a, getw(in, 13), S44, 1309151649); /* 60 */
  II ( a, b, c, d, getw(in,  4), S41, 4149444226); /* 61 */
  II ( d, a, b, c, getw(in, 11), S42, 3174756917); /* 62 */
  II ( c, d, a, b, getw(in,  2), S43,  718787259); /* 63 */
  II ( b, c, d, a, getw(in,  9), S44, 3951481745); /* 64 */

	a += a0;
	b += b0;
	c += c0;
	d += d0;

}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

// The kernel (this is the entrypoint of GPU code)
// Calculates the 64-byte word from MD5POOL to be hashed in shared memory,
// calls the calculation routine, compares to target and flags if a match is found
extern "C"
__global__ void md5_search(ullong starting_number, uint words_per_call, uint iterations, ullong max_number, uint *succ, uint *target)
{
	count_t linidx = (count_t)(gridDim.x*blockIdx.y + blockIdx.x)*blockDim.x + threadIdx.x; // assuming blockDim.y = 1 and threadIdx.y = 0, always
	if(linidx >= words_per_call) { return; }
	linidx += (count_t)starting_number;

	/* get the shared memory region for our calculations */
	uint *word = &memory[0] + threadIdx.x*16;
	
	for (int i=0 ; i < iterations && linidx < max_number; ++i) {
		// calculate the dictionary word for this thread
		number2paddedword(linidx, word);

		// compute MD5 hash
		uint a, b, c, d;

		RSA_KERNEL(word, a, b, c, d);

		if(a == target[0] && b == target[1] && c == target[2] && d == target[3])
		{
			count_t *temp = (count_t *) &succ[0];
			*temp = linidx;
			succ[3] = 1;
			break;
		}
		__syncthreads();
		if (succ[3] != 0)
			break;
		
		linidx += (count_t)words_per_call;
	}
  /*
  succ[0] = target[0];
  succ[1] = target[1];
  succ[2] = target[2];
  succ[3] = target[3];
  */
}
