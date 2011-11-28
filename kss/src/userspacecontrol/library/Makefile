# Testapplikation makefile
SO               :=libhwaccel_uc.so

.SUFFIXES : .cpp .o
.DEFAULT : $(SO)
.PHONY : clean

CPP  = g++
LIBS = 
INCLUDES = -I/usr/src/linux-headers-2.6.32-24-hwsched/include -I/usr/src/linux-headers-2.6.32-24/include -I/usr/include
CXXFLAGS = $(INCLUDES) -g3 -fPIC -D__DEBUG__ -D_REENTRANT -fbounds-check
RM = rm -f
CP = cp -f

SOURCES         := $(wildcard *.cpp)
HEADERS         := $(wildcard *.h)
OBJS            := $(patsubst %.cpp, %.o, $(SOURCES))


.PHONY: all all-before all-after clean clean-custom

all: all-before $(SO) all-after

$(SO): $(OBJS) $(HEADERS)
	$(CPP) -shared -Wl,-soname,$(SO) -o $(SO) $(OBJS)

clean: clean-custom
	${RM} $(OBJS) $(SO)

# Rest
%.o: %.cpp
	$(CPP) $(CXXFLAGS) -c $< -o $@

