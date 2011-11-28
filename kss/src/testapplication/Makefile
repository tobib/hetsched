# Testapplikation makefile
BIN               := testapp

USEDRVAPI         := 1

.DEFAULT : $(BIN)
.PHONY : clean

CXX               := g++ 
NVCC              := nvcc 
RM                := rm -f
CP                := cp -f

CUDA_INSTALL_PATH := /usr/local/cuda
CUDA_SDK_PATH     := /opt/NVIDIA_GPU_Computing_SDK
LIBS              := -L$(CUDA_INSTALL_PATH)/lib -L$(CUDA_INSTALL_PATH)/lib64 -L$(CUDA_SDK_PATH)/lib
INCLUDES          += -I. -I$(CUDA_INSTALL_PATH)/include -I$(CUDA_SDK_PATH)/common/inc  -I$(CUDA_SDK_PATH)/C/common/inc 
INCLUDES          += -I/usr/src/linux-headers-2.6.32-24-hwsched/include -I/usr/src/linux-headers-2.6.32-24/include -I/usr/include

LDFLAGS           := -lpthread -lrt -lm
# static linking, we will statically link against CUDA and CUDART
ifeq ($(USEDRVAPI),1)
 LDFLAGS += -lcuda -lcudart 
else
 LDFLAGS += -lcudart 
endif
CXXFLAGS          := $(INCLUDES) -pthread -g3 -D__DEBUG__ -D_REENTRANT -fbounds-check -O0
NVCCFLAGS         := $(INCLUDES) -DUNIX -g --compiler-options -fno-strict-aliasing -m64
SMVERSIONFLAGS    := -arch sm_20

CPP_SOURCES       := $(wildcard *.cpp)
CU_SOURCES        := $(wildcard *.cu)
HEADERS           := $(wildcard *.h)
CPP_OBJS          := $(patsubst %.cpp, %.o, $(CPP_SOURCES))
CU_OBJS           := $(patsubst %.cu, %.cu_o, $(CU_SOURCES))
CUBIN_OBJS        := $(patsubst %.cu, %.cubin, $(CU_SOURCES))

.PHONY: all all-before all-after clean clean-custom

all: all-before $(BIN) all-after

ifeq ($(USEDRVAPI),1)
$(BIN): $(CPP_OBJS) $(CUBIN_OBJS)
	$(CXX) -o $(BIN) $(CPP_OBJS) $(LDFLAGS) $(INCLUDES) $(LIBS) 
else
$(BIN): $(CPP_OBJS) $(CU_OBJS)
	$(CXX) -o $(BIN) $(CU_OBJS) $(CPP_OBJS) $(LDFLAGS) $(INCLUDES) $(LIBS) 
endif

clean: clean-custom
	${RM} $(CPP_OBJS) $(CU_OBJS) $(CUBIN_OBJS) $(BIN)

# Rest
%.cu_o : %.cu
	$(NVCC) $(NVCCFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $< 

%.cubin : %.cu
	$(NVCC) $(NVCCFLAGS) $(SMVERSIONFLAGS) -o $@ -cubin $<
