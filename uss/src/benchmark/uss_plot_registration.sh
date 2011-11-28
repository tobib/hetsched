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
if [ "$1" == "--help" ] ; then
	echo "uss_benchmark_plotjobs.sh <outputfile> <value1> <value2> ..."
	echo "this prints some bars with the given size"
fi


PNGFILE=$1

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

#gnuplot basic parameters
echo "set terminal png" >> $WD/gnuplotcommands
echo 'set output "'"$PNGFILE"'" ' >> $WD/gnuplotcommands
echo "unset log" >> $WD/gnuplotcommands
echo "unset label" >> $WD/gnuplotcommands

#gnuplot axis and labels
echo 'set xtics 1,5' >> $WD/gnuplotcommands
echo 'set ytic auto' >> $WD/gnuplotcommands
#echo "set xr [0:60]" >> $WD/gnuplotcommands
echo 'set ylabel "turnaround time of job in ms"' >> $WD/gnuplotcommands
echo 'set xlabel "jobs"' >> $WD/gnuplotcommands

#gnuplot line styles
echo 'set style line 1 lc 22'  >> $WD/gnuplotcommands
echo 'set style line 2 lc 2'  >> $WD/gnuplotcommands
echo 'set style line 3 lc 14'  >> $WD/gnuplotcommands
echo 'set style line 7 lc rgb "black"'  >> $WD/gnuplotcommands
echo 'set key left top' >> $WD/gnuplotcommands

#gnuplot data
echo 'plot "'"${WD}/${PLOTFILE}"'" using 1:2:(0.4) title "turnaround" linestyle 1 w boxes fs solid 1.0'  >> $WD/gnuplotcommands

#run gnuplot
gnuplot $WD/gnuplotcommands


# (3)
# cleanup and exit
#
rm $WD/$BENCHDIR/gnuplotcommands
exit 0

