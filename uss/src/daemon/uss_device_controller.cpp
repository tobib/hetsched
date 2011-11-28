#include "./uss_device_controller.h"
#include "./uss_scheduler.h"
#include "./uss_daemon.h"
#include "../common/uss_tools.h"

using namespace std;

/*
 * upon creation the device controller scans
 * for available hardware once
 * WARNING:
 * -> futher hardware cannot be added during runtime yet
 */
uss_device_controller::uss_device_controller(uss_scheduler *sc)
{
	int ret;
	//save scheduler
	this->sched = sc;
	this->nof_accelerators = 0;
	
	//open file that gives the accelerators to use
	FILE *fp;
	fp = fopen(USS_FILE_DEVICELIST, "r");
	if(fp == NULL) {dexit("devicelist not found\n");}
	
	//call schedulers methods to create corresponding structures
	int ci, multiqueue, runqueue;
	char c[2];
	c[1] = '\0';
	
	while((ci = fgetc(fp)) != EOF)
	{
		c[0] = (char)ci;
		if (c[0] == '\n') continue;
#if(USS_DAEMON_DEBUG == 1)
		//printf("mq (%i,%i)   ", atoi(c), ci);
#endif
		multiqueue = atoi(c);
		while((ci = fgetc(fp)) != EOF && ci != (int)'\n')
		{
			c[0] = (char)ci;
			if(c[0] == ' ' || c[0] == ':') continue;
			runqueue = atoi(c);
#if(USS_DAEMON_DEBUG == 1)			
			//printf(" (%i,%i) ", atoi(c), ci);
#endif			
			if(runqueue >= 0 && runqueue < 10) //support max 10 rqs
			{
				ret = sched->create_rq(multiqueue, runqueue);
				this->nof_accelerators++;
			}
		}
	}
	
	//close
	fclose(fp);
}

uss_device_controller::~uss_device_controller()
{
}

int uss_device_controller::get_nof_accelerators()
{
	return nof_accelerators;
}

