#include "./uss_daemon.h"
#include "./uss_comm_controller.h"
#include "./uss_registration_controller.h"
#include "./uss_scheduler.h"
#include "./uss_device_controller.h"
#include "../common/uss_tools.h"

using namespace std;

static volatile int daemon_exit = 0;

static void int_sighandler(int sig)
{
	if(sig == SIGINT){printf("signal INT recieved cleanup\n"); daemon_exit = 1;}
}

void user_sighandler(int sig, siginfo_t *si, void *ucontext)
{
	/*
	 *do nothing with USR signal! just wake up main thread
	 *
	if(sig == SIGUSR1){printf("uss_daemon signal user1 recieved\n");}
	if(sig == SIGUSR2){printf("uss_daemon signal user2 recieved\n");}
	*/
}

#if(BENCHMARK_DAEMON_CPUTIME == 1)
/*BENCHMARK to calculate the CPU time this P from state to stop
 *[additionally take time from main T and compare to P]
 *state:
 *(0) benchmark_daemon_cputime_state is initialized with 0
 *(1) first registration comes in [START]
 *    => start measurement
 *(2) last job/handle is removed [STOP]
 *    => end measurement, print cpu time in nano seconds and stop daemon
 */
int benchmark_daemon_cputime_state = 0;
clockid_t selected_clockid;
struct timespec time_start;
struct timespec process_cputime_start;
struct timespec thread_cputime_start;
struct timespec time_stop;
struct timespec process_cputime_stop;
struct timespec thread_cputime_stop;
#endif

void print_complete_status(uss_registration_controller *rc, uss_scheduler *sched)
{
	type_reg_pending_table_iterator it1 = rc->reg_pending_table.begin();
	printf("---------------------------------------------------------------------------\n");
	printf("reg pending table: ");
	for(; it1 != rc->reg_pending_table.end(); it1++)
	{
		printf(" %i ", (*it1).first);
	}
	printf("\n        reg table: ");
	type_reg_table_iterator it2 = rc->reg_table.begin();
	for(; it2 != rc->reg_table.end(); it2++)
	{
		printf(" %i ", (*it2).first);
	}
	printf("\n        addr table: ");
	type_addr_table_iterator it3 = rc->addr_table.begin();
	for(; it3 != rc->addr_table.end(); it3++)
	{
#if(USS_FIFO == 1)
		printf(" %ld ", (*it3).first.fifo);
#endif
	}
	printf("\n");
	
	sched->print_queues();
	printf("\n---------------------------------------------------------------------------\n");
}

/*
 * free this process from constraints
 */
void daemonize()
{
	/*
	 * make the process a child of the init process
	 * -> child process is guaranteed not to be a process group leader
	 */
	switch(fork())
	{
		case -1: _exit(EXIT_FAILURE);
		case 0: break;
		default: _exit(EXIT_SUCCESS);
	}
	
	/*
	 * free process of any association with a controlling terminal
	 */
	if(setsid() == -1) {_exit(EXIT_FAILURE);}
	
	/*
	 * ensure that the process isn't session leader
	 */
	switch(fork())
	{
		case -1: _exit(EXIT_FAILURE);
		case 0: break;
		default: _exit(EXIT_SUCCESS);
	}

	/*
	 * clear file mode creation mask
	 */
	umask(0);
	
	/*
	 * change to USS directory
	 */
	chdir(USS_DIRECTORY);
	
	
	return;
}

/***************************************\
* MAIN THREAD ! (daemon thread)			*
\***************************************/
int main ()
{
	//
	//set properties for a daemon
	//
#if(USS_DAEMONIZE == 1)	
	daemonize();
#endif
	int ret;

	//set timing interval
	struct timespec user_param_sched_interval;
	user_param_sched_interval.tv_sec = USS_SCHED_INTERVAL_SEC;
	user_param_sched_interval.tv_nsec = USS_SCHED_INTERVAL_NSEC;

	#if(USS_DAEMON_DEBUG == 1)
	printf("\nUSER SPACE SCHEDULER - daemon starting \n");
	printf("---------------------------------------------------------------------------\n");	
	
	int display_counter = 0;
	#endif
	
#if(USS_FIFO == 1)
	//make thread "immune" to SIGPIPE signals => such fails are handled by errno status
	sigset_t immune_set;
	sigemptyset(&immune_set);
	sigaddset(&immune_set, (SIGPIPE));
	pthread_sigmask(SIG_BLOCK, &immune_set, NULL);
#elif(USS_RTSIG == 1)
	//make thread "immune" to realtime signals => all later thread inherit this mask!
	sigset_t immune_set;
	sigemptyset(&immune_set);
	sigaddset(&immune_set, (SIGRTMIN+0));
	sigaddset(&immune_set, (SIGRTMIN+1));
	pthread_sigmask(SIG_BLOCK, &immune_set, NULL);
#endif

	//
	//enable default signal handles (clean termination)
	//
	struct sigaction sa_basic;
	sa_basic.sa_flags = 0;
	sa_basic.sa_handler = int_sighandler;
	sigemptyset(&sa_basic.sa_mask);
	
	ret = sigaction(SIGINT, &sa_basic, NULL);
	if(ret == -1) {printf("dderror: signal int not established\n"); exit(1);}
	
	//
	//enable special signal handler to wake up this thread
	//
	struct sigaction sa_user;
	sa_user.sa_flags = SA_SIGINFO;
	sa_user.sa_sigaction = user_sighandler;
	sigemptyset(&sa_user.sa_mask);
	//sigaddset(&sa_user.sa_mask, SIGUSR1);
	
	ret = sigaction(SIGUSR1, &sa_user, NULL);
	if(ret == -1) {printf("dderror: signal usr1 not established\n"); exit(1);}


	//
	//create instances of controllers
	//
	uss_comm_controller cc;
	uss_registration_controller rc(&cc);

	#if(USS_DAEMON_DEBUG == 1)
	printf("registration controller  | started | new thread listening for registrations \n");
	printf("---------------------------------------------------------------------------\n");
	printf("communication controller | started \n");
	printf("---------------------------------------------------------------------------\n");
	#endif
	
	//create a thread that listens for incoming registrations
	//
	pthread_t reg_thread;
	pthread_create(&reg_thread, NULL, start_handle_incoming_registrations, &rc);

	//
	//create instance of scheduling class
	//
	uss_scheduler sched(&cc, &rc);
	uss_device_controller dc(&sched);
	
	#if(USS_DAEMON_DEBUG == 1)
	printf("communication controller | started \n");
	printf("---------------------------------------------------------------------------\n");	
	printf("device controller        | started | %i accelerators given to scheduler\n", dc.get_nof_accelerators());
	printf("---------------------------------------------------------------------------\n");	
	#endif
	#if(BENCHMARK_LOAD_BALANCER == 1)
	struct timeval lbtstart, lbtcurrent;
	gettimeofday(&lbtstart, NULL);
	#endif
	//
	//MAIN LOOP
	//
	/*invocation of the scheduler is controlled by a user defined
	 *time interval
	 *-> this main loop does actions and then waits for this time
	 */
	struct timespec request, remain;
	request = user_param_sched_interval;
	int s, handle, accepted;

	while(!daemon_exit)
	{
		//
		//check if we have been wakend up by new registration or unreg
		//
		//printf("  rc.nof_new_regs() = %i\n", rc.nof_new_regs());
		while(rc.get_nof_new_regs() > 0)
		{
			#if(BENCHMARK_DAEMON_CPUTIME == 1)
			if(benchmark_daemon_cputime_state == 0) 
			{
				benchmark_daemon_cputime_state = 1;
				//start (real) time
				if(clock_gettime(CLOCK_MONOTONIC, &time_start) != 0) {dexit("clock_gettime failed");}
				//start cputime for whole process
				if(clock_getcpuclockid(getpid(), &selected_clockid) != 0) {dexit("clock_getcpuclockid failed");}
				if(clock_gettime(selected_clockid, &process_cputime_start) != 0) {dexit("clock_gettime failed");}
				//start cputime for daemon thread only 
				if(pthread_getcpuclockid(pthread_self(), &selected_clockid) != 0) {dexit("clock_getcpuclockid failed");}
				if(clock_gettime(selected_clockid, &thread_cputime_start) != 0) {dexit("clock_gettime failed");}
			}
			#endif
			
			//fetch any one new handle
			handle = rc.get_new_reg();
			
			#if(USS_DAEMON_DEBUG == 1)
			printf("[main thread] now working on new_reg with handle = %i\n", handle);
			#endif
			
			//the status of add_job() tells us if sched accepted this new reg
			accepted = sched.add_job(handle, rc.get_msai(handle));
			
			//work on cond variable of this handle's entry in reg_table so that T can terminate
			rc.finish_registration(handle, accepted);
			//printf("[main thread] leave new_reg\n");
		}
		
		//
		//do default periodic tick
		//
		sched.periodic_tick();
		
		#if(BENCHMARK_LOAD_BALANCER == 1)
		long s, m;
		gettimeofday(&lbtcurrent, NULL);
		s = lbtcurrent.tv_sec - lbtstart.tv_sec;
		if(lbtcurrent.tv_usec > lbtstart.tv_usec)
				m = lbtcurrent.tv_usec - lbtstart.tv_usec;
		else
				m = lbtcurrent.tv_usec;
		printf("%ld.%ld", s, m );
		uss_rq_matrix_iterator e = sched.rq_matrix.begin();
		for(; e != sched.rq_matrix.end(); e++)
		{
				printf(" %i ",(*e).second.nof_all_handles);
		}
		printf("\n");
		#endif
		
		//
		//do load balance every Xth time
		//
		//sched.load_balancing();
		
		//
		//print complete status every second
		//
		#if(USS_DAEMON_DEBUG == 1)
		display_counter++;
		if(display_counter == 25)
		{print_complete_status(&rc, &sched); display_counter = 0;}
		#endif

		//
		//erase old handles
		//
		sched.remove_finished_jobs();
		
		#if(BENCHMARK_DAEMON_CPUTIME == 1)
		if(benchmark_daemon_cputime_state == 1 && sched.tokill_list.size() == 0 && sched.se_table.size() == 0) 
		{
			//start (real) time
			if(clock_gettime(CLOCK_MONOTONIC, &time_stop) != 0) {dexit("clock_gettime failed");}
			//stop time for whole process
			if(clock_getcpuclockid(getpid(), &selected_clockid) != 0) {dexit("clock_getcpuclockid failed");}
			if(clock_gettime(selected_clockid, &process_cputime_stop) != 0) {dexit("clock_gettime failed");}
			//stop time for daemon thread only 
			if(pthread_getcpuclockid(pthread_self(), &selected_clockid) != 0) {dexit("clock_getcpuclockid failed");}
			if(clock_gettime(selected_clockid, &thread_cputime_stop) != 0) {dexit("clock_gettime failed");}
			//final and sole output of daemon in this mode should be [nano seconds]
			uint64_t time_stop_ns = time_stop.tv_sec*(1000000000) + time_stop.tv_nsec;
			uint64_t time_start_ns = time_start.tv_sec*(1000000000) + time_start.tv_nsec;
			uint64_t process_cputime_stop_ns = process_cputime_stop.tv_sec*(1000000000) + process_cputime_stop.tv_nsec;
			uint64_t process_cputime_start_ns = process_cputime_start.tv_sec*(1000000000) + process_cputime_start.tv_nsec;
			uint64_t thread_cputime_stop_ns = thread_cputime_stop.tv_sec*(1000000000) + thread_cputime_stop.tv_nsec;
			uint64_t thread_cputime_start_ns = thread_cputime_start.tv_sec*(1000000000) + thread_cputime_start.tv_nsec;
			printf("%lld %lld %lld",
					(long long int)(time_stop_ns - time_start_ns),
					(long long int)(process_cputime_stop_ns - process_cputime_start_ns),
					(long long int)(thread_cputime_stop_ns - thread_cputime_start_ns));
			exit(0);
		}
		#endif
		
		//
		//sleep for time interval
		//
		/*this controls the periodic tick and thus equals the interval
		 *the scheduler can update itself or perform operations
		 *
		 *nanosleep can be interrupted by a new registration and will sleep
		 *for another full interval
		 *(note that there are few interrupts that really receive the main thread)
		 *
		 *(!) although nanosleep allows nanosecond precision
		 *    precision depends on system-scheduler and clock previsions
		 */
		s = nanosleep(&request, &remain);
		if(s == -1 && errno != EINTR) {printf("dderror: starting nanosleep\n");}
	}//end main loop
	
	exit(0);
	return 0;
}

