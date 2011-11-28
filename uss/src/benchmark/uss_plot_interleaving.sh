#!/bin/sh
#
# user space scheduler (USS)
# plot
# OVERHEADS
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
	echo "uss_plot_overheads.sh <outputfile> <file1> <file2> ..."
	echo "this X curves for X file (they have to contain <time> <value>)"
fi

if [ $# -eq 2 ] ; then
	echo "correct parameters"
	PNGFILE=$1
	BFILE1=$2
	BFILE2=$3
	BFILE3=$4
	BFILE4=$5
	BFILE5=$6
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
echo 'set xtic auto' >> $WD/gnuplotcommands
echo 'set ytic auto' >> $WD/gnuplotcommands
#echo "set xr [0:8]" >> $WD/gnuplotcommands
#echo "set yr [40000:85000]" >> $WD/gnuplotcommands
echo 'set ylabel "number of searched strings"' >> $WD/gnuplotcommands
echo 'set xlabel "time in seconds"' >> $WD/gnuplotcommands

#gnuplot line styles
echo 'set style line 1 lc 22'  >> $WD/gnuplotcommands
echo 'set style line 2 lc rgb "grey"'  >> $WD/gnuplotcommands
echo 'set style line 3 lc 14'  >> $WD/gnuplotcommands
echo 'set style line 7 lc rgb "black"'  >> $WD/gnuplotcommands
#echo 'set key right top' >> $WD/gnuplotcommands

#gnuplot data
#gnuplot data
echo 'plot "'"${WD}/${BFILE1}"'" using (2):1: title "" linestyle 2 fs solid 1.0
'  >> $WD/gnuplotcommands

#run gnuplot
gnuplot $WD/gnuplotcommands


# (3)
# cleanup and exit
#
rm $WD/$BENCHDIR/gnuplotcommands
exit 0

