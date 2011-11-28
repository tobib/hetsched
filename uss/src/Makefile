LIB_DIR = ./library
DAE_DIR = ./daemon
TEST_DIR = ./testapp
BENCH_DIR = ./benchmark

all:	library daemon testapp

library:
	make -C $(LIB_DIR) all

daemon: 
	make -C $(DAE_DIR) all

testapp:
	make -C $(TEST_DIR) all
	
testappprime:
	make -C $(TEST_DIR) testappprime
	
testappmd5:
	make -C $(TEST_DIR) testappmd5
	
install:
	cp $(LIB_DIR)/uss.h /usr/include/; \
	cp $(LIB_DIR)/libuss.so /lib/; \
	cp $(LIB_DIR)/libuss.so /lib64/; \
	cp $(LIB_DIR)/libuss.so /usr/lib/; \
	cp $(LIB_DIR)/libuss.so /usr/lib64/; \

ticks:
	make -C $(BENCH_DIR) all
	
remakelibrary:
	make -C $(LIB_DIR) clean; \
	make -C $(LIB_DIR) all

remakedaemon:
	make -C $(DAE_DIR) clean; \
	make -C $(DAE_DIR) all

clean:
	make -C $(LIB_DIR) clean; \
	make -C $(DAE_DIR) clean; \
	make -C $(TEST_DIR) clean; \

.PHONY: all library daemon testapp install remakelibrary remakedaemon clean
