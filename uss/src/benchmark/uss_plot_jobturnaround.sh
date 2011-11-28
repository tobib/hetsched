#!/bin/sh
#
# user space scheduler (USS)
# plot
# TURNAROUNDS
# draw figure

# (0)
# variables and base parameters
#
CWD=`pwd`
WD=$CWD


# (1)
# parse input parameters
#
if [ $# -lt 3 ] ; then
	echo "wrong parameters (type --help for manual)"
fi

if [ "$1" == "--help" ] ; then
	echo "uss_benchmark_plotjobs.sh <jobsfile> <average> <outputfilename>"
	echo "needs a file with the following format"
	echo "<positionindex> <value>"
fi

if [ $# -eq 3 ] ; then
	echo "correct parameters"
	PLOTFILE=$1
	AVERAGE=$2
	PNGFILE=$3
else
	exit 1
fi

# (2)
# draw figure
#
#check if gnuplot is installed
gnuplot -V 2> /dev/null
if [ $? -gt 0 ] ; then
	echo -e "no gnuplot installed -> aborting!"
	exit 0
fi

if [ -f $WD/gnuplotcommands ] ; then
	rm $WD/gnuplotcommands
fi
touch $WD/gnuplotcommands

NOFELEMENTS=`wc -l < $PLOTFILE`
NOFELEMENTSPLUS=`expr $NOFELEMENTS + 2`

#gnuplot basic parameters
echo "set terminal png size 1000,600" >> $WD/gnuplotcommands
echo 'set output "'"$PNGFILE"'" ' >> $WD/gnuplotcommands
echo "unset log" >> $WD/gnuplotcommands
echo "unset label" >> $WD/gnuplotcommands

#gnuplot axis and labels
echo 'set xtics 1,5' >> $WD/gnuplotcommands
echo 'set ytic auto' >> $WD/gnuplotcommands
#echo "set yr [0:140000]" >> $WD/gnuplotcommands
echo 'set ylabel "turnaround time of job in ms"' >> $WD/gnuplotcommands
echo 'set xlabel "jobs"' >> $WD/gnuplotcommands

#gnuplot line styles
echo 'set style line 1 lc 22'  >> $WD/gnuplotcommands
echo 'set style line 2 lc rgb "grey"'  >> $WD/gnuplotcommands
echo 'set style line 3 lc 14'  >> $WD/gnuplotcommands
echo 'set style line 7 lc rgb "black"'  >> $WD/gnuplotcommands
echo 'set key left top' >> $WD/gnuplotcommands
echo 'set arrow from 0,"'"${AVERAGE}"'" to "'"${NOFELEMENTSPLUS}"'","'"${AVERAGE}"'" ls 7 nohead' >> $WD/gnuplotcommands

#gnuplot data
echo 'plot "'"${WD}/${PLOTFILE}"'" using 1:2:(0.4) title "turnaround" linestyle 2 w boxes fs solid 1.0, \
	 "'"${WD}/${PLOTFILE}"'" using 1:2:3 title "" linestyle 7 w yerrorbars
'  >> $WD/gnuplotcommands

#run gnuplot
gnuplot $WD/gnuplotcommands


# (3)
# cleanup and exit
#
rm $WD/$BENCHDIR/gnuplotcommands
exit 0

