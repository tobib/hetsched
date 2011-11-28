#!/bin/sh
#
# user space scheduler (USS)
# benchmarks
# BATCH
# is a predefined workload with 75 jobs

# syntax
# uss_benchmark_batch <selected testsetting> <file for jobturnaround times> <file for avgjobturnaround times>
# (if filenames are given, only the jobturnaround file will be empties, averages remains untouched)

# (0)
# variables and base parameters
#
CWD=`pwd`
WD=$CWD
TESTDIR=testapp
BENCHDIR=benchmark
LIBPATH="/home/dwelp/uss/library/"

TESTAPP1=
TESTAPP2=
TESTSETTING=
JOBTURNAROUNDSFILENAME=
AVGJOBTURNAROUNDSFILENAME=
TEMPFILE="uss_benchmark_batch.tmp"
JOBTURNAROUNDSFILENAMEUNSORTED="uss_benchmark_batch_unsorted.tmp"


TESTAPP1=testappc

TESTAPPPARAM1=5

TESTAPP1NOFJOBS=2



# (4)
# start testapp multiple times with parameter id and number to facorize
#
for (( i=0 ; i<${TESTAPP1NOFJOBS} ; i++ ))
do
	#execute command
	LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP1} ${i} ${TESTAPPPARAM1} &
done

exit 0
