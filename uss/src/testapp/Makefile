LIB_DIR = ./library
DAE_DIR = ./daemon
TEST_DIR = ./testapp

VERSION	= 4.4.5
GCC		= gcc
GPP		= g++
NVCC	= nvcc

USS_LIBDIR = ../library
BENCH_DIR = ../benchmark

CUDA_SDK_PATH  = /home/daniel/cudaexamples
CUDA_SDK_PATH2 = /opt/NVIDIA_GPU_Computing_SDK/CUDALibraries
INCLUDES	= -I. -I$(CUDA_SDK_PATH)/common/inc  -I$(CUDA_SDK_PATH)/C/common/inc -I$(CUDA_SDK_PATH2)/common/inc  -I$(CUDA_SDK_PATH2)/C/common/inc 

NFLAGS	= $(INCLUDES) -lcuda -DUNIX -g --compiler-options -fno-strict-aliasing -m64
CFLAGS 	= -Wall -g3 -D_REENTRANT3 -D_REENTRANT3 -D_REENTRANT -pthread -fbounds-check -O0
LDFLAGS = -lrt
SMVERSIONFLAGS    := -arch sm_20

TESTAPPC_OBJ = testapp.c 
TESTAPPCMULTI_OBJ = testappmultithreaded.c 

all: testappc testappcmulti

kernelprime: prime.cu
	$(NVCC) $(NFLAGS) $(SMVERSIONFLAGS) -cubin prime.cu
	
kernelmd5: md5.cu
	$(NVCC) $(NFLAGS) $(SMVERSIONFLAGS) -cubin md5.cu

testappprime: testappprime.cu $(BENCH_DIR)/dwatch.cpp $(BENCH_DIR)/dwatch.h kernelprime
	$(NVCC) $(NFLAGS) testappprime.cu $(BENCH_DIR)/dwatch.cpp -o testappprime -L$(CURDIR)/$(USS_LIBDIR) -luss
	
testappmd5: testappmd5.cu $(BENCH_DIR)/dwatch.cpp $(BENCH_DIR)/dwatch.h kernelmd5
	$(NVCC) $(NFLAGS) testappmd5.cu $(BENCH_DIR)/dwatch.cpp -o testappmd5 -L$(CURDIR)/$(USS_LIBDIR) -luss

testappcu: testapp.cu $(BENCH_DIR)/dwatch.cpp $(BENCH_DIR)/dwatch.h
	$(NVCC) $(NFLAGS) testapp.cu $(BENCH_DIR)/dwatch.cpp -o testappcu -L$(CURDIR)/$(USS_LIBDIR) -luss

testappc: testapp.c $(BENCH_DIR)/dwatch.cpp $(BENCH_DIR)/dwatch.h
	$(GPP) $(CFLAGS) $(LDFLAGS) testapp.c $(BENCH_DIR)/dwatch.cpp -o testappc -Wl,-rpath,$(CURDIR)/$(USS_LIBDIR) -L$(CURDIR)/$(USS_LIBDIR) -luss
	
testappcmulti: testappmultithreaded.c $(BENCH_DIR)/dwatch.cpp $(BENCH_DIR)/dwatch.h
	$(GPP) $(CFLAGS) $(LDFLAGS) testappmultithreaded.c $(BENCH_DIR)/dwatch.cpp -o testappcmulti -Wl,-rpath,$(CURDIR)/$(USS_LIBDIR) -L/$(CURDIR)/$(USS_LIBDIR) -luss
	
clean:
	rm testappc; \
	rm testappcu; \
	rm testappcmulti; \
	rm testappprime; \
	rm testappmd5; \
	rm prime.cubin; \
	rm md5.cubin
	
.PHONY: clean all