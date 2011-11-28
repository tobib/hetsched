/* defines */
#ifndef _HWACCEL_UC_H
#define _HWACCEL_UC_H

#include <vector>
#include <limits.h>

/* include kernel structs */
#include <linux/sched_hwaccel.h>

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
	struct hardware_properties *hp);

/*
 * This function removes a hardware computing device from the scheduler
 * in: id is the scheduler-identifier of the computing unit
 * out: is >0 on success and <=0 on failure
 * If it is below zero then it represents a kernel errno
 */
int delHardwareUnit(unsigned long id);

/*
 * Returns a list of the computing units which the scheduler uses at the moment
 * as vector of computing_unit_shortinfo structs
 * Returns NULL on failure in which case errno can be used to identify the error
 */
std::vector<struct computing_unit_shortinfo> listHardwareUnits(void);

/*
 * This function retrieves detailed information on a specific hardware computing
 * device which is currently used by the scheduler
 * in: id is the scheduler-identifier of the computing unit
 * in/out: Pointer to a struct computing_unit_shortinfo filled by the kernel
 * in/out: Pointer to a struct hardware_properties filled by the kernel
 * If there was an error then the "type" member is set to CU_TYPE_UNDEFINED
 * out: 1 on success (struct has been filled with valid information) and 0 otherwise
 */
int getHardwareDetails(
	unsigned long id, struct computing_unit_shortinfo *cu,
	struct hardware_properties *hp);

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
	struct hardware_properties *hp);

#endif /*_HWACCEL_UC_H*/
