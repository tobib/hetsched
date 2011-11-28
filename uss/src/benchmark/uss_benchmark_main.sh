#!/bin/sh
#
# user space scheduler (USS)
# benchmarks
# main 1

# (0)
# variables and base parameters
#
TESTAPP=testappc
SMALLPARAM=50
BIGPARAM=10
ALT=99488307847707802
ALT=9948830784770780222

CWD=`pwd`
WD=$CWD

TESTDIR=testapp
BENCHDIR=benchmark

TARGETFILE="uss_benchmark_main.log"
TEMPFILE="uss_benchmark_main.tmp"
PLOTFILE="uss_benchmark_main.plot"
PNGFILE="uss_benchmark_main.png"

LIBPATH="/home/dwelp/uss/library/"

if [ -f $WD/$BENCHDIR/$TARGETFILE ] ; then
	rm $WD/$BENCHDIR/$TARGETFILE
fi
touch $WD/$BENCHDIR/$TARGETFILE

if [ -f $WD/$BENCHDIR/$TEMPFILE ] ; then
	rm $WD/$BENCHDIR/$TEMPFILE
fi
touch $WD/$BENCHDIR/$TEMPFILE

SJ=0
BJ=0


# (1)
# parse input parameters
#
if [ $# -lt 2 ] ; then
	echo "wrong parameters (type --help for manual)"
fi

if [ "$1" == "--help" ] ; then
	echo "uss_benchmark_main SMALL_JOBS BIG_JOBS"
	echo "this script will start testappprime multiple times with different sizes"
fi

if [ $# -eq 2 ] ; then
	echo "correct parameters"
	SJ=$1
	BJ=$2
else
	exit 1
fi


# (2)
# start testapp multiple times with parameter id and number to facorize
# +take time
START=`date +%s%N`

NOFALL=`expr ${SJ} + ${BJ}`

for (( i=0 ; i<${NOFALL} ; i++ ))
do
	SELECTOR=$(echo "scale=0; ${i}%2" | bc)
	if [ ${SELECTOR} -eq 0 ] ; then
		#execute command small
		LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP} ${i} ${SMALLPARAM} >> ${WD}/$BENCHDIR/${TARGETFILE} &
	fi

	if [ ${SELECTOR} -eq 1 ] ; then
		#execute command small
		LD_LIBRARY_PATH=${LIBPATH} ${WD}/${TESTDIR}/${TESTAPP} ${i} ${BIGPARAM} >> ${WD}/$BENCHDIR/${TARGETFILE} &
	fi
done


# (3)
# wait for the last one to finish
# +take time
wait
STOP=`date +%s%N`
#difference in ns
DIFF=$(echo "scale=2; (${STOP}-${START})" | bc)
#difference in ms
DIFF=$(echo "scale=2; (${DIFF}/1000000)" | bc)

# (4)
# calculate average turnaround time
#

#create temporary copy of target file
sed -e '/^ / d' < ${WD}/${BENCHDIR}/${TARGETFILE} >> ${WD}/${BENCHDIR}/${TEMPFILE}

if [ -f $WD/$BENCHDIR/$PLOTFILE ] ; then
	rm $WD/$BENCHDIR/$PLOTFILE
fi
touch $WD/$BENCHDIR/$PLOTFILE

#cat ${WD}/${BENCHDIR}/${TEMPFILE}

#work on each line of file
AVGTURN=0
for(( i=0 ; i<$NOFALL ; i++ ))
do
	#cut first line and write remaining text back to temporary file
	TMPAVGTURN=`head -n 1 < ${WD}/$BENCHDIR/${TEMPFILE}`
	TMPTEXT=`sed -e '1,1 d' < ${WD}/$BENCHDIR/${TEMPFILE}`
	echo -e "$TMPTEXT" > ${WD}/$BENCHDIR/${TEMPFILE}
	
	#extract second value (first is index)
	TMPAVGTURN=`echo $TMPAVGTURN | awk '{ print $2 }'`
	
	#write to plotfile with new index (to have them sorted ascending)
	echo -e "$i $TMPAVGTURN" >> $WD/$BENCHDIR/$PLOTFILE
	
	#calculate aritmetic mean sum
	AVGTURN=$(echo "scale=2; $AVGTURN+$TMPAVGTURN" | bc)
done

AVGTURN=$(echo "scale=2; ${AVGTURN}/${NOFALL}" | bc)
echo -e "computed the average turnaround time with value: $AVGTURN"
echo -e "entires batch completed in time [ms]: $DIFF"


# (5)
# draw figure
#
#check if gnuplot is installed
gnuplot -V 2> /dev/null
if [ $? -gt 0 ] ; then
	echo -e "no gnuplot installed -> aborting!"
	exit 0
fi

if [ -f $WD/$BENCHDIR/gnuplotcommands ] ; then
	rm $WD/$BENCHDIR/gnuplotcommands
fi
touch $WD/$BENCHDIR/gnuplotcommands

#gnuplot basic parameters
echo "set terminal png" >> $WD/$BENCHDIR/gnuplotcommands
echo 'set output "'"$PNGFILE"'" ' >> $WD/$BENCHDIR/gnuplotcommands
echo "unset log" >> $WD/$BENCHDIR/gnuplotcommands
echo "unset label" >> $WD/$BENCHDIR/gnuplotcommands

#gnuplot axis and labels
echo 'set xtics 1,2' >> $WD/$BENCHDIR/gnuplotcommands
echo 'set ytic auto' >> $WD/$BENCHDIR/gnuplotcommands
echo "set xr [0:28]" >> $WD/$BENCHDIR/gnuplotcommands
echo 'set ylabel "turnaround time of job in ms"' >> $WD/$BENCHDIR/gnuplotcommands
echo 'set xlabel "jobs"' >> $WD/$BENCHDIR/gnuplotcommands

#gnuplot line styles
echo 'set style line 1 lc 22'  >> $WD/$BENCHDIR/gnuplotcommands
echo 'set style line 2 lc 2'  >> $WD/$BENCHDIR/gnuplotcommands
echo 'set style line 3 lc 14'  >> $WD/$BENCHDIR/gnuplotcommands
echo 'set style line 7 lc rgb "black"'  >> $WD/$BENCHDIR/gnuplotcommands
echo 'set key left top' >> $WD/$BENCHDIR/gnuplotcommands

echo preview
cat ${WD}/${BENCHDIR}/${TARGETFILE}

#gnuplot data
echo 'plot "'"${WD}/${BENCHDIR}/${PLOTFILE}"'" using 1:2:(0.4) title "turnaround" linestyle 1 w boxes fs solid 1.0
'  >> $WD/$BENCHDIR/gnuplotcommands
#run gnuplot

gnuplot $WD/$BENCHDIR/gnuplotcommands


# (6)
# cleanup and exit
#
rm $WD/$BENCHDIR/gnuplotcommands
exit 0

