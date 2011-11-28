/** \file testapp.h
 *
 *  \brief Header for the main program
 *
 *  Contains various switches affecting the whole program.
 *
 *  Originally written 2010 by Tobias Wiersema
 *
 */

#ifndef _TESTAPP_H
#define _TESTAPP_H

/**
 * \file num_of_ghosts.h
 * \def NUM_GHOSTS
 * NUM_GHOSTS controls how many sub threads (ghosts) the program spawns
 * This may be stored in a separate file to simplify the usage of this program
 * in automated compile-and-test scripts
 */
#include "num_of_ghosts.h"
#ifndef NUM_GHOSTS
  #define NUM_GHOSTS 75
#endif

/**
 * \def GRANULARITY_SECONDS
 * GRANULARITY_SECONDS controls the number of seconds which the main program
 * periodically sleeps while waiting for all sub threads to finish
 * This is not to be confused with the kernel granularity!
 */
#define GRANULARITY_SECONDS 1

/**
 * \file kernel_granularity.h
 * \def KERNEL_GRANULARITY
 * KERNEL_GRANULARITY is the kernel's switching granularity of hardware tasks
 * This will only honored by the kernel if it was compiled with
 * APPLICATION_CONTROLLED_GRANULARITY enabled
 * This is stored in a separate file to simplify the usage of this program in
 * automated compile-and-test scripts
 */
#include "kernel_granularity.h"

/**
 * The current version of the program has two different worker types
 * - MD5 hash cracking worker
 * - Prime Factorization worker
 *
 * These defines control which workers will be spawned by the main program
 * Only one of them may be enabled at a time:
 * - MODE_ONLY_MD5 - Only spawn MD5 ghosts 
 * - MODE_ONLY_PF - Only spawn prime factrization ghosts
 * - MODE_MD5_AND_PF - Spawn equal amounts of MD5 and PF ghosts
 * - MODE_1_MD5_AND_2_PF - Spawn twice as much PF ghosts as MD5 ones
 */
//#define MODE_ONLY_MD5
//#define MODE_ONLY_PF
#define MODE_MD5_AND_PF
//#define MODE_1_MD5_AND_2_PF


/* The following define-block can be used to fine tune the output of the program
 * There are two modes predefined: "normal operation" and "load balancer timing"
 * By switching the OUTPUT_DISABLED and OUTPUT_ENABLED settings, the user can
 * control which output should be sent to stdout.
 *
 */
#define NORMAL_OPERATION

#define OUTPUT_ENABLED(...) workerAnnounce(__VA_ARGS__)
#define OUTPUT_DISABLED(...) 
#ifdef NORMAL_OPERATION
  //! SYSCALL_ANNOUNCE            -  one notice per called syscall
  #define SYSCALL_ANNOUNCE              OUTPUT_DISABLED
  //! SYSCALL_ERROR               -  one notice per called syscall error
  #define SYSCALL_ERROR                 OUTPUT_ENABLED
  //! WORKER_STATUS               -  lifetime (thread) status of the workers
  #define WORKER_STATUS                 OUTPUT_ENABLED
  //! WORKER_CU                   -  notice on allocation of a new computing unit
  #define WORKER_CU                     OUTPUT_ENABLED
  //! WORKER_AFTER_LAST_REREQUEST -  notice after denied rerequest
  #define WORKER_AFTER_LAST_REREQUEST   OUTPUT_ENABLED
  //! WORKER_FOUND_SOLUTION       -  notice if a worker completed it's work
  #define WORKER_FOUND_SOLUTION         OUTPUT_ENABLED
  //! WORKER_ANNOUNCE_CPUWORK     -  occasional announces of current work status
  #define WORKER_ANNOUNCE_CPUWORK       OUTPUT_ENABLED
  //! WORKER_ANNOUNCE_TARGET      -  notice of worker's work goal at thread start
  #define WORKER_ANNOUNCE_TARGET        OUTPUT_ENABLED
  //! LOAD_BALANCER_TIMING        -  estimate for the runtime of the in-kernel load balancer: time spent in the "free cu" syscall
  #define LOAD_BALANCER_TIMING          OUTPUT_DISABLED
#else
  //! SYSCALL_ANNOUNCE            -  one notice per called syscall
  #define SYSCALL_ANNOUNCE              OUTPUT_DISABLED
  //! SYSCALL_ERROR               -  one notice per called syscall error
  #define SYSCALL_ERROR                 OUTPUT_ENABLED
  //! WORKER_STATUS               -  lifetime (thread) status of the workers
  #define WORKER_STATUS                 OUTPUT_DISABLED
  //! WORKER_CU                   -  notice on allocation of a new computing unit
  #define WORKER_CU                     OUTPUT_DISABLED
  //! WORKER_AFTER_LAST_REREQUEST -  notice after denied rerequest
  #define WORKER_AFTER_LAST_REREQUEST   OUTPUT_DISABLED
  //! WORKER_FOUND_SOLUTION       -  notice if a worker completed it's work
  #define WORKER_FOUND_SOLUTION         OUTPUT_DISABLED
  //! WORKER_ANNOUNCE_CPUWORK     -  occasional announces of current work status
  #define WORKER_ANNOUNCE_CPUWORK       OUTPUT_ENABLED
  //! WORKER_ANNOUNCE_TARGET      -  notice of worker's work goal at thread start
  #define WORKER_ANNOUNCE_TARGET        OUTPUT_DISABLED
  //! LOAD_BALANCER_TIMING        -  estimate for the runtime of the in-kernel load balancer: time spent in the "free cu" syscall
  #define LOAD_BALANCER_TIMING          OUTPUT_ENABLED
#endif

/* signal handling constants */
#define INVALID_SIGNAL -1
#define SIGNAL_SIGINT -2
#define SIGNAL_SIGHUP -3
#define SIGNAL_LOADANNOUNCE -4

/* system includes */
#include <pthread.h>
#include <sys/signal.h>
#include <string>
#include <queue>
#include <vector>
#include <math.h>

/* project includes */
#include "debug.h"

using namespace std;

/* forward definitions needed to spawn workers */
class Worker;


/** 
 * \brief Main class for this program suit.
 * Spawns several sub threads and waits for their completion. Handles operating
 * system signals (like SIGINT).
 */
class Testapp {
  private:
    int lastSignal;     //!< Internal signal handling variable
    bool signalPending; //!< Internal signal handling variable
    
    /**
     * \brief The program periodically announces the amount of workers that did not yet
     * return. This controls the seconds between two subsequent announces and is
     * set at the top of the source code of the program.
     */
    static int load_announce_delay;
    
    //! Storage array for the references to the worker threads (ghosts)
    Worker* workers[NUM_GHOSTS];
    
    //! Signal handling in the context of the main thread
    void handle_last_signal();
    
    //! Cleanup and shutdown method
    void shutDown(void);
    
  public:
    //! New signal received - called by the static signal handler
    void setReceivedSignal(int signal);

    //! "main" method of Testapp - creates and joins workers
    void performWork(void);
    
    /**
     * \brief Announce-function to report the workers left to join - called by
     * the static announcer
     */
    void announceLoad(void);
    
    /**
     * \brief Entry for the (static) signal handling and announcing thread
     *
     * NOTE on signal handling:
     *  The signal handler must not call any of the pthread functions,
     *  especially not cond_signal and mutex_lock/unlock.
     *  The announcing thread sleeps and wakes up periodically and checks if a 
     *  signal has been caught in the meantime. If so, it forwards it to 
     *  Testapp by calling setReceivedSignal. This ensures processing of all 
     *  incoming signals and enables the signal handling methods to use all
     *  thread functions.
     */
    static void* announcer(void *parm);

    Testapp();  //!< ctor
    ~Testapp(); //!< dtor

    /**
     * \brief Testapp running and signal flags
     *   static so that they can be changed by signal handler
     *   volatile so that changes are noted by all threads
     */
    static volatile bool isRunning;
    /**
     * \brief Testapp running and signal flags
     *   static so that they can be changed by signal handler
     *   volatile so that changes are noted by all threads
     */
    static volatile bool caughtSIGINT;
    /**
     * \brief Testapp running and signal flags
     *   static so that they can be changed by signal handler
     *   volatile so that changes are noted by all threads
     */
    static volatile bool caughtSIGHUP;
    /**
     * \brief Testapp running and signal flags
     *   static so that they can be changed by signal handler
     *   volatile so that changes are noted by all threads
     */
    static volatile bool caughtSIGHUPpending;
    /**
     * \brief Testapp running and signal flags
     *   static so that they can be changed by signal handler
     *   volatile so that changes are noted by all threads
     */
    static volatile bool caughtSIGUSR1;
    /**
     * \brief Testapp running and signal flags
     *   static so that they can be changed by signal handler
     *   volatile so that changes are noted by all threads
     */
    static volatile bool caughtSIGUSR2;
    /**
     * \brief Testapp running and signal flags
     *   static so that they can be changed by signal handler
     *   volatile so that changes are noted by all threads
     */
    static volatile bool caughtSIGNAL;
    
    //! Singleton-pattern implementation
    static Testapp* getsingleInstance(void);
    //! Singleton-pattern implementation
    static Testapp* singleInstance;
};

#endif
