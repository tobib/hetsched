#ifndef LIBRARY_H_INCLUDED
#define LIBRARY_H_INCLUDED

/*
 * this equals the number of elements in enum
 * uss_supported_accelerators
 * 
 * (!) decrement with care
 */
#define USS_NOF_SUPPORTED_ACCEL 20


/*
 * this enum contains all supported accelerator types
 * for each of them, it is ensured, that they cooperate
 * with the USS
 *
 * (!) set USS_NOF_SUPPORTED_ACCEL correspondingly
 * (!) max value must be smaller than USS_NOF_SUPPORTED_ACCEL
 *
 */
enum uss_supported_accelerators
{
	USS_ACCEL_TYPE_IDLE = 0,
	USS_ACCEL_TYPE_CPU = 1,
	USS_ACCEL_TYPE_CUDA = 4, 
	USS_ACCEL_TYPE_FPGA = 5,
	USS_ACCEL_TYPE_STREAM = 6,
	USS_ACCEL_TYPE_ELEVEN = 9
};


/*
 * to measure the affinity of user code to the supported
 * accelerator types (as listed in uss_supported_accelerators)
 * the programmer of an application has to tell the USS
 * how good his algorithm works on which accelerator
 * 
 * to do so he fills the meta_sched_info structure that holds
 * a list containing an entry for each accelertor the
 * applications support which in turn is a subset of all
 * accelerators supported by the USS
 * 
 * 10: very good affinity
 * 1: bad affinity
 * 0: not supported (default for each accel not in this list)
 *
 */
struct meta_sched_info_element
{
	//int accelerator_type; NOT NEEDED HERE
	int affinity;
	int flags; 
	int (*init)(void*, void*, int);
	int (*main)(void*, void*, int);
	int (*free)(void*, void*, int);
};


/*
 * this is a linked list
 * => the user can fill it without worrying about indexes
 *    or completeness of the list
 */
struct meta_sched_info
{
	struct meta_sched_info_element *ptr[USS_NOF_SUPPORTED_ACCEL];
};

int libuss_fill_msi(struct meta_sched_info *msi, int type, int affinity, int flags, 
					int (*algo_init_function)(void*, void*, int), 
					int (*algo_main_function)(void*, void*, int), 
					int (*algo_free_function)(void*, void*, int));

int libuss_free_msi(struct meta_sched_info *msi);

int libuss_start(struct meta_sched_info *msi, void *md, void *mcp, int *is_finished, int *run_on, int *device_id);

#endif
