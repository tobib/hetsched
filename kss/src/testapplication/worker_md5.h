/** \file worker_md5.h
 *
 *  \brief Header of the md5-hash-cracking worker
 *
 * This file defines the class Worker_md5, which derives from class Worker.
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

#ifndef WORKER_MD5_H
#define WORKER_MD5_H

#include "worker.h"

/** \def MD5POOL
 *  \brief Defines the alphabet used to generate source strings
 *
 * Class Worker_md5 uses brute force to find the (initially calculated) hash
 * of a string. This define is the alphabet from which the source string is
 * generated, and which the brute-force algorithm walks to find the hash.
 */
//               0         1         2         3         4         5         6 
//               01234567890123456789012345678901234567890123456789012345678901
//#define MD5POOL "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
#define MD5POOL "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
//#define MD5POOL "abcdefghijklmnopqrstuvwxyz0123456789"
/** \def ORIGINAL_WORD_LENGTH
 *  \brief Defines the length of the source strings
 *
 * Defines the length of the generated source string and the generated
 * brute-force strings.
 */
#define ORIGINAL_WORD_LENGTH 6
/** \def WORDS_PER_BATCH
 *  \brief Defines the distance between two checkpoints for the CPU implementation
 *
 * Defines, how many generated brute-force strings two adjacent checkpoints of
 * this worker are apart on a CPU.
 */
#define WORDS_PER_BATCH 500
/** \def WORDS_PER_BATCH_GPU
 *  \brief Defines the distance between two checkpoints for the GPU implementation
 *
 * Defines, how many generated brute-force strings two adjacent checkpoints of
 * this worker are apart on a GPU.
 */
//#define WORDS_PER_BATCH_GPU 1000000000
#define WORDS_PER_BATCH_GPU 100000000

//! Abbreviation for the number of characters in the currently used alphabet
#define NUM_OF_CHARS (sizeof(MD5POOL) - 1)

/* Includes */
#include <string>
#include <iostream>
#include <cuda.h>

/** \brief The Worker::workingresources struct for the md5 worker
 * 
 * This struct contains all information that has to be persistent between two
 * cycles of the main loop, where all accelerator resources have to be freed
 * in between., or two cycles of the inner loop. The contained data can be
 * divided in two categories:
 * - Checkpoint encoding
 * - Cuda context references
 */
typedef struct md5_resources {
	std::string hash_to_search;           //!< Checkpoint: Target hash that has to be found, as string
	unsigned int raw_hash_to_search[4];   //!< Checkpoint: Target hash that has to be found, as four unsigned integers
	
	unsigned long long currentWordNumber; //!< Checkpoint: Last generated source string that has been checked
	bool foundsolution;                   //!< Checkpoint: Flag, indicating if the target string has been found
	
	int shared_mem_available; //!< Cuda: Available shared memory of the current accelerator
	CUdeviceptr dev_success;  //!< Cuda: On-GPU pointer to a flag indicating if one cuda thread found the solution
  CUdeviceptr dev_target;   //!< Cuda: On-GPU pointer to the target hash
	CUstream cudastream;      //!< Cuda: Currently used cuda stream
	CUcontext cudaContext;    //!< Cuda: Currently used cuda context
	CUfunction cudaFunction;  //!< Cuda: The loaded function of the md5 cuda kernel
  CUmodule cudaModule;      //!< Cuda: The loaded module of the md5 cuda kernel
} md5_resources_t;

/** \brief A small class for calculating MD5 hashes of strings or byte arrays
 *         it is not meant to be fast or secure
 *
 * usage:
 *   -# feed it blocks of uchars with update()
 *   -# finalize()
 *   -# get hexdigest() string
 *      or
 *      Worker_md5(std::string).hexdigest()
 *
 * Assumes that char is 8 bit and int is 32 bit.
 */
class Worker_md5 : public Worker
{
public:
  typedef unsigned int  size_type; //!< must be 32bit
  typedef unsigned char uint1;     //!<  8bit integer
  typedef unsigned int  uint4;     //!< 32bit integer

  Worker_md5(int id); //!< Default ctor, initialize with id and random word
  Worker_md5(const std::string& text, int id); //!< Shortcut ctor, compute MD5 for string and finalize it right away
  
  //! MD5 block update operation. Continues an MD5 message-digest operation, processing another message block
  void update(const unsigned char *buf, size_type length);
  //! Convenience version of updates with signed char
  void update(const char *buf, size_type length);
  //! MD5 finalization. Ends an MD5 message-digest operation, writing the message digest and zeroizing the context.
  Worker_md5& finalize();
  //! Return hex representation of digest as string
  std::string hexdigest() const;
  //! Return hex representation of digest into a stream
  friend std::ostream& operator<<(std::ostream&, Worker_md5 md5);

  //! The Worker::workingresources struct for the md5 worker
  md5_resources_t resources;

  //! Implementation multiplexer for the class Worker_md5 - implements the virtual Worker::getImplementationFor
  void* getImplementationFor(unsigned int type, accelerated_functions_t *af);

  //! Meta information generator for the class Worker_md5 - implements the virtual Worker::workerMetaInfo
	void workerMetaInfo(struct meta_info *mi);
	
  //! Dummy function to "initialize" a CPU - does nothing
  void* cpuinitFunc(computing_unit_shortinfo* cu);
  //! CPU version of the MD5 brute-force algorithm, running to the next checkpoint
  void cpumainFunc(computing_unit_shortinfo* cu, void* workingresources, signed long* workstatus);
  //! Dummy function to "free" a CPU - does nothing
  void cpufreeFunc(computing_unit_shortinfo* cu, void* workingresources);
  
  //! Allocates GPU memory and copies the last checkpoint to it
  void* cudainitFunc(computing_unit_shortinfo* cu);
  //! GPU version of the MD5 brute-force algorithm, running to the next checkpoint
  void cudamainFunc(computing_unit_shortinfo* cu, void* workingresources, signed long* workstatus);
  //! Frees the memory on the GPU
  void cudafreeFunc(computing_unit_shortinfo* cu, void* workingresources);

private:
  void init(); //!< Resets this instance to an empty state
  enum {blocksize = 64 /**< \brief Blocksize for streams, VC6 won't eat a const static int here */};

  //! Apply MD5 algo on a block
  void transform(const uint1 block[blocksize]);
  //! Decodes input (unsigned char) into output (Worker_md5::uint4). Assumes len is a multiple of 4.
  static void decode(uint4 output[], const uint1 input[], size_type len);
  //! Encodes input (Worker_md5::uint4) into output (unsigned char). Assumes len is a multiple of 4.
  static void encode(uint1 output[], const uint4 input[], size_type len);

  bool  finalized;         //!< Flag indicating if the digest has been written using #finalize
  uint1 buffer[blocksize]; //!< Bytes that didn't fit in last 64 byte chunk
  uint4 count[2];          //!< 64bit counter for number of bits (lo, hi)
  uint4 state[4];          //!< Digest so far
  uint1 digest[16];        //!< The result

  // low level logic operations
  static inline uint4 F(uint4 x, uint4 y, uint4 z); //!< F is a basic MD5 function: x&y | ~x&z
  static inline uint4 G(uint4 x, uint4 y, uint4 z); //!< G is a basic MD5 function: x&z | y&~z
  static inline uint4 H(uint4 x, uint4 y, uint4 z); //!< H is a basic MD5 function: x^y^z
  static inline uint4 I(uint4 x, uint4 y, uint4 z); //!< I is a basic MD5 function: y ^ (x | ~z)
  static inline uint4 rotate_left(uint4 x, int n);  //!< rotate_left rotates x left n bits
  static inline void FF(uint4 &a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac); //!< FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation.
  static inline void GG(uint4 &a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac); //!< FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation.
  static inline void HH(uint4 &a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac); //!< FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation.
  static inline void II(uint4 &a, uint4 b, uint4 c, uint4 d, uint4 x, uint4 s, uint4 ac); //!< FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4. Rotation is separate from addition to prevent recomputation.
};

#endif /* WORKER_MD5_H */
