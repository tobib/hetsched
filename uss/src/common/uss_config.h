#ifndef CONFIG_INCLUDED
#define CONFIG_INCLUDED

//basic
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

//socket
#include <sys/socket.h>
#include <sys/un.h>
//memeset
#include <string.h>

//read and write
#include <unistd.h>

//integers
#include <stdint.h>

//pthread and signal
#include <pthread.h>
#include <signal.h>

//string
#include <string.h>

//time
#include <time.h>
#include <sys/time.h>

//pid
#include <sys/types.h>

//errno
#include <errno.h>


//maximum length of a string
#define MAX_STRING_LEN 100
//define half lenght to avoid calculations
#define MAX_STRING_LEN_2 50


/***************************************\
* basic setup							*
\***************************************/
/*
 * communication method
 * WARNING: select only one!
 */
#define USS_FIFO 1
#define USS_RTSIG 0 

#if(USS_RTSIG == 1)
#include <sys/signalfd.h>
#endif

/*
 * DEBUG
 * can be chosen individually (the first is for files in common folder)
 */
#define USS_DEBUG 0
#define USS_DAEMON_DEBUG 0
#define USS_LIBRARY_DEBUG 0

/*
 * advanced debugging
 */
#define USS_FILE_LOGGING 0


/***************************************\
* bechmarks								*
\***************************************/
#define BENCHMARK_DAEMON_CPUTIME 0
#define BENCHMARK_REGISTRATION_TIME 0
#define BENCHMARK_CONTEXTSWITCH_TIME 0
#define BENCHMARK_LOAD_BALANCER 0

/***************************************\
* configuration/features				*
\***************************************/
/*
 * scheduling interval
 */
#define USS_SCHED_INTERVAL_SEC 0
#define USS_SCHED_INTERVAL_NSEC 50000000 //50ms

/*
 * activate to use a file to read in accel specific base granularities 
 * (not yet implemneted)
 */
#define USS_MIN_GRANULARITY_FROM_FILE 0

/*
 * default base granularity
 * WARNING: this is only used if USS_MIN_GRANULARITY_FROM_FILE is 0
 * COMMENT: [micro seconds]
 */
#define USS_MIN_GRANULARITY 1000000 //=1sec

/*
 * bluemode
 * if cpu sysload is low enough, then send inactive handles to cpu instead of idle
 */
#define USS_BLUEMODE 0

/*
 * cpu sysload parameters
 * only if the current sysload is less or equal than the USS_CPU_THRESHOLD
 * more additional threads are allowed onto the CPU (no removing if too high)
 *
 * select to load from /proc filesystem OR per system call
 *
 * an update interval X means that the update is done very Xth iteration of daemon main loop
 */
#define USS_CPU_THRESHOLD 10

#define USS_SYSLOAD_FROM_PROC 0 //WARNING: not yet implemented
#define USS_SYSLOAD_FROM_SYSCALL 1

#define USS_SYSLOAD_UPDATE_INTERVAL 1

#if(USS_SYSLOAD_FROM_SYSCALL == 1)
#include <sys/sysinfo.h>
#endif


/*
 * should the user-space scheduler be ran as a daemon
 */
#define USS_DAEMONIZE 0

/*
 * number of maximal parallel worker threads
 */
#define USS_MAX_LOCAL_THREADS 2048

/*
 * for transporting the meta scheduling information
 * to uss_daemon (in particular to the scheduler itself)
 * a struct of constant size is used
 * -> the struct meta_sched_info is packed into
 *    a new struct meta_sched_info_transport
 */
#define USS_MAX_MSI_TRANSPORT 10

/*
 *push curve is used by push/pull loadbalancing mechanism
 */
#define USS_MAX_PUSH_CURVE_LEN 20

/*
 * limit thread concurrency for registrations
 */
#define USS_NOF_MAX_DISPATCHER_THREADS 30


/***************************************\
* path names							*
\***************************************/
/*
 * unix socket addresses (path names)
 */
#define USS_REGISTRATION_DAEMON_SOCKET "/tmp/uss_daemon_socket"
#define USS_REGISTRATION_MULTIPLEXER_SOCKET "/tmp/uss_multi_socket"

/*
 * the main directory of the USS
 */
#define USS_DIRECTORY "/usr/bin/uss"

/*
 * the complete file path for the device list
 */
#define USS_FILE_DEVICELIST "/home/dwelp/uss/devicelist"

/*
 * folder to create fifo's in
 */
#define USS_FIFO_NAME_TEMPLATE "/tmp/uss/f.%ld"
#define USS_FIFO_NAME_LEN (sizeof(USS_FIFO_NAME_TEMPLATE)+30)


/***************************************\
* other constants						*
\***************************************/
/*
 * error constants
 */
#define USS_ERROR_GENERAL -10
#define USS_ERROR_SCHED_DECLINED_REG -20
#define USS_ERROR_TRANSPORT -30

enum uss_control_entry_status
{
	USS_CONTROL_NOT_PROCESSED = 0,
	USS_CONTROL_SCHED_ACCEPTED = 1,
	USS_CONTROL_SCHED_DECLINED = -1,
	USS_CONTROL_UNREGISTER_PENDING = 2,
	USS_CONTROL_UNREGISTER_SUCCESSFUL = 3
};

enum uss_message_types
{
	USS_MESSAGE_NOT_SET = 0,
	USS_MESSAGE_RUNON = 1,
	USS_MESSAGE_CLEANUP_DONE = 3,
	USS_MESSAGE_STATUS_REPORT = 4,
	USS_MESSAGE_ISFINISHED = 5,
	USS_MESSAGE_RESET = 6
};

/***************************************\
* structs								*
\***************************************/
/*
 * this is used the address a registered client can be
 * communicated with
 */
struct uss_address
{
	pid_t pid;
#if(USS_FIFO == 1)	
	long fifo; /*id of the fifo in addition to base name given in config*/
	
	bool operator==(const uss_address& other) const
	{
		return (pid == other.pid && fifo == other.fifo);
	}
	
	bool operator< (const struct uss_address& a) const
	{
		return (this->pid < a.pid || (this->pid == a.pid && this->fifo < a.fifo));
	}
#elif(USS_RTSIG == 1)
	int lid;
	
	bool operator==(const uss_address& other) const
	{
		return (pid == other.pid && lid == other.lid);
	}
	
	bool operator< (const struct uss_address& a) const
	{
		return (this->pid < a.pid || (this->pid == a.pid && this->lid < a.lid));
	}
#endif
};


/*
 * during an registration attempt, three values are important
 * 1) a check value, that is sizeof(s meta_sched_addr_info) if
 *    everything went corretly or an error message otherwise
 * 2) client
 * 3) daemon
 */
struct uss_registration_response
{
	int check;
	struct uss_address client_addr;
	struct uss_address daemon_addr;
};


/*
 * this is a container for transporting meta_sched_info
 * and the address from library to daemon
 * -> smaller array, because a user will not implement
 *    an algorithm for each accel
 */
struct meta_sched_addr_info
{
	int length;
	int accelerator_type[USS_MAX_MSI_TRANSPORT];
	int affinity[USS_MAX_MSI_TRANSPORT];
	int flags[USS_MAX_MSI_TRANSPORT];
	struct uss_address addr;
	pthread_t tid;
};


/*
 * a uss_message is the abstract form of a message that can be
 * passed in between the library call and daemon/scheduler
 */
struct uss_message
{
	/* header */
#if(USS_FIFO == 1)	
	struct uss_address address;
#elif(USS_RTSIG == 1)
	//the address is implicitly transported by signal and in wrapped_int
#endif	
	int message_type;
	
	/* data */
	int accelerator_type;
	int accelerator_index;
};


#endif

