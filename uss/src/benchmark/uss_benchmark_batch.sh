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

# (0)
# parse input parameters
#
if [ $# -eq 0 ] ; then
	echo "wrong parameters (type --help for manual)"
fi

if [ "$1" == "--help" ] ; then
	echo "uss_benchmark_batch.sh <selected testsetting>"
	echo "<file for jobturnaround times> <file for avgjobturnaround times>"
	echo "select testsetting: 1=prime+md5 2=testappc"
fi

if [ $# -eq 1 ] ; then
	#echo "correct parameters without redirecting outputs to file"
	TESTSETTING=${1}
elif [ $# -eq 3 ] ; then
	#echo "correct parameters redirecting to files"
	TESTSETTING=${1}
	JOBTURNAROUNDSFILENAME=${2}
	AVGJOBTURNAROUNDSFILENAME=${3}	
else
	echo "wrong parameters now aborting"
	exit 1
fi

# (1)
# refresh some filenames
#
if [ -f $WD/$BENCHDIR/$JOBTURNAROUNDSFILENAME ] ; then
	rm $WD/$BENCHDIR/$JOBTURNAROUNDSFILENAME
fi
touch $WD/$BENCHDIR/$JOBTURNAROUNDSFILENAME

if [ -f $WD/$BENCHDIR/$JOBTURNAROUNDSFILENAMEUNSORTED ] ; then
	rm $WD/$BENCHDIR/$JOBTURNAROUNDSFILENAMEUNSORTED
fi
touch $WD/$BENCHDIR/$JOBTURNAROUNDSFILENAMEUNSORTED


# (2)
# select test scenario
#
if [ ${TESTSETTING} -eq 1 ] ; then
	TESTAPP1=testappprime
	TESTAPP2=testappmd5
	TESTAPPPARAM1=0
	TESTAPPPARAM2=0
	#ALT=99488307847707802
	TESTAPP1NOFJOBS=50
	TESTAPP2NOFJOBS=25
elif [ ${TESTSETTING} -eq 2 ] ; then
	TESTAPP1=testappc
	TESTAPP2=testappc
	TESTAPPPARAM1=50
	TESTAPPPARAM2=10
	TESTAPP1NOFJOBS=10
	TESTAPP2NOFJOBS=10
elif [ ${TESTSETTING} -eq 3 ] ; then
	TESTAPP1=testappc
	TESTAPP2=testappc
	TESTAPPPARAM1=50
	TESTAPPPARAM2=50
	TESTAPP1NOFJOBS=1
	TESTAPP2NOFJOBS=1
fi

TOBMODE=1
NOFALL=`expr ${TESTAPP1NOFJOBS} + ${TESTAPP2NOFJOBS}`


# (3)
# remember complete batch time
# 
START=`date +%s%N`


# (4)
# start testapp multiple times with parameter id and number to facorize (0 means BASENUMBER+id)
#


if [ ${TOBMODE} -eq 1 ] ; then
if [ $# -eq 3 ] ; then
        for (( i=0 ; i<${NOFALL} ; i++ ))
                do
                SELECTOR=`expr $NOFALL % 3`

                if [ ${SELECTOR} -eq 0 ] ; then
                LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP1} ${i} ${TESTAPPPARAM1} >> ${WD}/$BENCHDIR/${JOBTURNAROUNDSFILENAMEUNSORTED} &
                fi

                if [ ${SELECTOR} -eq 1 ] ; then
                LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP2} ${i} ${TESTAPPPARAM2} >> ${WD}/$BENCHDIR/${JOBTURNAROUNDSFILENAMEUNSORTED} &
                fi

                if [ ${SELECTOR} -eq 2 ] ; then
                LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP2} ${i} ${TESTAPPPARAM2} >> ${WD}/$BENCHDIR/${JOBTURNAROUNDSFILENAMEUNSORTED} &
                fi

        done
fi
fi


if [ ${TOBMODE} -eq 0 ] ; then
if [ $# -eq 3 ] ; then
	for (( i=0 ; i<${TESTAPP1NOFJOBS} ; i++ ))
	do
	#execute command
	LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP1} ${i} ${TESTAPPPARAM1} >> ${WD}/$BENCHDIR/${JOBTURNAROUNDSFILENAMEUNSORTED} &
	done

	for (( i=0 ; i<${TESTAPP2NOFJOBS} ; i++ ))
	do
	#execute command
	LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP2} ${i} ${TESTAPPPARAM2} >> ${WD}/$BENCHDIR/${JOBTURNAROUNDSFILENAMEUNSORTED} &
	done
else
	for (( i=0 ; i<${TESTAPP1NOFJOBS} ; i++ ))
	do
	#execute command
	LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP1} ${i} ${TESTAPPPARAM1} &
	done

	for (( i=0 ; i<${TESTAPP2NOFJOBS} ; i++ ))
	do
	#execute command
	LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP2} ${i} ${TESTAPPPARAM2} &
	done
fi
fi

# (3)
# wait for the last one to finish
#
wait


# (4)
# rememver finish batch time
#
STOP=`date +%s%N`


# (5)
# calculate average job turnaround time and append to file AVGJOBTURNAROUNDSFILENAME
#
if [ $# -eq 3 ] ; then
	#clean tempfile
	if [ -f $WD/$BENCHDIR/$TEMPFILE ] ; then
		rm $WD/$BENCHDIR/$TEMPFILE
	fi
	touch $WD/$BENCHDIR/$TEMPFILE
	#create temporary copy of target file
	sed -e '/^ / d' < ${WD}/${BENCHDIR}/${JOBTURNAROUNDSFILENAMEUNSORTED} >> ${WD}/${BENCHDIR}/${TEMPFILE}

	#work on each line of JOBTURNAROUNDS to make index monotonic and calc avg
	AVGJOBTURNAROUNDTIME=0
	NOFALL=`expr ${TESTAPP1NOFJOBS} + ${TESTAPP2NOFJOBS}`
	for(( i=0 ; i<$NOFALL ; i++ ))
	do
		#cut first line and write remaining text back to temporary file
		TMPAVGJOBTURNAROUNDTIME=`head -n 1 < ${WD}/$BENCHDIR/${TEMPFILE}`
		TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${TEMPFILE}`
		echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${TEMPFILE}
		
		#extract second value (first is index)
		TMPAVGJOBTURNAROUNDTIME=`echo $TMPAVGJOBTURNAROUNDTIME | awk '{ print $2 }'`
		
		#write to final JOBTURNAOURNDSFILENAME with new index (to have them sorted ascending)
		echo -e "$i $TMPAVGJOBTURNAROUNDTIME" >> $WD/$BENCHDIR/$JOBTURNAROUNDSFILENAME
		
		#calculate aritmetic mean sum
		AVGJOBTURNAROUNDTIME=$(echo "scale=2; $AVGJOBTURNAROUNDTIME+$TMPAVGJOBTURNAROUNDTIME" | bc)
	done

	AVGJOBTURNAROUNDTIME=$(echo "scale=2; ${AVGJOBTURNAROUNDTIME}/${NOFALL}" | bc)
	echo -e "$AVGJOBTURNAROUNDTIME" >> $WD/$BENCHDIR/$AVGJOBTURNAROUNDSFILENAME
fi

# (6)
# finish by printing out the time the batch took (only computation not management)
#
#difference in ns
DIFF=$(echo "scale=2; (${STOP}-${START})" | bc)
#difference in ms
DIFF=$(echo "scale=2; (${DIFF}/1000000)" | bc)
echo "${DIFF}"
exit 0
