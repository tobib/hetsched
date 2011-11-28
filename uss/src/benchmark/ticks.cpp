#include <iostream>
#include <fstream>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include "./cycle.h"

int main(int argc, char** argv) {
  
  ticks x10, x20;
  int i;
  double average = 0.0;
  double avg_time, avg_time_ms, avg_time_ns = 0.0;
  double t1,t2, t3, t4, t5, t6 =0.0;

  //number of runs to measure ticks --> len=100 needs roughy 100 seconds to measure
  int len=100;
  
  
  struct timeval start, end;
  
  //precision of measurement_error --> #runs to measure error
  int prec = 100000;
  
  gettimeofday(&start, NULL);
  
  for (int i=0;i<prec;i++)
    {
      x10 = getticks();
      x20 = getticks();
    }

  gettimeofday(&end, NULL);
       
  t1 = start.tv_sec * 1000000000.0 + (start.tv_usec*1000.0);
  t2 = end.tv_sec * 1000000000.0 +(end.tv_usec*1000.0);
       
  double avg_meas_err_ns =(t2-t1)/prec;
  
       
  printf("Measurement error (ns): %f\n",  avg_meas_err_ns);
  

     
  for (i=1;i<len;i++) {
	 
    gettimeofday(&start, NULL);    

    x10 = getticks();
    sleep(1);
    x20 = getticks();
    
    gettimeofday(&end, NULL);
	 
    //s
    t1 = start.tv_sec+(start.tv_usec/1000000.0);
    t2 = end.tv_sec+(end.tv_usec/1000000.0);

    //ms
    t3 = start.tv_sec * 1000.0 + start.tv_usec/1000.0;
    t4 = end.tv_sec * 1000.0 + end.tv_usec/1000.0;

    //ns
    t5 = start.tv_sec * 1000000000.0 + start.tv_usec*1000.0;
    t6 = end.tv_sec* 1000000000.0 + end.tv_usec*1000.0;

    average = ( ( average * (i-1) ) + (x20-x10) ) / (double) i;
    avg_time = ( ( avg_time * (i-1) ) + (t2-t1) ) / (double)i; 
    avg_time_ms = ( ( avg_time_ms * (i-1) ) + (t4-t3) ) / (double)i;
    avg_time_ns = ( ( avg_time_ns * (i-1) ) + (t6-t5) - avg_meas_err_ns ) / (double)i;
  }
	 
  printf("average ticks: %f\n", average);
  printf("average time (sec): %f\n", avg_time);
  printf("average time (ms): %f\n", avg_time_ms);
  printf("average time (ns): %f\n", avg_time_ns);
  
  double ticks_per_ns = average/avg_time_ns;
  
  printf("ticks_per_ns: %f\n", ticks_per_ns);
   
  std::ofstream fileStream("average_ticks.txt");
       
  fileStream << ticks_per_ns << "\n";
       
  return 0;
}
  
  
