#include "hwaccel_uc.h"

#include <cuda.h>
#include <cuda_runtime_api.h>

#include <vector>
#include <iostream>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>

#define DEVICE_NOT_FOUND 1
#define NO_DEVICES_FOUND 2
#define WRONG_OPTION_ARG 3
#define SYSCALL_FAILED   4
#define DELAYED_DELETION 5

using namespace std;

vector<cudaDeviceProp> list_of_cuda_devices;
#define for_each_vector_entry(it, vector) for (int it=0; it < vector.size(); it++)

static int verbose_flag;
static int force_flag;

// trim from start
static inline std::string &ltrim(std::string &s) {
	size_t found = s.find_first_not_of(" \t\r\n");
	if (found!=string::npos)
	{
		s.erase(s.begin(), s.begin() + found);
	}
	return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
	size_t found = s.find_last_not_of(" \t\r\n");
	if (found!=string::npos)
	{
		s.erase(s.begin() + found + 1, s.end());
	}
	return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
	return ltrim(rtrim(s));
}

void get_all_cuda_properties(void)
{
	int num_devices = 0;
	int device = 0;
	list_of_cuda_devices.clear();
	cudaGetDeviceCount(&num_devices);
	if (num_devices > 0) {
		for (device = 0; device < num_devices; device++) {
			cudaDeviceProp properties;
			cudaGetDeviceProperties(&properties, device);
			list_of_cuda_devices.push_back(properties);
		}
	}
}

/*
 * Checks if the given device number exists for the CUDA API and if not, asks the user to
 * choose a valid device from a list. If the user aborts this or if no CUDA devices are present
 * in the system, the function exits.
 * 
 * @device  A device number for use with the cuda API or a number < 0 to let the user choose one
 * @message A message which is being displayed to the user above the list from which he has to choose
 * 
 * Returns:
 *   A valid device number for the CUDA API, if the function returns at all
 */
int check_cuda_device(int deviceNumber, const char *message)
{
	int device = -1;
	
	get_all_cuda_properties();
	int devices = list_of_cuda_devices.size();
	
	if (devices <= 0) {
		cerr << "No CUDA devices found via CUDA runtime API" << endl;
		exit(NO_DEVICES_FOUND);
	}
	if (deviceNumber >= devices) {
		cerr << "Invalid device number '" << deviceNumber << "', please use a number between 0 and " << (list_of_cuda_devices.size() - 1) << endl;
		exit(DEVICE_NOT_FOUND);
	}
	if (deviceNumber < 0) {
		cout << message << endl;
		for_each_vector_entry(i, list_of_cuda_devices) {
			cudaDeviceProp devProp = list_of_cuda_devices[i];
			cout << " Device " << i << ", '" << devProp.name << "' (" << devProp.multiProcessorCount << "MP / " << (long)(devProp.totalGlobalMem/1024/1024) << "MB / CC " << devProp.major << "." << devProp.minor << ")" << endl;
		}
		while (device < 0) {
			cout << "Press  ctrl+c to abort" << endl;
			if (!(cin >> device)) {
				string badChars;
				cin.clear();
				cin >> badChars;
			}
			if (device >= 0 && device < list_of_cuda_devices.size())
				break;
			cout << "Invalid input, please enter a number between 0 and " << (devices - 1) << endl;
			device = -1;
		}
	} else device = deviceNumber;
	
	return device;
}

/*
 * Retrieves all currently known computing units in the system. If there is an error with the syscall,
 * the function exits.
 * 
 * Returns:
 *   All compuing units currently known to the scheduler, if the function returns at all
 */
std::vector<struct computing_unit_shortinfo> get_all_cus()
{
	errno = 0;
	std::vector<struct computing_unit_shortinfo> units = listHardwareUnits();
	if (errno == ENODEV) {
		cout << "Currently there are no computing units known to the scheduler" << endl;
		cout << "The hardware scheduling feature is therefore effectively disabled" << endl;
	} else if (errno) {
		cerr << "Error: Failed to get computing unit list from the scheduler." << endl;
		perror("Kernel error from listHardwareUnits");
		cerr << endl;
		exit(SYSCALL_FAILED);
	}

	return units;
}

/*
 * Displays a list of all computing units of type "type" with header "message".
 * 
 * @units   A vector filled by the kernel with all the computing units
 * @type    A computing unit type as string, or "" for all devices
 * @message A message which is being displayed to the user above the list
 * 
 * Returns:
 *   The number of displayed computing unit handles, if the function returns at all
 */
unsigned long display_computing_units(std::vector<struct computing_unit_shortinfo> units, string type, const char *message)
{
	type = trim(type);
	cout << message;
	if (!type.length() == 0)
		cout << " (filter '" << type << "')";
	cout << endl;
	
	if (!units.size()) {
		cout << "Currently the scheduler knows about no computing units." << endl;
		return 0;
	}
	
	unsigned long handles = 0;
	for_each_vector_entry(i, units) {
		computing_unit_shortinfo unit = units[i];
		/* if a type is given then skip devices of other types */
		string unittype = string(cu_type_to_const_char(unit.type));
		if (!type.length() == 0 && type.compare(ltrim(unittype)) != 0)
			continue;
		cout << " Unit " << unit.handle << " of type " << unittype << " with " << unit.count << " (+" << unit.waiting << ") tasks";
		if (!unit.online)
			cout << " (is being deleted)";
		cout << endl;
		handles++;
	}
	if (!handles)
		cout << units.size() << " devices found, but none of type " << type << endl;
	
	return handles;
}

/*
 * Checks if the given handle is a valid handle in the scheduler extension for hardware scheduling.
 * If it is not, the user will be presented with a list of all currently known devices in the system
 * to choose one of them. If the user aborts this or if no CUDA devices are present in the system,
 * the function exits.
 * 
 * @handle  A computing unit handle or CU_INVALID_HANDLE to let the user choose one
 * @message A message which is being displayed to the user above the list from which he has to choose
 * 
 * Returns:
 *   A valid computing unit handle, if the function returns at all
 */
unsigned long check_handle(unsigned long arg_handle, const char *message)
{
	unsigned long handle = CU_INVALID_HANDLE;
	
	if (arg_handle == CU_INVALID_HANDLE) {
		/* no handle given: ask for handle */
		while (handle == CU_INVALID_HANDLE) {
			std::vector<struct computing_unit_shortinfo> units = get_all_cus();
			unsigned long handles = display_computing_units(units, "", message);
			if (handles <= 0) {
				cerr << "No computing units found, exiting." << endl;
				exit(NO_DEVICES_FOUND);
			}
			cout << "Press ctrl+c to abort" << endl;
			if (!(cin >> handle)) {
				string badChars;
				cin.clear();
				cin >> badChars;
			}
			if (getHardwareDetails(handle, NULL, NULL))
				break;
			cout << "Invalid input, please enter a number from the list" << endl << endl;
			handle = CU_INVALID_HANDLE;
		}
	} else {
		/* handle given: check if the handle exists */
		if (getHardwareDetails(arg_handle, NULL, NULL))
			handle = arg_handle;
		else {
			cerr << "Invalid computing unit handle '" << arg_handle << "'" << endl;
			exit(DEVICE_NOT_FOUND);
		}
	}
	
	return handle;
}

/*
 * Adds the cuda device to the scheduler. If device is < 0 the user will be
 * asked to choose the device from a list
 * 
 * @device  A device number for use with the cuda API
 */
void add_cuda_device(int deviceNumber)
{
	int device = check_cuda_device(deviceNumber, "Please select a CUDA device to add to the hardware scheduler");
	cudaDeviceProp devProp = list_of_cuda_devices[device];
	
	struct hardware_properties hp;
	/* hp.concurrent_kernels = devProp.concurrentKernels; */
	/* TODO: Deactivated concurrent kernels for now, as they require a shared cuda context */
	hp.concurrent_kernels = 1;
	hp.memory = devProp.totalGlobalMem;
	
#define NUM_TESTS 5
	// TODO: Add test that this cannot exceed the number of total available memory of the gpu
	unsigned long mb_per_sec[NUM_TESTS];
	size_t sizes_in_kb[NUM_TESTS] = {1024, 10 * 1024, 10 * 1024, 50 * 1024, 100 * 1024};
	for (unsigned int i = 0; i < NUM_TESTS; ++i) {
		/* new set of vars */
		float *memory, *device_memory, time;
		cudaEvent_t start, stop;
		size_t size = sizes_in_kb[i] * 1024 * sizeof(float);
		
		if (size > hp.memory)
			continue;
		
		/* allocate both memory regions */
		memory = (float *)malloc(size);
		cudaMalloc((void **)&device_memory, size);
		
		/* setup the events */
		cudaEventCreate(&start);
		cudaEventCreate(&stop);
		
		/* time the copies */
		cudaEventRecord(start, 0);
		cudaMemcpy(device_memory, memory, size, cudaMemcpyHostToDevice);
		cudaEventRecord(stop, 0);
		cudaEventSynchronize(stop);
		cudaEventElapsedTime(&time, start, stop);
		mb_per_sec[i] = (unsigned long)((float)sizes_in_kb[i] / 1024) / (time / 1000);
		
		cudaEventRecord(start, 0);
		cudaMemcpy(memory, device_memory, size, cudaMemcpyDeviceToHost);
		cudaEventRecord(stop, 0);
		cudaEventSynchronize(stop);
		cudaEventElapsedTime(&time, start, stop);
		
		mb_per_sec[i] = (mb_per_sec[i] +
		                (unsigned long)((float)sizes_in_kb[i] / 1024) / (time / 1000))
										/ 2;
		
		/* free the regions and events */
		cudaEventDestroy(start);
		cudaEventDestroy(stop);
		free(memory);
		cudaFree(device_memory);
	}

	unsigned long sum = 0;
	for (unsigned int i = 0; i < NUM_TESTS; ++i)
		sum += mb_per_sec[i];
	
	hp.bandwidth = (unsigned long) sum/NUM_TESTS;
#undef NUM_TESTS
	
	errno = 0;
	unsigned long handle = addHardwareUnit(CU_TYPE_CUDA, (unsigned long) device, &hp);
	if (errno || handle == CU_INVALID_HANDLE) {
		cerr << "Error: Failed to add device number " << device << " to the scheduler." << endl;
		perror("Kernel error from addHardwareUnit");
		cerr << endl;
		exit(SYSCALL_FAILED);
	}
	cout << "Successfully added CUDA device number " << device << " to the scheduler with handle " << handle << endl;
	cout << "  Stats: " << hp.concurrent_kernels << " conurrent kernels, " << (unsigned long)(hp.memory / 1024 / 1024) << " MB memory, " << hp.bandwidth << " MB/sec bandwidth." << endl;
}

/*
 * Removes a device from the scheduler. TODO:If device is < 0 the user will be
 * asked to choose the device from a list
 * 
 * @handle  A handle to the device which should be deleted...
 */
void del_device(long arg_handle)
{
	unsigned long handle;
	if (arg_handle < 0)
		handle = CU_INVALID_HANDLE;
	else
		handle = (unsigned long) arg_handle;
	
	handle = check_handle(handle, "Please select a device to remove from the hardware scheduler");
	errno = 0;
	int status = delHardwareUnit(handle);
	if (errno == EBUSY || status == -EBUSY) {
		cout << "The computing unit is currently busy and will be deleted once there are no tasks" << endl;
		cout << "left on the device. All further allocs or rerequests of the unit will be denied." << endl;
		exit(DELAYED_DELETION);
	} else if (errno || status < 0) {
		cerr << "Error: Failed to remove device with handle " << handle << " from the scheduler." << endl;
		perror("Kernel error from delHardwareUnit");
		cerr << endl;
		exit(SYSCALL_FAILED);
	}
	cout << "Successfully removed device with handle " << handle << " from the scheduler." << endl;
}

/*
 * Lists the devices used by the scheduler
 * 
 * @handle  A handle to the device which should be deleted...
 */
void list_devices(string type)
{
	std::vector<struct computing_unit_shortinfo> units = get_all_cus();
	display_computing_units(units, type, "List of computing units currently known to the scheduler");
}

/*
 * Sets the number of cpu slots the scheduler uses for the ghost threads
 * 
 * @count   Number of cpu slots to use in the scheduler
 */
void set_cpu_count(int count)
{
	struct hardware_properties hp;
	unsigned long cpudevice = CU_INVALID_HANDLE;
	errno = 0;
	
	/* Search for the cpu device */
	std::vector<struct computing_unit_shortinfo> units = get_all_cus();
	for_each_vector_entry(i, units) {
		if (units[i].type == CU_TYPE_CPU) {
			cpudevice = units[i].handle;
			break;
		}
	}
	
	if (cpudevice == CU_INVALID_HANDLE) {
		/* Readd cpu device if it has been removed */
		cout << "CPU device could not be found, readding it with right count." << endl;
	
		/* setup the hardware_properties to reflect the new count */
		hp.concurrent_kernels = count;
		cpudevice = addHardwareUnit(CU_TYPE_CPU, 0, &hp);
		if (errno || cpudevice == CU_INVALID_HANDLE) {
			cerr << "Error: Failed to add the CPU device to the scheduler." << endl;
			perror("Kernel error from addHardwareUnit");
			cerr << endl;
			exit(SYSCALL_FAILED);
		}
	
	} else {
		/* Reset the count of the cpu device if it was found */
		struct computing_unit_shortinfo cu;
		if (!getHardwareDetails(cpudevice, &cu, &hp)) {
			cerr << "Error: The reported CPU device handle " << cpudevice << " seems to be invalid." << endl;
			perror("Kernel error from getHardwareDetails");
			cerr << endl;
			exit(SYSCALL_FAILED);
		}
		
		/* We have the current information and the new one available... Do reset. */
		hp.concurrent_kernels = count;
		int success = setHardwareUnit(cpudevice, &cu,	&hp);
		if (errno || !success) {
			cerr << "Error: Failed to set the new count for the CPU device." << endl;
			perror("Kernel error from setHardwareUnit");
			cerr << endl;
			exit(SYSCALL_FAILED);
		}
	}
	cout << "The CPU count has been successfully reset to " << count << endl;
}





int main(int argc, char **argv)
{
	int c;
	unsigned char action = 0;
	string actionarg_s = "";
	long actionarg_l = -1;

	while (1)
	{
		static struct option long_options[] =
		{
			/* These options set a flag. */
			{"verbose", no_argument,       &verbose_flag, 1},
			{"brief",   no_argument,       &verbose_flag, 0},
			{"force",   no_argument,       &force_flag,   1},
			/* These options don't set a flag.
			We distinguish them by their indices. */
			{"cuda",       optional_argument, 0, 'c'},
			
			{"fpga",       optional_argument, 0, 'f'},

			{"cpu",        required_argument, 0, 'C'},
			
			{"list",       optional_argument, 0, 'l'},
			{"del",        optional_argument, 0, 'd'},
			{0, 0, 0, 0}
		};
		/* getopt_long stores the option index here. */
		int option_index = 0;

		c = getopt_long (argc, argv, "c::f::C:l::d::",
		long_options, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c)
		{
			case 0:
				/* If this option set a flag, do nothing else now. */
				if (long_options[option_index].flag != 0)
					break;
				cout << "option " << long_options[option_index].name;
				if (optarg)
					cout << " with arg " << optarg;
				cout << endl;
				break;

			case 'c':
			case 'f':
			case 'C':
			case 'd':
				/* Action to be taken with numeric argument */
				action = (unsigned char)c;
				if (optarg) {
					char *endptr;
					actionarg_l = strtol(optarg, &endptr, 10);
					if ((errno == ERANGE && (actionarg_l == LONG_MAX || actionarg_l == LONG_MIN))
							|| (errno != 0 && actionarg_l == 0)) {
						perror("strtol");
						exit(WRONG_OPTION_ARG);
					}
				}
				break;

			case 'l':
				/* Action to be taken with string argument */
				action = (unsigned char)c;
				if (optarg)
					actionarg_s = string(optarg);
				break;

			case '?':
				/* getopt_long already printed an error message. */
				break;

			default:
				cout << "Found unknown option " << c;
				if (optarg)
					cout << " with arg " << optarg;
				cout << endl;
		}
	}
	
	/* Command line parsing complete, do something */
	cout <<  "Userspace control CLI frontend for hardware scheduler" << endl << endl;
	
	if (action == 0) {
		cout << "Nothing to do." << endl;// << "Try 'accelerator-ctl --help' for usage instructions." << endl << endl;
		exit(0);
	}
	
	switch (action)
	{
		case 'c':
			/* cuda add */
			add_cuda_device(actionarg_l);
			break;
		case 'f':
			/* fpga add */
			break;
		case 'C':
			/* set cpu count */
			set_cpu_count((int)actionarg_l);
			break;
		case 'l':
			/* list scheduler devices */
			list_devices(actionarg_s);
			break;
		case 'd':
			/* remove scheduler device */
			del_device(actionarg_l);
			break;
	}
	
	
	cout << endl;
	exit (0);

	/* Print any remaining command line arguments (not options). */
	/*
	if (optind < argc)
	{
		cout << "non-option ARGV-elements: ";
		while (optind < argc)
			cout << argv[optind++] << " ";
		cout << endl;;
	}
	*/
}
