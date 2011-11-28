VERSION	= 4.4.5
GCC		= gcc
GPP		= g++
NVCC	= nvcc

COMMON_DIR = ../common

CFLAGS 	= -Wall -g
LDFLAGS = -lrt -fno-exceptions

TIME_OBJ = ticks.o

all: ticks avgticks

avgticks: ticks
	./ticks
	
ticks: $(TIME_OBJ)
	$(GPP) $(CFLAGS) $(LDFLAGS) -o ticks $(TIME_OBJ)

ticks.o: ticks.cpp cycle.h
	$(GPP) $(CFLAGS) $(LDFLAGS) -c ticks.cpp -o $@	

clean:
	rm tmpfile; \
	rm tempfile

.PHONY: clean all avgticks