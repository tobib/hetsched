VERSION	= 4.4.5
GCC		= gcc
GPP		= g++
NVCC	= nvcc

COMMON_DIR = ../common

CUDA_SDK_PATH  = /home/daniel/cudaexamples
CUDA_SDK_PATH2 = /opt/NVIDIA_GPU_Computing_SDK/CUDALibraries
INCLUDES	= -I. -I$(CUDA_SDK_PATH)/common/inc  -I$(CUDA_SDK_PATH)/C/common/inc -I$(CUDA_SDK_PATH2)/common/inc  -I$(CUDA_SDK_PATH2)/C/common/inc 

NFLAGS	= $(INCLUDES) -lcuda -g -G
CFLAGS 	= -Wall -g -pthread
LDFLAGS = -lrt
SMVERSIONFLAGS    := -arch sm_20

DAEMON_OBJ	= uss_daemon.o uss_comm_controller.o uss_registration_controller.o uss_device_controller.o uss_scheduler.o uss_tools.o uss_fifo.o

all: daemon

daemon: $(DAEMON_OBJ)
	$(GPP) $(CFLAGS) $(LDFLAGS) -o daemon $(DAEMON_OBJ)

uss_tools.o: $(COMMON_DIR)/uss_tools.cpp $(COMMON_DIR)/uss_tools.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c $(COMMON_DIR)/uss_tools.cpp -o $@

uss_rtsig.o: $(COMMON_DIR)/uss_rtsig.cpp $(COMMON_DIR)/uss_rtsig.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c $(COMMON_DIR)/uss_rtsig.cpp -o $@

uss_fifo.o: $(COMMON_DIR)/uss_fifo.cpp $(COMMON_DIR)/uss_fifo.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c $(COMMON_DIR)/uss_fifo.cpp -o $@	

uss_daemon.o: uss_daemon.cpp uss_daemon.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c uss_daemon.cpp -o $@

uss_comm_controller.o: uss_comm_controller.cpp uss_comm_controller.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c uss_comm_controller.cpp -o $@
	
uss_registration_controller.o: uss_registration_controller.cpp uss_registration_controller.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c uss_registration_controller.cpp -o $@
	
uss_device_controller.o: uss_device_controller.cpp uss_device_controller.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c uss_device_controller.cpp -o $@
	
uss_scheduler.o: uss_scheduler.cpp uss_scheduler.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c uss_scheduler.cpp -o $@
	
dwatch.o: $(BENCH_DIR)/dwatch.cpp $(BENCH_DIR)/dwatch.h
	$(GPP) -Wall -g -c $(BENCH_DIR)/dwatch.cpp -o $@
	
clean:
	rm -f *.o; \
	rm daemon

.PHONY: clean