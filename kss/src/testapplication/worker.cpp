/*! \file worker.cpp
 *
 *  \brief Source code (of the non-abstract part) of the base class of all workers
 *
 *  Includes the implementation of all the common methods of the workers, like
 *  their main loop.
 *
 *  Originally written 2010 by Tobias Wiersema
 *
 */

/* Project includes */
#include "worker.h"
#include "debug.h"

/* System includes */
// for syscalls
#include <syscall.h>
#include <unistd.h>
#include <errno.h>
// for errormessages
#include <string.h> 
#include <limits.h> 

/* The syscall function from syscall.h needs to know the syscall numbers of the
 * extension, as defined in
 * kernel_sources/arch/x86/include/asm/unistd_32.h or
 * kernel_sources/arch/x86/include/asm/unistd_64.h
 * depending on the architecture of the target system.
 * For simplicity the numbers are copied here from the corresponding files.
 * In theory it should also be possible to include the files here.
 *
 * NOTE:
 * These numbers _MUST_ match the numbers of the kernel when running this
 * program, otherwise none of the new system calls will work, and hence the
 * whole program cannot run correctly.
 */
#ifdef __amd64__
#define __NR_computing_unit_alloc      299
#define __NR_computing_unit_rerequest  300
#define __NR_computing_unit_free       301
#elif defined __i386__
#define __NR_computing_unit_alloc      337
#define __NR_computing_unit_rerequest  338
#define __NR_computing_unit_free       339
#endif

/**
 * \brief  Syscall wrapper for system call: computing_unit_alloc
 *
 *         Executes the new system call computing_unit_alloc, which instructs
 *         the kernel to assign a computing unit to the calling thread. The call
 *         will block until a matching unit is available.
 *         
 *         Will end the program if there are errors executing the syscall.
 *
 *         For the documentation of the parameter structs see kernel sources:
 *         /include/linux/sched_hwaccel.h
 *
 * \param [in]      mi      struct containing meta information about the caller
 *                          to aid the kernel in the selection of an appropriate
 *                          computing unit 
 * \param [in,out]  cu      struct which is filled by the kernel with
 *                          information about the assigned computing unit
 * \param [in]   workerid   internal id of the caller, used in all outputs of
 *                          the function
 * \return                  boolean indicating the success of the call
 */
bool cu_alloc(struct meta_info *mi, struct computing_unit_shortinfo *cu, int workerid)
{
	signed long status;
#ifdef KERNEL_GRANULARITY
	status = syscall(__NR_computing_unit_alloc, mi, cu, KERNEL_GRANULARITY);
#else
	status = syscall(__NR_computing_unit_alloc, mi, cu);
#endif
	SYSCALL_ANNOUNCE(workerid, DBG_FINEST, "alloced");
	if (status < 0)
	{
		stringstream	stm("");
		char msg[256];
		stm << "Error allocating computing unit.\n     " << strerror_r(errno, msg, sizeof(msg));
		SYSCALL_ERROR(workerid, DBG_ERROR, stm.str());
	}
	return (status == 0);
}

/**
 * \brief  Syscall wrapper for system call: computing_unit_rerequest
 *
 *         Executes the new system call computing_unit_rerequest, which requests
 *         another slice of execution time (from one checkpoint to another) from
 *         the kernel.
 *         
 *         Will end the program if there are errors executing the syscall.
 *
 * \param [in]   workerid   internal id of the caller, used in all outputs of
 *                          the function
 * \return                  boolean indicating if the application may continue
 *                          to work on the computing unit
 */
bool cu_rerequest(int workerid)
{
	signed long status;
	status = syscall(__NR_computing_unit_rerequest);
	SYSCALL_ANNOUNCE(workerid, DBG_FINEST, "rerequested");
	if (status < 0)
	{
		stringstream	stm("");
		char msg[256];
		stm << "Error rerequesting computing unit.\n     " << strerror_r(errno, msg, sizeof(msg));
		SYSCALL_ERROR(workerid, DBG_ERROR, stm.str());
	}
	return (status == 0);
}

#include <sys/time.h>
#include <iomanip>
/**
 * \brief  Helper function to measure the time span spent in the "free" system
 *         call as an approximation to the load balancer running time.
 *
 *         Subtracts the `struct timeval' values X and Y,	storing the result in
 *         RESULT. Returns 1 if the difference is negative, otherwise 0. Y is
 *         modified in the process, carrying full seconds from the microseconds
 *         counter to the seconds counter.
 *
 *         Taken from
 *         http://www.gnu.org/s/libc/manual/html_node/Elapsed-Time.html
 *         
 * \param [out]    result   timeval struct containing the difference of x and y
 * \param [in,out]      x   the minuend timeval struct
 * \param [in,out]      y   the subtrahend timeval struct
 * \return                  1 if the difference is negative, otherwise 0
 */
int
timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y)
{
 /* Perform the carry for the later subtraction by updating y. */
 if (x->tv_usec < y->tv_usec) {
	 int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
	 y->tv_usec -= 1000000 * nsec;
	 y->tv_sec += nsec;
 }
 if (x->tv_usec - y->tv_usec > 1000000) {
	 int nsec = (x->tv_usec - y->tv_usec) / 1000000;
	 y->tv_usec += 1000000 * nsec;
	 y->tv_sec -= nsec;
 }

 /* Compute the time remaining to wait.
		tv_usec is certainly positive. */
 result->tv_sec = x->tv_sec - y->tv_sec;
 result->tv_usec = x->tv_usec - y->tv_usec;

 /* Return 1 if result is negative. */
 return x->tv_sec < y->tv_sec;
}

/**
 * \brief  Syscall wrapper for system call: computing_unit_free
 *
 *         Executes the new system call computing_unit_free, which releases the
 *         computing unit which was previously locked using the "alloc" syscall.
 *         
 *         Will end the program if there are errors executing the syscall.
 *
 * \param [in]   workerid   internal id of the caller, used in all outputs of
 *                          the function
 * \return                  boolean indicating the success of the call
 */
bool cu_free(int workerid)
{
	struct timeval start, stop, diff;
	
	signed long status;
  /* time the system call */
	gettimeofday(&start, 0);
	status = syscall(__NR_computing_unit_free);
	gettimeofday(&stop, 0);
	SYSCALL_ANNOUNCE(workerid, DBG_FINEST, "freed");
	if (status < 0)
	{
		stringstream	stm("");
		char msg[256];
		stm << "Error freeing computing unit.\n     " << strerror_r(errno, msg, sizeof(msg));
		SYSCALL_ERROR(workerid, DBG_ERROR, stm.str());
	}
	
	/* output the timings, if enabled */
  timeval_subtract (&diff, &stop, &start);
	stringstream	stm("");
	stm << "Seconds spent in free: " << diff.tv_sec << "." << std::setfill('0') << std::setw(6) << diff.tv_usec;
	LOAD_BALANCER_TIMING(workerid, DBG_FINE, stm.str());
	
	return (status == 0);
}

/**
 * \brief  Constructor using an id
 *
 *         Constructs a new instance of the "Worker" class. Since this class
 *         contains pure virtual functions, this constructor can only be used as
 *         "super" constructor by derived classes.
 *
 * \param [in]         id   internal id of the new worker
 * \return                  new worker instance
 */
Worker::Worker(int id){
	int retVal; 
	this->initialized = false;
	this->id = id;

	/* Ensure correct detach state (non-detached aka joinable) */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	/* create the actual working pthread */
	isRunning = true;
	retVal = pthread_create(&myThread, &attr, this->thread_entry, this);
	isRunning = (retVal == 0);
	isJoined = !isRunning;
	/* as this is the exit condition do not set this to true until AFTER the work */
	isDone = !isRunning;
	
}

/**
 * \brief  Destructor
 *
 *         Cleans up and ensures the end of the corresponding pthread by setting
 *         the exit flag.
 */
Worker::~Worker(){
	this->isRunning = false;
	/* destroy thread related objects */
	pthread_attr_destroy(&attr);
}

/**
 * \brief  Thread entry for the pthread of this worker
 *
 *         This thread performs the work of this worker by starting it's main
 *         loop (startWorking).
 *
 * \param [in] myContainer  pointer to the instance of the worker class which
 *                          serves as the data container for this pthread
 * \return                  pointer to the pthread
 */
void*	Worker::thread_entry(void* myContainer){
	Worker*		myDataContainer = (Worker*) myContainer;

	myDataContainer->startWorking();
	pthread_exit(NULL);
}

/**
 * \brief  Main work loop of the worker
 *
 *         This main loop is executed in the separate pthread of the worker. It
 *         uses an infinite loop that implements the
 *         "alloc -> rerequest -> free" cycles which have to be performed for
 *         the accelerator-timesharing kernel. In each stage it calls function
 *         pointers which have to be provided by the derived classes. With them,
 *         the loop looks as follows:
 *
 *         -# \ref cu_alloc "syscall alloc"
 *         -# initialize accelerator (*)
 *         -# Perform work
 *           -# compute to the next checkpoint (*)
 *           -# \ref cu_rerequest "syscall rerequest"
 *         -# release accelerator resources (*)
 *         -# \ref cu_free "syscall free"
 *
 *         The whole loop (1-5) runs as long as the computation indicates, that
 *         there is still work left. The inner loop (a-b) runs, as long as the
 *         kernel grants the rerequests. The functions marked with (*) have to
 *         be provided by the derived classes. See #getImplementationFor for
 *         details.
 */
void Worker::startWorking(){
	stringstream	stm("");
	pthread_t		myself = pthread_self();
	bool success;
	/* cooperative multitasking */
	struct computing_unit_shortinfo cu;
	cu.handle = CU_INVALID_HANDLE;
	accelerated_functions_t af;
	af.initialized = false;
	signed long workstatus = true;
	unsigned long cu_cycles = 0;
	unsigned long gpu_cycles = 0;
	unsigned long cu_rerequests = 0;
	
	/* The contructor of our data container is being executed by the main thread, 
   * which means that it is not necessarily finished by now.
	 * If we call workerMetaInfo too early, we could be calling the pure virtual
   * method of the super class instead. Hence we wait here until the correct
   * version is ready.
	 */
	while (!this->initialized)
		pthread_yield();
	/* One more to better the odds that we are not between the last line of the
	 * constructor and its actual ending...
	 */
	pthread_yield();
	
	/* Collect the meta info from the derived class */
  struct meta_info mi;
	workerMetaInfo(&mi);

	stm << "Worker started";
	WORKER_STATUS(id, DBG_FINEST, stm.str()); stm.str("");
	/* Run main loop while work is left and main thread is not exiting */
	while (workstatus && isRunning){
		this->initialized = 2;
		/* Wait for Scheduler to assign us a computing unit - this is blocking */
		success = cu_alloc(&mi, &cu, id);
		
		stm << "Allocated a " << cu_type_to_const_char(cu.type);
		WORKER_CU(id, DBG_FINER, stm.str()); stm.str("");
		
		/* Once alloc returned, check if we shall stop */
		if (!success || !isRunning) break;
		
		/* Count the times we allocated a cu */
		cu_cycles++;
		if (cu.type == CU_TYPE_CUDA)
			gpu_cycles++;
		
		
		/* At this point we have a unit to work on, get the appropriate
     * implementation as function pointers from the derived class
     */
		getImplementationFor(cu.type, &af);
		/* call the functions in a cooperative mt loop */
		if (af.initialized) {
			/* allocate and copy resources to hardware */
			workingresources = (this->*af.init)(&cu);
			/* cooperative multitasking:
			 * occupy hardware as long as we may and there is work left
			 */
			cu_rerequests = 0;
			do {
				cu_rerequests++;
				(this->*af.main)(&cu, workingresources, &workstatus);
				if (workstatus)
					success = cu_rerequest(id);
				else success = workstatus;
			} while (workstatus && success && isRunning);
			cu_rerequests -= 1;
			stm << "   Successfully rerequested a " << cu_type_to_const_char(cu.type) <<  " " << cu_rerequests << " times";
			if (workstatus > 0)
				stm << " (" << workstatus << ")";
			WORKER_AFTER_LAST_REREQUEST(id, DBG_FINE, stm.str()); stm.str("");
			/* free the resources / copy them back to main memory */
			(this->*af.free)(&cu, workingresources);
			workingresources = NULL;
			/* free the computing unit for the scheduler */
			cu_free(id);
			cu.handle = CU_INVALID_HANDLE;
		}
		/* reset previous information */
		af.initialized = false;
	}
	/* free resources */
	if (cu.handle != CU_INVALID_HANDLE)
	{
		if (workingresources && af.initialized)
			(this->*af.free)(&cu, workingresources);
		cu_free(id);
	}
	stm << "Worker stopped (reallocated a cu " << cu_cycles << " times, got a gpu "<< gpu_cycles << " times)";
	WORKER_STATUS(id, DBG_INFO, stm.str()); stm.str("");
  /* Signal the main thread that we are done */
	isDone = true;
}

/**
 * \brief  Checks if the worker has completed it's work
 *
 *         Returns true if the worker has no work left to be done.
 *
 * \return                  True, if the worker is ready
 */
bool Worker::workIsDone()
{
	return isDone;
}

/**
 * \brief  Interrupts the worker and initiates a clean shutdown
 *
 *         Instructs the worker to exit it's pthread at the next iteration of
 *         the main loop, or after the next return from the alloc syscall.
 */
void Worker::shutdown(){
  isRunning = false;
}

/**
 * \brief  Joins the pthread of this worker
 *
 *         Calls pthread_join, so that the pthread of the worker will be joined
 *         to the calling thread. Initiates a shutdown of the pthread, if that
 *         has not been done before.
 */
void Worker::join(){
  int retVal = 0;

  isRunning = false;
  if (!isJoined){
    retVal = pthread_join(myThread, NULL);
  }
  isJoined = (retVal == 0);
  
}

/**
 * \brief  Returns the pthread of this worker
 *
 * \return                  The pthread of this worker
 */
pthread_t Worker::thisThread()
{
	return this->myThread;
}
