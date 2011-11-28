/*
 *  testapp.cpp
 *
 *  Source code of the main program
 *
 *  Spawns several sub threads and waits for their completion. Handles operating
 *  system signals (like SIGINT).
 *
 *  Originally written 2010 by Tobias Wiersema
 *
 */

/* Includes */
#include "testapp.h"
#include <stdio.h>

/* Helper macro to walk all unfinished workers (ghosts) */
#define foreach_unfinished_ghost(itghost) \
  for (int itghost = 0; itghost < NUM_GHOSTS; itghost++) \
    if (workIsDone[itghost]) \
      continue; \
    else

/* Initialize static flags */
volatile bool Testapp::isRunning = true;
volatile bool Testapp::caughtSIGINT = false;
volatile bool Testapp::caughtSIGHUP = false;
volatile bool Testapp::caughtSIGHUPpending = false;
volatile bool Testapp::caughtSIGUSR1 = false;
volatile bool Testapp::caughtSIGUSR2 = false;
volatile bool Testapp::caughtSIGNAL = false;

/* Seconds between two announces of number of running workers */
int Testapp::load_announce_delay = 60;

/* Signal handling - typedefs and forward declarations
 *   The application has to be a singleton with an available reference to it
 *   for the implemented version of signal handling
 */
Testapp* Testapp::singleInstance = NULL;
typedef void (*sighandler_t)(int);
sighandler_t signal(int signum, sighandler_t handler);
void handle_signals(int signum);


/* static singleton factory */
Testapp * Testapp::getsingleInstance(void){
  // dont create new instance if we are no longer running
  // this is effectively a single-shot singleton
  if (Testapp::isRunning && !singleInstance) singleInstance = new Testapp();
  return singleInstance;
}


/* Constructor */
Testapp::Testapp() {
  // Save the instance reference
  singleInstance = this;

  // set normal verbosity
  Debug::getDebug()->verbosityWrapper(8);

  // initialize signal handling
  lastSignal = 0;
  signalPending = false;
}

/* Destructor */
Testapp::~Testapp() {
  this->shutDown();

  DBG_INFO << "Testapp finished" << dendl;
  Debug::dropDebug();
  singleInstance = NULL;
}



/* get the constructors of the workers that can be spawned 
 * If you implement new workers, their headers have to be added here
 */
#include "worker_md5.h"
#include "worker_prime.h"

/* main work function
 * creates all workers, starts them and joins them afterwards
 */
void Testapp::performWork() {
  DBG_INFO << " Starting workers" << dendl;
  // Start all workers
  // If you implement new workers, they have to be instanciated here
  for (int i = 0; i < NUM_GHOSTS; i++){
#ifdef MODE_ONLY_MD5
      workers[i] = new Worker_md5(i);
#elif defined MODE_ONLY_PF
      workers[i] = new Worker_prime(i);
#elif defined MODE_MD5_AND_PF
    if (i % 2 == 0)
      workers[i] = new Worker_md5(i);
    else
      workers[i] = new Worker_prime(i);
#elif defined MODE_1_MD5_AND_2_PF
    if (i % 3 == 0)
      workers[i] = new Worker_md5(i);
    else
      workers[i] = new Worker_prime(i);
#endif
    // Try to preserve ordering: Let the new worker reach the alloc syscall
    while (workers[i]->initialized < 2)
      pthread_yield();
  }

  // ALl workers started
  DBG_FINE << " " << NUM_GHOSTS << " workers started\n  Joining workers...\n";

  // Wait for their completion
  int workersLeft = NUM_GHOSTS;
  bool workIsDone[NUM_GHOSTS];
  for (int i = 0; i < NUM_GHOSTS; i++){
    workIsDone[i] = false;
  }
  int graceperiod=20;
  bool killed = false;
  while (workersLeft > 0)
  {
    // signal handling in main loop
    if (this->signalPending)
      handle_last_signal();

    foreach_unfinished_ghost(i) {
      if (workers[i]->workIsDone())
      {
        workersLeft--;
        workIsDone[i] = true;
      }
    }
    sleep(GRANULARITY_SECONDS);

    // If testapp shuts down while there are still workers running, relay the
    // shutdown to them and wait, kill them after a few seconds if nothing
    // happens
    if (!isRunning)
    {
      graceperiod -= GRANULARITY_SECONDS;
      if (graceperiod < -10)
      {
        foreach_unfinished_ghost(i) {
          pthread_kill(workers[i]->thisThread(), SIGKILL);
        }
        break;
      }
      else if (!killed && graceperiod < 0)
      {
        int failedWorkers = 0;
        foreach_unfinished_ghost(i) {
          pthread_kill(workers[i]->thisThread(), SIGTERM);
           failedWorkers++;
        }
        DBG_FINER << "  Killing " << failedWorkers << "/" << NUM_GHOSTS << " workers" << dendl;
        killed = true;
      }
      else
      {
        foreach_unfinished_ghost(i) {
          workers[i]->shutdown();
        }
      }
    }
  }

  // ALl workers are ready, clean up
  DBG_FINE << "  All workers are ready." << dendl;
  DBG_FINE << "  Joining and deleting workers..." << dendl;
  for (int i = 0; i < NUM_GHOSTS; i++){
    if (workIsDone[i])
       workers[i]->join();
    delete workers[i];
  }
  DBG_FINE << "  Workers joined and destroyed" << dendl;

  // exit application gracefully
  shutDown();
}

/* Announce-function to report the workers left to join - called periodically
 * by the static announcer
 */
void Testapp::announceLoad(void){
  long busyWorkers = NUM_GHOSTS;

  // count still working workers
  for (int i = 0; i < NUM_GHOSTS; i++){
    if (workers[i]->workIsDone())
      busyWorkers--;
  }

  // generate a timestamp in local time
  time_t rawtime;
  struct tm * timeinfo;
  time ( &rawtime );
  timeinfo = localtime ( &rawtime );
  std::string temp = string(asctime (timeinfo));
  temp.at(temp.length() - 1) = ' '; /* delete trailing newline */

  // announce
  DBG_INFO << temp << "WB:" << busyWorkers << "/" << NUM_GHOSTS << dendl;
}

/* Signal handler in the context of the main thread */
void Testapp::handle_last_signal(){
  // exit if there is no new signal
  if (!this->signalPending) return;

  // switch on last received signal
  switch (this->lastSignal){
    case SIGNAL_SIGINT:
      // shutDown
      this->shutDown();
      break;
    case SIGNAL_SIGHUP:
      // announce load on user request
      announceLoad();
      break;
    case SIGNAL_LOADANNOUNCE:
      // generated by the announcer thread
      announceLoad();
      break;
    default:
      DBG_WARN << " Got unhandled signal (" << this->lastSignal << "), ignoring" << dendl;
  }
  // reset pending flag
  this->signalPending = false;
}

/* Called by the static signal handler to indicate a new signal to the main
 * thread
 */
void Testapp::setReceivedSignal(int signal)
{
  this->lastSignal = signal;
  this->signalPending = true;
}

/* Entry function for the (static) signal handling and announcing thread */
void * Testapp::announcer(void * parm) {
  // save the reference to the testapp which instanciated this thread
  Testapp	*	testapp = (Testapp *) parm;
  struct tm local;

  // unblock all signals - they interrupt sleep
  sigset_t		mask;
  sigfillset(&mask); // all allowed signals
  pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

  // setup time tracking for thread loop
  time_t now = 0, nextAnnounce = 0;

  while(testapp->isRunning) {
    // update time tracking variables
    now = time(NULL);
    localtime_r(&now, &local);
    now = mktime(&local);

    //DBG_INFO << "Current Time: " << asctime(&local) << " difference in seconds: " << difftime(now, oldnow) << dendl;

    // if the time for the next announce has passed, relay that to the testapp
    // this check is necessary, since signals interrupt the sleep below
    if (now >= nextAnnounce) {
      // check if nextAnnounce has already been initialized
      if (nextAnnounce != 0){
        testapp->setReceivedSignal(SIGNAL_LOADANNOUNCE);
      } else nextAnnounce = now;
      while (now >= nextAnnounce)
        nextAnnounce += Testapp::load_announce_delay;
    }

    // calculate offset until next deadline
    int sleepLeft = nextAnnounce - now;

    // sleep if no signal is pending
    if (!Testapp::caughtSIGNAL){
      if (sleepLeft > 0) sleep(sleepLeft);
    }
    Testapp::caughtSIGNAL = false;

    // notify testapp of signals
    if (Testapp::caughtSIGHUP){
      testapp->setReceivedSignal(SIGNAL_SIGHUP);
      Testapp::caughtSIGHUP = false;
    }
    if (Testapp::caughtSIGINT){
      testapp->setReceivedSignal(SIGNAL_SIGINT);
      Testapp::caughtSIGINT = false;
      break;
    }
  }

  DBG_INFO << "  Announcer shutting down" << dendl;

  pthread_exit(NULL);
}

/* Cleanup and shutdown method */
void Testapp::shutDown(void) {
  if (isRunning){
    DBG_INFO << "Testapp shutting down" << dendl;

    isRunning = false;
  }
}



/* main routine - instanciate testapp */
int main(int argc, char *argv[]) {
  pthread_t announcer;
  pthread_attr_t attr;
  int retVal = 0;

  // init signal handlers
  (void) signal(SIGINT, handle_signals);
  (void) signal(SIGHUP, handle_signals);
  (void) signal(SIGUSR1, handle_signals);
  (void) signal(SIGUSR2, handle_signals);
  (void) signal(SIGTERM, handle_signals);

  DBG_INFO << "Testapp booting..." << dendl;
  Testapp *Testapp = Testapp::getsingleInstance();

  // For portability, explicitly create threads in a joinable state
  // so that they can be joined later.
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
  retVal = pthread_create(&announcer, &attr, Testapp->announcer, Testapp);
  if (retVal == 0){
    // announcer thread was created - it does the signal handling now
    sigset_t   mask;
    sigfillset(&mask);
    sigprocmask(SIG_BLOCK, &mask, NULL);
  }
  DBG_INFO << "Booted up - Testapp now working" << dendl;

  // Start main loop of testapp
  Testapp->performWork();



  // Clean up and exit
  pthread_kill(announcer, SIGUSR2); // wake up announcer from sleep
  retVal = pthread_join(announcer,NULL);
  delete Testapp;
  pthread_attr_destroy(&attr);
  return(retVal);
}

/* signal handler which is called by the OS */
void handle_signals(int signum) {
  Testapp::caughtSIGNAL = true;
  if (signum == SIGTERM) {
    printf("\n\nSIGTERM caught\n");
    Testapp::caughtSIGINT = true;
  } else if (!Testapp::caughtSIGINT && signum == SIGINT) {
    printf("\n\nSIGINT caught\n");
    Testapp::caughtSIGINT = true;
  } else if (!Testapp::caughtSIGHUP && signum == SIGHUP) {
    printf("\n\nSIGHUP caught\n");
    Testapp::caughtSIGHUP = true;
  } else if (!Testapp::caughtSIGUSR2 && signum == SIGUSR2) {
    printf("\n\SIGUSR2 caught\n");
    Testapp::caughtSIGUSR2 = true;
  }
}

