#include "hwaccel_uc.h"

/* include syscalls */
#include <syscall.h>
#include <unistd.h>
#include <errno.h>

/* define syscall numbers */
#ifdef __amd64__
#define __NR_computing_unit_add			302
#define __NR_computing_unit_del			303
#define __NR_computing_unit_iterate			304
#define __NR_computing_unit_details			305
#define __NR_computing_unit_set   			306
#elif defined __i386__
#define __NR_computing_unit_add			340
#define __NR_computing_unit_del			341
#define __NR_computing_unit_iterate			342
#define __NR_computing_unit_details			343
#define __NR_computing_unit_set   			344
#endif

/*
 * This function adds a hardware computing unit to the scheduler
 * in: type is one of the CU_TYPE_* defines in linux/sched_hwaccel.h
 * in: api_device_number is the device number which the unit specific
 *		api uses to identifiy this device
 * in: hardware_properties are the properties of this device, s.a. bandwidth
 * out: handle with which the scheduler henceforth identifies the unit
 */
unsigned long addHardwareUnit(
	unsigned int type, unsigned long api_device_number,
	struct hardware_properties *hp)
{
	signed long status;
	
	/* Fill the parameter struct for the kernel */
	struct computing_unit_shortinfo cu;
	cu.type = type;
	cu.api_device_number = api_device_number;
	
	/* Execute the kernel syscall */
	status = syscall(__NR_computing_unit_add, &cu, hp);
	
	/* Interpret and relay results */
	if (status < 0)
		return CU_INVALID_HANDLE;
	return cu.handle;
}

/*
 * This function removes a hardware computing device from the scheduler
 * in: id is the scheduler-identifier of the computing unit
 * out: is >0 on success and <=0 on failure
 * If it is below zero then it represents a kernel errno
 */
int delHardwareUnit(unsigned long id)
{
	signed long status;
	
	/* Execute the kernel syscall */
	status = syscall(__NR_computing_unit_del, id);
	
	/* Interpret and relay results */
	if (status < 0)
		return errno;
	return true;
}

void cusi_copy(struct computing_unit_shortinfo *src, struct computing_unit_shortinfo *dst)
{
	/* TODO: Expand this as more information is added to the struct */
	dst->handle = src->handle;
	dst->type = src->type;
	dst->api_device_number = src->api_device_number;
	dst->count = src->count;
	dst->waiting = src->waiting;
}
/*
 * Returns a list of the computing units which the scheduler uses at the moment
 * as vector of computing_unit_shortinfo structs
 * Returns NULL on failure in which case errno can be used to identify the error
 */
std::vector<struct computing_unit_shortinfo> listHardwareUnits(void)
{
	signed long status;
	std::vector<struct computing_unit_shortinfo> retVect;
	
	/* Variables to iterate over all devices */
	unsigned long iterator = 0;
	unsigned long nr_devices = 0;
	unsigned long devices = 0;
	
	/* Execute the kernel syscall to initialize iteration */
	status = syscall(__NR_computing_unit_iterate, &iterator, NULL, &nr_devices);
	/* Interpret and relay results */
	if (status < 0) {
		retVect.clear();
		return retVect;
	}
	
	/* iterator is now set to the first valid id, or to CU_INVALID_HANDLE if there is none */
	while (iterator != CU_INVALID_HANDLE) {
		struct computing_unit_shortinfo cu;
		/* Execute the kernel syscall */
		status = syscall(__NR_computing_unit_iterate, &iterator, &cu, &nr_devices);
		/* Check for errors */
		if (status < 0) {
			/* This is no failure if we were ready */
			if (nr_devices != devices) retVect.clear();
			return retVect;
		}
		/* This is needed to handle the case where the last device is removed during this iteration */
		if (cu.handle == CU_INVALID_HANDLE)
			break;
		
		/* Add the temporary struct to the vector */
		devices++;
		retVect.push_back(cu);
	}
	
	return retVect;
}

/*
 * This function retrieves detailed information on a specific hardware computing
 * device which is currently used by the scheduler
 * in: id is the scheduler-identifier of the computing unit
 * in/out: Pointer to a struct computing_unit_info filled by the kernel
 * If there was an error then the "type" member is set to CU_TYPE_UNDEFINED
 * out: true on success (struct has been filled with valid information) and 0 otherwise
 */
int getHardwareDetails(
	unsigned long id, struct computing_unit_shortinfo *cu,
	struct hardware_properties *hp)
{
	signed long status;
	
	/* Execute the kernel syscall */
	status = syscall(__NR_computing_unit_details, id, cu, hp);
	
	/* Interpret and relay results */
	return (status == 0);
}

/*
 * This function resets the hardware properties of an already added computing unit
 * available to the scheduler.
 * in: id is the scheduler-identifier of the computing unit
 * in: Pointer to a struct computing_unit_shortinfo with the new properties
 * in: Pointer to a struct hardware_properties with the new properties
 * out: 1 on success (properties have been reset) and 0 otherwise
 */
int setHardwareUnit(
	unsigned long id, struct computing_unit_shortinfo *cu,
	struct hardware_properties *hp)
{
	signed long status;
	
	/* Execute the kernel syscall */
	status = syscall(__NR_computing_unit_set, id, cu, hp);
	
	/* Interpret and relay results */
	return (status == 0);
}
