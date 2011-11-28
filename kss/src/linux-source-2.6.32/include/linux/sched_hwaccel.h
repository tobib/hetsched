#ifndef _LINUX_SCHED_HWACCEL_H
#define _LINUX_SCHED_HWACCEL_H

/* for now not a Kconfig var... */
#define CONFIG_SCHED_HWACCEL

#ifdef CONFIG_SCHED_HWACCEL

/*
 * Note: These types are used as array indizes and therefore have to be
 * in ascending order and without gap
 */
#define CU_NUMOF_TYPES 3

#define CU_TYPE_CPU  0
#define CU_TYPE_CUDA 1
#define CU_TYPE_FPGA 2
#define CU_TYPE_UNDEFINED 9999

#define CU_HW_QUEUE_LIMIT 10
#define CU_HW_LOAD_BALANCER_FILLS_QUEUE
#define CU_HW_KEEP_QUEUE_FULL
//#define FEAT_HWACCEL_DISABLE_CHECKPOINTING
//#define APPLICATION_CONTROLLED_GRANULARITY

#define SINGLE_IMPLEMENTATION_BONUS 40

#define CU_INVALID_HANDLE ULONG_MAX

#define cu_type_to_const_char(type) ((type) == CU_TYPE_CPU ?  "  cpu" : \
                                    ((type) == CU_TYPE_CUDA ? " cuda" : \
                                    ((type) == CU_TYPE_FPGA ? " fpga" : \
                                                              "inval")))

#define CU_AFFINITY_WITH_PAREFFGAIN 1

/* Short summary of all cu-information needed by the application
 */
struct computing_unit_shortinfo {
	unsigned long handle;
	unsigned int type;
	unsigned long api_device_number;
	int count;
	int waiting;
	int online;
};

/* struct for meta information about a thread
 * the scheduler picks an appropriate cu based on this info
 * NOTE: In its current state this is very basic
 * It is therefore subject to change
 */
struct meta_info {
	/*
	 * In MB. The amount of memory which has to be transferred to and from the device.
	 * This will not be considered for the CPU implementations as they can access
	 * the main memory directly.
	 */
	unsigned int memory_to_copy;
	
	/*
	 * In MB. The amount of memory the algorithm typically allocates on the device.
	 */
	unsigned int memory_consumption;
	
	/*
	 * On a scale from 0 (not parallelisable or small problem scale; no speedup expected from parallelisation)
	 * to 5 (completely parallelisable large scale problem)
	 */
	int parallel_efficiency_gain;
	
	/*
	 * Array holding the affinity of the application towards the cu types.
	 * Affinity 0 means that no implementation is available for that type,
	 * otherwise higher values correspond to higher affinity.
	 * The programmer can use this to encode information about which type
	 * of cu has the best implementation for this problem, or only which
	 * implementations are given and which are not.
	 * Do not include parallelisability here as this is being considered by
	 * the scheduler itself.
	 * Valid range: [0-15]
	 */
	unsigned int type_affinity[CU_NUMOF_TYPES];
};

/* 
 * Hardware API fetched details of a specific computing unit
 */
struct hardware_properties {
	int concurrent_kernels;
	unsigned long bandwidth; /* measured as MB per second */
	size_t memory; /* measured in bytes */
	
	unsigned int gflops_per_sec; /* Gigaflops per second */
};

#ifdef __KERNEL__
#ifdef APPLICATION_CONTROLLED_GRANULARITY
asmlinkage long sys_computing_unit_alloc(struct meta_info *mi, struct computing_unit_shortinfo *cu, int base_granularity);
#else
asmlinkage long sys_computing_unit_alloc(struct meta_info *mi, struct computing_unit_shortinfo *cu);
#endif
asmlinkage long sys_computing_unit_rerequest(void);
asmlinkage long sys_computing_unit_free(void);

asmlinkage long sys_computing_unit_add(struct computing_unit_shortinfo *cu, struct hardware_properties *hp);
asmlinkage long sys_computing_unit_del(unsigned long id);
asmlinkage long sys_computing_unit_iterate(unsigned long *iterator, struct computing_unit_shortinfo *cu, unsigned long *nr_devices);
asmlinkage long sys_computing_unit_details(unsigned long id, struct computing_unit_shortinfo *cu, struct hardware_properties *hp);
asmlinkage long sys_computing_unit_set(unsigned long id, struct computing_unit_shortinfo *cu, struct hardware_properties *hp);
#endif /* __KERNEL__ */

#endif /* CONFIG_SCHED_HWACCEL */


#endif
