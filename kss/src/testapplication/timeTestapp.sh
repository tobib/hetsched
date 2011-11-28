#!/bin/bash
directory=`dirname $0`
mean=0

make all

echo "Time measurements on `date`" > time.log

for (( i = 1 ; i <= $1 ; i++ ))
do

##Zeitmessung
  start=$(date +%s)

##Testapp
./testapp || { echo "Restarting $i" >> time.log; let i--; continue; }

##Zeitmessung
  end=$(date +%s)
  let diff=$end-$start
  let diffmin=diff/60
  echo "Testapp ran for ${diff} seconds (that is roughly ${diffmin} minutes)"
  echo "$i: $diff" >> time.log
  mean=`expr $mean + $diff`
  #mean=`expr $mean + \( $diff / $1 \)`
done

echo "Combined runtime: $mean seconds for $1 runs" >> time.log

mean=`expr $mean / $1`

echo "Mean runtime: $mean" >> time.log

cat time.log
