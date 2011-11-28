#!/bin/sh
#
# user space scheduler (USS)
# plot
# CONTEXT (ACCELERATOR) SWITCHING TIMES
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
	echo "uss_plot_overheads.sh <outputfile> <value1> <value2> ..."
	echo "this prints some bars with the given size"
fi

if [ $# -eq 4 ] ; then
	echo "correct parameters"
	PNGFILE=$1
	BFILE1=$2
	BFILE2=$3
	BFILE3=$4
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


#gnuplot basic parameters
echo "set terminal png" >> $WD/gnuplotcommands
echo 'set output "'"$PNGFILE"'" ' >> $WD/gnuplotcommands
echo "unset log" >> $WD/gnuplotcommands
echo "unset label" >> $WD/gnuplotcommands

#gnuplot axis and labels
#echo 'set xtics 1,5' >> $WD/gnuplotcommands
echo 'set xtics ("1sec" 2, "4sec" 4, "infinite" 6)' >> $WD/gnuplotcommands
echo 'set ytic auto' >> $WD/gnuplotcommands
echo "set xr [0:8]" >> $WD/gnuplotcommands
echo "set yr [40000:85000]" >> $WD/gnuplotcommands
echo 'set ylabel "batchc turnaround time in ms"' >> $WD/gnuplotcommands
echo 'set xlabel "scheduling granularity"' >> $WD/gnuplotcommands

#gnuplot line styles
echo 'set style line 1 lc 22'  >> $WD/gnuplotcommands
echo 'set style line 2 lc rgb "grey"'  >> $WD/gnuplotcommands
echo 'set style line 3 lc 14'  >> $WD/gnuplotcommands
echo 'set style line 7 lc rgb "black"'  >> $WD/gnuplotcommands
#echo 'set key right top' >> $WD/gnuplotcommands

#gnuplot data
#gnuplot data
echo 'plot "'"${WD}/${BFILE1}"'" using (2):1:(0.6) title "" linestyle 2 w boxes fs solid 1.0, \
	 "'"${WD}/${BFILE1}"'" using (2):1:2 title "" linestyle 7 w yerrorbars, \
	 "'"${WD}/${BFILE2}"'" using (4):1:(0.6) title "" linestyle 2 w boxes fs solid 1.0, \
	 "'"${WD}/${BFILE2}"'" using (4):1:2 title "" linestyle 7 w yerrorbars, \
	 "'"${WD}/${BFILE3}"'" using (6):1:(0.6) title "" linestyle 2 w boxes fs solid 1.0, \
	 "'"${WD}/${BFILE3}"'" using (6):1:2 title "" linestyle 7 w yerrorbars
'  >> $WD/gnuplotcommands

#run gnuplot
gnuplot $WD/gnuplotcommands


# (3)
# cleanup and exit
#
rm $WD/$BENCHDIR/gnuplotcommands
exit 0

