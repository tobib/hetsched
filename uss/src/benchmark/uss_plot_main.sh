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

if [ $# -eq 11 ] ; then
	echo "correct parameters"
	PNGFILE=$1
	BFILE1=$2
	BFILE2=$3
	BFILE3=$4
	BFILE4=$5
	BFILE5=$6
	BFILE6=$7
	BFILE7=$8
	BFILE8=$9
	BFILE9=${10}
	BFILE10=${11}
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
echo "set terminal png size 1000,500" >> $WD/gnuplotcommands
#echo "set terminal " >> $WD/gnuplotcommands
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
echo 'plot "'"${WD}/${BFILE1}"'" using 1:2 title "" with lines linestyle 14, \
	"'"${WD}/${BFILE2}"'" using 1:2 title "" with lines linestyle 13, \
	"'"${WD}/${BFILE3}"'" using 1:2 title "" with lines linestyle 3, \
	"'"${WD}/${BFILE4}"'" using 1:2 title "" with lines linestyle 4, \
	"'"${WD}/${BFILE5}"'" using 1:2 title "" with lines linestyle 5, \
	"'"${WD}/${BFILE6}"'" using 1:2 title "" with lines linestyle 6, \
	"'"${WD}/${BFILE7}"'" using 1:2 title "" with lines linestyle 11, \
	"'"${WD}/${BFILE8}"'" using 1:2 title "" with lines linestyle 8, \
	"'"${WD}/${BFILE9}"'" using 1:2 title "" with lines linestyle 15, \
	"'"${WD}/${BFILE10}"'" using 1:2 title "" with lines linestyle 22
'  >> $WD/gnuplotcommands

#run gnuplot
gnuplot $WD/gnuplotcommands


# (3)
# cleanup and exit
#
rm $WD/$BENCHDIR/gnuplotcommands
exit 0

