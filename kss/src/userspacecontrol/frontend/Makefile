# Testapplikation makefile
BIN               := accelerator-ctl

.SUFFIXES : .cpp .o
.DEFAULT : $(BIN)
.PHONY : clean

CUDA_INSTALL_PATH := /usr/local/cuda
CUDA_SDK_PATH     := /opt/NVIDIA_GPU_Computing_SDK
NVCC              := $(CUDA_INSTALL_PATH)/bin/nvcc
CPP               := g++
LIBPATHS          := -L"." -L../library -L"$(CUDA_INSTALL_PATH)/lib" -L"$(CUDA_INSTALL_PATH)/lib64"
LIBRARIES         := -lhwaccel_uc -lcudart
#LIBRARIES         := -lhwaccel_uc -lm -lrt -lcudart 
INCLUDES          := -I/usr/src/linux-headers-2.6.32-24-hwsched/include -I/usr/src/linux-headers-2.6.32-24/include -I/usr/include -I../library -I$(CUDA_INSTALL_PATH)/include
CXXFLAGS          := $(INCLUDES) -g3 -D__DEBUG__ -D_REENTRANT -fbounds-check

RM = rm -f
CP = cp -f

SOURCES         := $(wildcard *.cpp)
HEADERS         := $(wildcard *.h)
OBJS            := $(patsubst %.cpp, %.o, $(SOURCES))


.PHONY: all all-before all-after clean clean-custom

all: all-before $(BIN) all-after

$(BIN): $(OBJS) $(HEADERS)
	$(CPP) -o $(BIN) $(OBJS) $(LIBPATHS) $(LIBRARIES)

clean: clean-custom
	${RM} $(OBJS) $(BIN)

# Rest
%.o: %.cpp
	$(CPP) $(CXXFLAGS) -c $< -o $@

