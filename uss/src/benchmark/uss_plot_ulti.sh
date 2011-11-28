#!/bin/sh
#
# user space scheduler (USS)
# plot
# SCHED FREQ
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
	echo "uss_plot_ulti.sh <outputfile> <value1> <value2>"
	echo "this prints some bars with the given size"
fi

if [ $# -eq 4 ] ; then
	echo "correct parameters"
	PNGFILE=$1
	BFILE1=$2
	BFILE2=$3
	BFILE3=$4
	AVERAGE1=68
	AVERAGE2=72
	AVERAGE3=140
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
echo "set terminal png size 1000,600" >> $WD/gnuplotcommands
echo 'set output "'"$PNGFILE"'" ' >> $WD/gnuplotcommands
echo "unset log" >> $WD/gnuplotcommands
echo "unset label" >> $WD/gnuplotcommands

#gnuplot axis and labels
#echo 'set xtics 1,5' >> $WD/gnuplotcommands
echo 'set xtics ("1" 2, "5" 4, "10" 6, "20" 8, "30" 10, "40" 12, "50" 14, "75" 16, "100" 18, "200" 20, "300" 22, "400" 24, "500" 26)' >> $WD/gnuplotcommands
#echo 'set xtic auto' >> $WD/gnuplotcommands
echo 'set ytic auto' >> $WD/gnuplotcommands
#echo 'set log x' >> $WD/gnuplotcommands
#echo 'set log y' >> $WD/gnuplotcommands
echo "set xr [0:28]" >> $WD/gnuplotcommands
echo "set yr [0:600]" >> $WD/gnuplotcommands
echo 'set ylabel "scheduling interval in micro seconds"' >> $WD/gnuplotcommands
echo 'set xlabel "daemon invocation interval in micro seconds"' >> $WD/gnuplotcommands

#gnuplot line styles
#echo 'set style line 1 lc 22'  >> $WD/gnuplotcommands
#echo 'set style line 2 lt 4 lc rgb "grey"'  >> $WD/gnuplotcommands
#echo 'set style line 3 lc 14'  >> $WD/gnuplotcommands
#echo 'set style line 7 lc rgb "black"'  >> $WD/gnuplotcommands
#echo 'set style line 13 lw 2 lc rgb "green"'  >> $WD/gnuplotcommands
#echo 'set style line 14 lw 2 lc rgb "orange"'  >> $WD/gnuplotcommands
#echo 'set style line 15 lw 2 lc rgb "red"'  >> $WD/gnuplotcommands
#echo 'set key right bottom' >> $WD/gnuplotcommands

#gnuplot line styles
echo 'set style line 1 lc 22'  >> $WD/gnuplotcommands
echo 'set style line 2 lc rgb "grey"'  >> $WD/gnuplotcommands
echo 'set style line 3 lc 14'  >> $WD/gnuplotcommands
echo 'set style line 7 lc rgb "black"'  >> $WD/gnuplotcommands
echo 'set style line 13 lc rgb "green"'  >> $WD/gnuplotcommands
echo 'set style line 14 lc rgb "orange"'  >> $WD/gnuplotcommands
echo 'set style line 15 lc rgb "red"'  >> $WD/gnuplotcommands
echo 'set key left top' >> $WD/gnuplotcommands

echo 'set arrow from 0,"'"${AVERAGE1}"'" to 27,"'"${AVERAGE1}"'" lt 0 lc rgb "green" nohead' >> $WD/gnuplotcommands
echo 'set arrow from 0,"'"${AVERAGE2}"'" to 27,"'"${AVERAGE2}"'" lt 0 lc rgb "orange" nohead' >> $WD/gnuplotcommands
echo 'set arrow from 0,"'"${AVERAGE3}"'" to 27,"'"${AVERAGE3}"'" lt 0 lc rgb "red" nohead' >> $WD/gnuplotcommands

#gnuplot data
echo 'plot "'"${WD}/${BFILE1}"'" using (2-0.3):13:(0.2) title "idle system" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (4-0.3):12:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (6-0.3):11:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (8-0.3):10:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (10-0.3):9:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (12-0.3):8:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (14-0.3):7:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (16-0.3):6:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (18-0.3):5:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (20-0.3):4:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (22-0.3):3:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (24-0.3):2:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE1}"'" using (26-0.3):1:(0.2) title "" linestyle 13 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (2-0):13:(0.2) title "stress 64 cpu + 64 io" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (4-0):12:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (6-0):11:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (8-0):10:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (10-0):9:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (12-0):8:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (14-0):7:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (16-0):6:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (18-0):5:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (20-0):4:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (22-0):3:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (24-0):2:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE2}"'" using (26-0):1:(0.2) title "" linestyle 14 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (2+0.3):13:(0.2) title "stress 400 cpu + 400 io" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (4+0.3):12:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (6+0.3):11:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (8+0.3):10:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (10+0.3):9:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (12+0.3):8:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (14+0.3):7:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (16+0.3):6:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (18+0.3):5:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (20+0.3):4:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (22+0.3):3:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (24+0.3):2:(0.2) title "" linestyle 15 w boxes fs solid 1.0, \
	"'"${WD}/${BFILE3}"'" using (26+0.3):1:(0.2) title "" linestyle 15 w boxes fs solid 1.0
'  >> $WD/gnuplotcommands

#run gnuplot
gnuplot $WD/gnuplotcommands


# (3)
# cleanup and exit
#
rm $WD/$BENCHDIR/gnuplotcommands
exit 0