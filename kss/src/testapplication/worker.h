/** \file worker.h
 *
 *  \brief Header of the abstract base class of all workers
 *
 *  Defines all common methods and several virtual ones, which need to be
 *  overriden by all descendants.
 *
 *  Originally written 2010 by Tobias Wiersema
 *
 */

#ifndef _WORKER_H
#define _WORKER_H

/* Includes */
#include <pthread.h>
#include <sys/signal.h>
#include <sstream>
#include <vector>

#include "testapp.h"

/* include kernel structs */
#include <linux/sched_hwaccel.h>

//! prepend a 0 to ids < 10 to create two-digit strings
#define formatted_stream_id(id) (id < 10 ? "0" : "") << id 

class Testapp;
class Worker;

typedef void* (initFunc)(computing_unit_shortinfo*);                     //!< Function signature for accelerator initialization functions (allocate and copy resources)
typedef void (mainFunc)(computing_unit_shortinfo*, void*, signed long*); //!< Function signature for computation functions (from one checkpoint to the next)
typedef void (freeFunc)(computing_unit_shortinfo*, void*);               //!< Function signature for accelerator release functions (release all allocated resources)

/**
 * \brief A struct to pass a triplet of function pointers from a subclass to the
 *        superclass
 * 
 * As the superclass Worker contains the main loop, which ensures time-shared
 * usage of the accelerators using the new kernel system calls, but the
 * descendants define the actual work which should be done by the worker, the
 * subclasses have to define callbacks for the superclass. The superclass calls
 * these functions (repeatedly) from the main loop.
 *
 * This struct is used to pass the needed function pointers from the subclass to
 * the superclass.
 */
typedef struct accelerated_functions {
	//! boolean flag, indicating if the function pointers are valid
	bool initialized;
	//! Function pointer to an initFunc, which should allocate computing unit memory and copy resources to it
	initFunc Worker::*init;
	//! Function pointer to a mainFunc, which should run the algorithm on the accelerator from checkpoint to checkpoint
	mainFunc Worker::*main;
	//! Function pointer to a freeFunc, which should copy resources back to main memory and free computing unit memory
	freeFunc Worker::*free;
} accelerated_functions_t;

/**
 * \brief Abstract base class of all workers
 * 
 * This class implements a worker, which is a 1:1 pair of a data container and
 * an associated pthread. It already implements many of the mechanisms that
 * form the cooperative multitasking, which is needed to achieve time-sharing on
 * the accelerators. It spawns a thread which then works for itself, using the
 * following main loop:
 *
 *   -# \ref cu_alloc "syscall alloc"
 *   -# initialize accelerator (*)
 *   -# Perform work
 *     -# compute to the next checkpoint (*)
 *     -# \ref cu_rerequest "syscall rerequest"
 *   -# release accelerator resources (*)
 *   -# \ref cu_free "syscall free"
 *
 * The functions marked with (*) are the ones that need to be provided by the
 * subclasses (in the implementation of #getImplementationFor).
 *
 * The functions that are pure virtual, and hence need to be overwritten, are
 * the following:
 * -# #workerMetaInfo
 * -# #getImplementationFor
 */
class Worker
{
	protected:
		int id; //!< Identification number of this worker
    
    /** \brief Pointer to resources for this worker
     *
     * The superclass only passes this pointer around and never dereferences it.
     * This has to be implemented by the inheriting classes for instance as a
     * struct... The derived ctor has to set this pointer to an instance of
     * that struct and the dtor should obviously destroy it.
     */
    void *workingresources;
    
  private:
    /* Threading variables */
    pthread_t		myThread; //!< The associated pthread
    pthread_attr_t	attr; //!< Thread attributes for the pthread
    bool			isJoined;   //!< Flag, indicating if the worker has been joined to the main thread already
    bool			isDone;     //!< Flag, indicating if the worker has completed all of it's work
	
    //! Thread entry for the pthread of this worker
    static void* thread_entry(void*);

    //! Flag to initiate cancellation of the pthread
    volatile bool	isRunning;
	
    /**
     * \brief Implementation multiplexer - must be overwritten 
     * 
     * This function takes the type of the computing unit which has been
     * allocated by the kernel to this worker and fetches the corresponding
     * functions (#initFunc, #mainFunc and #freeFunc) for this unit. It writes
     * these functions as function pointers to the #accelerated_functions_t
     * struct af, and sets it's initialized member to true.
     *
     * The type of the computing unit can be copied directly from the
     * corresponding member of the struct computing_unit_shortinfo.
     *
     * \param [in]  type  The type of computing unit, for which the 
     *                    implementations should be returned - encoded in the
     *                    same way as in the struct computing_unit_shortinfo
     * \param [out]   af  A pointer to an #accelerated_functions_t, in which
     *                    this function should write the function pointers to
     *                    the actual implementations
     */
    virtual void getImplementationFor(unsigned int type, accelerated_functions_t *af) = 0;
	
    /**
     * \brief Meta information generator for the algorithm - must be overwritten
     * 
     * This function fills the provided struct meta_info with information about
     * the specific algorithm, which this worker employs. Since the abstract
     * class neither employs a working algorithm, nor knows about the algorithms
     * of the subclasses, this function is pure virtual and needs to be
     * implemented in the derived classes. For the information that is needed
     * by the kernel see either 
     * - kernel sources /include/linux/sched_hwaccel.h or
     * - an already implemented version of workerMetaInfo
     *
     * \param [out]   mi  A pointer to a struct meta_info, in which
     *                    this function should write the meta information about
     *                    the algorithm used in this worker subclass
     */
    virtual void workerMetaInfo(struct meta_info *mi) = 0;

  public:
    /* ctor & dtor */
    Worker(int id); //!< Constructor using an id.
    ~Worker();      //!< Destructor.
		
		/** \brief Flag to indicate the initialization status
     *
     * Two threads (main- and workerthread) access this data class, but the
     * constructor is only executed by one of them. The second thread is started
     * in the contructor, so it may run (and access this class) before the
     * constructor is finished. It would therefore access an instance of the
     * superclass and could call one of the pure virtual functions. Thus it is
     * crucial that the worker thread waits for the constructor to finish,
     * before entering the main loop. This flag indicates the progress
     * of the initialization of this worker:
     * - 0: not initialized
     * - 1: after ctor
     * - 2: just before alloc
     */
		int initialized;
    
    //! Main work loop of the worker.
    void startWorking();
	
    //! Checks if the worker has completed it's work.
    bool workIsDone();

    //! Interrupts the worker and initiates a clean shutdown.
    void shutdown();
    //! Joins the pthread of this worker.
    void join();

    //! Returns the pthread of this worker.
    pthread_t thisThread();
};

#endif

