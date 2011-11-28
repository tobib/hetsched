#!/bin/sh
#
# user space scheduler (USS)
# benchmarks
# TURNAROUNDS
# this determines average turnaround times of jobs or batches by using uss_benchmark_batch.sh

# syntax
# uss_benchmark_turnaround.sh <nof batches to start> <testsetting> <measure job turnarounds = 1>

# (0)
# variables and base parameters
#
CWD=`pwd`
WD=$CWD
TESTDIR=testapp
BENCHDIR=benchmark
LIBPATH="/home/dwelp/uss/library/"

#template will be retailored by when staring each batch
JOBTURNAROUNDSFILENAME_TEMPLATE="uss_benchmark_jobturnarounds_b"
PERJOBTURNAROUNDSFILENAME_TEMPLATE="uss_benchmark_perjobturnarounds"

#fixed names
AVGJOBTURNAROUNDSFILENAME="uss_benchmark_avgjobturnarounds.log"
BATCHTURNAROUNDSFILENAME="uss_benchmark_batchturnarounds.log"

RESULT_MEAN_JOBTURNAROUNDS="uss_benchmark_mean_jobturnarounds.log"
RESULT_MEAN_AVGJOBTURNAROUND="uss_benchmark_mean_avgjobturnaround.log"
RESULT_MEAN_BATCHTURNAROUND="uss_benchmark_mean_batchturnarounds.log"

TEMPFILE="uss_benchmark_turnaround.tmp"

# (1)
# parse input parameters
#
if [ $# -lt 3 ] ; then
	echo "wrong parameters (type --help for manual)"
fi

if [ "$1" == "--help" ] ; then
	echo "uss_benchmark_turnaround.sh <nof batches to start> <testsetting> <measure job turnaround on = 1>"
	echo "this script will start uss_benchmark_batch.sh multiple times"
fi

if [ $# -eq 3 ] ; then
	#echo "correct parameters"
	NOFBATCHES=${1}
	TESTSETTING=${2}
	MEASURETURNAROUNDS=${3}
else
	exit 1
fi


# (2)
# refresh some files
#
#avgfiles
if [ -f $WD/$BENCHDIR/${AVGJOBTURNAROUNDSFILENAME} ] ; then
	rm $WD/$BENCHDIR/${AVGJOBTURNAROUNDSFILENAME}
fi
touch $WD/$BENCHDIR/${AVGJOBTURNAROUNDSFILENAME}

if [ -f $WD/$BENCHDIR/${BATCHTURNAROUNDSFILENAME} ] ; then
	rm $WD/$BENCHDIR/${BATCHTURNAROUNDSFILENAME}
fi
touch $WD/$BENCHDIR/${BATCHTURNAROUNDSFILENAME}

#results
if [ -f $WD/$BENCHDIR/${RESULT_MEAN_JOBTURNAROUNDS} ] ; then
	rm $WD/$BENCHDIR/${RESULT_MEAN_JOBTURNAROUNDS}
fi
touch $WD/$BENCHDIR/${RESULT_MEAN_JOBTURNAROUNDS}

if [ -f $WD/$BENCHDIR/${RESULT_MEAN_AVGJOBTURNAROUND} ] ; then
	rm $WD/$BENCHDIR/${RESULT_MEAN_AVGJOBTURNAROUND}
fi
touch $WD/$BENCHDIR/${RESULT_MEAN_AVGJOBTURNAROUND}

if [ -f $WD/$BENCHDIR/${RESULT_MEAN_BATCHTURNAROUND} ] ; then
	rm $WD/$BENCHDIR/${RESULT_MEAN_BATCHTURNAROUND}
fi
touch $WD/$BENCHDIR/${RESULT_MEAN_BATCHTURNAROUND}


# (3)
# start batch multiple times with testsettings 1 or 2
#
if [ ${MEASURETURNAROUNDS} -eq 1 ] ; then
	#start with redirection of job turnaround times
	for (( i=0 ; i<${NOFBATCHES} ; i++ ))
	do
		#tailor filename
		CURRENTJOBTURNAROUNDSFILENAME=`echo "${JOBTURNAROUNDSFILENAME_TEMPLATE}${i}.log"`
		#execute command
		bash ${WD}/${BENCHDIR}/uss_benchmark_batch.sh ${TESTSETTING} ${CURRENTJOBTURNAROUNDSFILENAME} ${AVGJOBTURNAROUNDSFILENAME} >> ${WD}/$BENCHDIR/${BATCHTURNAROUNDSFILENAME}
		echo "iteration ${i} done"
	done
else
	#ignore job turnaround times
	for (( i=0 ; i<${NOFBATCHES} ; i++ ))
	do
		#execute command
		bash ${WD}/${BENCHDIR}/uss_benchmark_batch.sh ${TESTSETTING} >> ${WD}/$BENCHDIR/${BATCHTURNAROUNDSFILENAME}
	done
fi

# (4)
# wait on batches to finish
#
wait
echo "measurements done"

# (5)
# calculate mean batch turnaround time and confidence interval 95%
#
#clean tempfile
echo "step 5 started"
if [ -f $WD/$BENCHDIR/$TEMPFILE ] ; then
	rm $WD/$BENCHDIR/$TEMPFILE
fi
touch $WD/$BENCHDIR/$TEMPFILE

#create temporary copy of target file
sed -e '/^ / d' < ${WD}/${BENCHDIR}/${BATCHTURNAROUNDSFILENAME} >> ${WD}/${BENCHDIR}/${TEMPFILE}

#work on each line of file
VAL=0
MEANBATCHTURNAROUND=0
for(( i=0 ; i<$NOFBATCHES ; i++ ))
do
	#cut first line and write remaining text back to temporary file
	VAL=`head -n 1 < ${WD}/$BENCHDIR/${TEMPFILE}`
	TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${TEMPFILE}`
	echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${TEMPFILE}
	
	#calculate aritmetic mean (sum only here)
	MEANBATCHTURNAROUND=$(echo "scale=2; $MEANBATCHTURNAROUND+$VAL" | bc)
done

#final mean batch turnaround time
MEANBATCHTURNAROUND=$(echo "scale=2; ${MEANBATCHTURNAROUND}/${NOFBATCHES}" | bc)


#clean tempfile
if [ -f $WD/$BENCHDIR/$TEMPFILE ] ; then
	rm $WD/$BENCHDIR/$TEMPFILE
fi
touch $WD/$BENCHDIR/$TEMPFILE

#create temporary copy of target file
sed -e '/^ / d' < ${WD}/${BENCHDIR}/${BATCHTURNAROUNDSFILENAME} >> ${WD}/${BENCHDIR}/${TEMPFILE}

#work on each line of file
VAL=0
VARIANCEBATCHTURNAROUND=0
for(( i=0 ; i<$NOFBATCHES ; i++ ))
do
	#cut first line and write remaining text back to temporary file
	VAL=`head -n 1 < ${WD}/$BENCHDIR/${TEMPFILE}`
	TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${TEMPFILE}`
	echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${TEMPFILE}
	
	#calculate batch variance (sum only here)
	VARIANCEBATCHTURNAROUND=$(echo "scale=9; $VARIANCEBATCHTURNAROUND+(($VAL-$MEANBATCHTURNAROUND)^2)" | bc)
done

#finalize batch variance of batch turnaround times
VARIANCEBATCHTURNAROUND=$(echo "scale=9; ${VARIANCEBATCHTURNAROUND}/(${NOFBATCHES})" | bc)

#compute 95% confindence interval
MEANBATCHTURNAROUNDCONFI=$(echo "scale=9; (1.96*sqrt($VARIANCEBATCHTURNAROUND))/sqrt($NOFBATCHES)" | bc)

#write result to storage location
echo -e "$MEANBATCHTURNAROUND $MEANBATCHTURNAROUNDCONFI" >> $WD/$BENCHDIR/${RESULT_MEAN_BATCHTURNAROUND}

echo "step 5 done"


## (6)
## calculate mean of avg job turnaround times and confidence interval 95%
##
#clean tempfile
echo "step 6 started"
if [ -f $WD/$BENCHDIR/$TEMPFILE ] ; then
	rm $WD/$BENCHDIR/$TEMPFILE
fi
touch $WD/$BENCHDIR/$TEMPFILE

#create temporary copy of target file
sed -e '/^ / d' < ${WD}/${BENCHDIR}/${AVGJOBTURNAROUNDSFILENAME} >> ${WD}/${BENCHDIR}/${TEMPFILE}

#work on each line of file
VAL=0
MEANJOBAVGTURNAROUND=0
for(( i=0 ; i<$NOFBATCHES ; i++ ))
do
	#cut first line and write remaining text back to temporary file
	VAL=`head -n 1 < ${WD}/$BENCHDIR/${TEMPFILE}`
	TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${TEMPFILE}`
	echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${TEMPFILE}
	
	#calculate aritmetic mean sum
	MEANJOBAVGTURNAROUND=$(echo "scale=2; $MEANJOBAVGTURNAROUND+$VAL" | bc)
done

#final mean of avg job turnaround times
MEANJOBAVGTURNAROUND=$(echo "scale=2; ${MEANJOBAVGTURNAROUND}/${NOFBATCHES}" | bc)


#clean tempfile
if [ -f $WD/$BENCHDIR/$TEMPFILE ] ; then
	rm $WD/$BENCHDIR/$TEMPFILE
fi
touch $WD/$BENCHDIR/$TEMPFILE

#create temporary copy of target file
sed -e '/^ / d' < ${WD}/${BENCHDIR}/${AVGJOBTURNAROUNDSFILENAME} >> ${WD}/${BENCHDIR}/${TEMPFILE}

#work on each line of file
VAL=0
VARIANCEAVGJOBTURNAROUND=0
for(( i=0 ; i<$NOFBATCHES ; i++ ))
do
	#cut first line and write remaining text back to temporary file
	VAL=`head -n 1 < ${WD}/$BENCHDIR/${TEMPFILE}`
	TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${TEMPFILE}`
	echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${TEMPFILE}
	
	#calculate batch variance (sum only here)
	VARIANCEAVGJOBTURNAROUND=$(echo "scale=9; $VARIANCEAVGJOBTURNAROUND+(($VAL-$MEANJOBAVGTURNAROUND)^2)" | bc)
done

#finalize batch variance of batch turnaround times
VARIANCEAVGJOBTURNAROUND=$(echo "scale=9; ${VARIANCEAVGJOBTURNAROUND}/(${NOFBATCHES})" | bc)

#compute 95% confindence interval
MEANAVGJOBTURNAROUNDCONFI=$(echo "scale=9; (1.96*sqrt($VARIANCEAVGJOBTURNAROUND))/sqrt($NOFBATCHES)" | bc)

#write result to storage location
echo -e "$MEANJOBAVGTURNAROUND $MEANAVGJOBTURNAROUNDCONFI" >> $WD/$BENCHDIR/${RESULT_MEAN_AVGJOBTURNAROUND}

echo "step 6 done"

# (7)
# calculate mean of job turnaround times and confidence interval 95%
#
echo "step 7 started"
#determine how many jobs each batch had
CURRENTJOBTURNAROUNDSFILENAME=`echo "${JOBTURNAROUNDSFILENAME_TEMPLATE}0.log"`
NOFJOBS=`wc -l < ${WD}/${BENCHDIR}/${CURRENTJOBTURNAROUNDSFILENAME}`
echo " number of jobs recocnized = $NOFJOBS"

#produce a valuefile for each job
for(( j=0 ; j<$NOFJOBS ; j++ ))
do
	#tailor name for valuefile and then clear it
	CURRENTPERJOBTURNAROUNDSFILENAME=`echo ${PERJOBTURNAROUNDSFILENAME_TEMPLATE}${j}.log`
	if [ -f $WD/$BENCHDIR/$CURRENTPERJOBTURNAROUNDSFILENAME ] ; then
		rm $WD/$BENCHDIR/$CURRENTPERJOBTURNAROUNDSFILENAME
	fi
	touch $WD/$BENCHDIR/$CURRENTPERJOBTURNAROUNDSFILENAME	
	
	for(( b=0 ; b<$NOFBATCHES ; b++ ))
	do
		#tailor sourcefile
		CURRENTJOBTURNAROUNDSFILENAME=`echo "${JOBTURNAROUNDSFILENAME_TEMPLATE}${b}.log"`

		#cut first line of sourcefile and write remaining text back
		LINE=`head -n 1 < ${WD}/$BENCHDIR/${CURRENTJOBTURNAROUNDSFILENAME}`
		TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${CURRENTJOBTURNAROUNDSFILENAME}`
		echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${CURRENTJOBTURNAROUNDSFILENAME}
		
		#extract second value (first is index)
		VAL=`echo $LINE | awk '{ print $2 }'`
		
		#append value from per-batch file to per-job file
		echo -e "$VAL" >> ${WD}/$BENCHDIR/${CURRENTPERJOBTURNAROUNDSFILENAME}
	done
done

echo " done creating per-job files"

#work on the each jobs valuefile
for(( j=0 ; j<$NOFJOBS ; j++ ))
do
	#tailor name for valuefile
	CURRENTPERJOBTURNAROUNDSFILENAME=`echo ${PERJOBTURNAROUNDSFILENAME_TEMPLATE}${j}.log`

	#clean tempfile
	if [ -f $WD/$BENCHDIR/$TEMPFILE ] ; then
		rm $WD/$BENCHDIR/$TEMPFILE
	fi
	touch $WD/$BENCHDIR/$TEMPFILE

	#create temporary copy of target file
	sed -e '/^ / d' < ${WD}/${BENCHDIR}/${CURRENTPERJOBTURNAROUNDSFILENAME} >> ${WD}/${BENCHDIR}/${TEMPFILE}

	#work on each line of file
	VAL=0
	MEAN=0
	for(( i=0 ; i<$NOFBATCHES ; i++ ))
	do
		#cut first line and write remaining text back to temporary file
		VAL=`head -n 1 < ${WD}/$BENCHDIR/${TEMPFILE}`
		TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${TEMPFILE}`
		echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${TEMPFILE}
		
		#calculate aritmetic mean sum
		MEAN=$(echo "scale=2; $MEAN+$VAL" | bc)
	done

	#final mean of job[j] turnaround times
	MEAN=$(echo "scale=2; ${MEAN}/${NOFBATCHES}" | bc)


	#clean tempfile
	if [ -f $WD/$BENCHDIR/$TEMPFILE ] ; then
		rm $WD/$BENCHDIR/$TEMPFILE
	fi
	touch $WD/$BENCHDIR/$TEMPFILE

	#create temporary copy of target file
	sed -e '/^ / d' < ${WD}/${BENCHDIR}/${CURRENTPERJOBTURNAROUNDSFILENAME} >> ${WD}/${BENCHDIR}/${TEMPFILE}

	#work on each line of file
	VAL=0
	VARIANCE=0
	for(( i=0 ; i<$NOFBATCHES ; i++ ))
	do
		#cut first line and write remaining text back to temporary file
		VAL=`head -n 1 < ${WD}/$BENCHDIR/${TEMPFILE}`
		TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${TEMPFILE}`
		echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${TEMPFILE}
		
		#calculate batch variance (sum only here)
		VARIANCE=$(echo "scale=9; $VARIANCE+(($VAL-$MEAN)^2)" | bc)
	done

	#finalize batch variance of batch turnaround times
	VARIANCE=$(echo "scale=9; ${VARIANCE}/(${NOFBATCHES})" | bc)

	#compute 95% confindence interval
	CONFI=$(echo "scale=9; (1.96*sqrt($VARIANCE))/sqrt($NOFBATCHES)" | bc)
	
	#write for each job the mean(=jobX averaged over all batches) and the confidence interval
	INDEX=$(echo "scale=1; (${j}+1)" | bc)
	echo -e "${INDEX} ${MEAN} ${CONFI}" >> $WD/$BENCHDIR/${RESULT_MEAN_JOBTURNAROUNDS}	

done



echo "step 7 done"

# (7)
# finish and exit
#
exit 0
echo "mean batch ta = $MEANBATCHTURNAROUND"
echo "mean job avg ta = $MEANJOBAVGTURNAROUND"
exit 0
