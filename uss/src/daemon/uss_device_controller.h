#ifndef DEVICE_CONTROLLER_H_INCLUDED
#define DEVICE_CONTROLLER_H_INCLUDED

#include "./uss_scheduler.h"
#include "./uss_daemon.h"

using namespace std;

class uss_device_controller
{
	private:
	uss_scheduler *sched;
	int nof_accelerators;
	
	public:
	uss_device_controller(uss_scheduler *sc);
	~uss_device_controller();
	
	int get_nof_accelerators();
};

#endif
