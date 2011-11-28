VERSION	= 4.4.5
GCC		= gcc
GPP		= g++
NVCC	= nvcc

COMMON_DIR = ../common

CFLAGS 	= -Wall -g -fPIC
LDFLAGS = -lrt -lpthread -fno-exceptions

LIBRARY_OBJ = uss_library.o uss_fifo.o uss_tools.o

all: library

library: $(LIBRARY_OBJ)
	$(GPP) -g -shared $(LDFLAGS) -o libuss.so $(LIBRARY_OBJ)

uss_library.o: uss_library.cpp uss.h 
	$(GPP) $(CFLAGS) $(LDFLAGS) -c uss_library.cpp -o $@
	
uss_tools.o: $(COMMON_DIR)/uss_tools.cpp $(COMMON_DIR)/uss_tools.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c $(COMMON_DIR)/uss_tools.cpp -o $@

uss_rtsig.o: $(COMMON_DIR)/uss_rtsig.cpp $(COMMON_DIR)/uss_rtsig.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c $(COMMON_DIR)/uss_rtsig.cpp -o $@
	
uss_fifo.o: $(COMMON_DIR)/uss_fifo.cpp $(COMMON_DIR)/uss_fifo.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c $(COMMON_DIR)/uss_fifo.cpp -o $@	

clean:
	rm *.so; \
	rm *.o

.PHONY: clean all library