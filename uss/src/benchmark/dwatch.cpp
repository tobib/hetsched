#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/time.h>
#include <time.h>
#include "cycle.h"
#include "dwatch.h"

using namespace std;

double ticks_per_ns=2.40039;
double startticks, lastticks, nowticks, diffticks, runticks;
double diff_in_ms;

void init_dwatch()
{
	//read average ticks per ns from file
	ifstream indata;
	indata.open("./benchmark/average_ticks.txt");
	if(!indata) {cerr << "Error reading file" << endl; exit(-1);}
	indata >> ticks_per_ns;
	indata.close();
#if(TESTDEBUG == 1)		
	printf("INIT DWATCH with ticks_per_ns=%lf\n", ticks_per_ns);
#endif
	startticks = getticks();
	lastticks = startticks;
}

double diff_dwatch()
{
	nowticks = getticks();
	diffticks = nowticks - lastticks;
	lastticks = nowticks;
	return diffticks / ticks_per_ns / 1000000;
}

void print_dwatch()
{
	//update lastticks
	nowticks = getticks();
	diffticks = nowticks - lastticks;
	lastticks = nowticks;
	//print current stopwatch display in ms
	printf("\t[timestamp in ms: %lf] \n", (nowticks - startticks) / ticks_per_ns / 1000000);
}

void print_dwatch(char *string)
{
	printf("%s ", string);
	print_dwatch();
}

void print_dwatch(char *string, int print_diff_dwatch)
{
	printf("%s ", string);
	print_dwatch();
	if(print_diff_dwatch) {printf("    delta = %lf \n\n", diff_dwatch());}
}

