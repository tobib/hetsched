#include "./uss_config.h"
#include "./uss_rtsig.h"
#include "./uss_tools.h"

/***************************************\
* installation (make any T a listener)	*
\***************************************/
/*
 * to create and obtain a new NONBLOCKING socket file descriptor
 *
 * (!) this will block SIGRTMIN for this thread only
 *
 * returns -1 on error
 */
int rtsig_install_receiver(int listen_on_rtsig, int nonblock_on)
{
	//make thread "immune" to realtime signals
	sigset_t immune_set;
	sigemptyset(&immune_set);
	sigaddset(&immune_set, (SIGRTMIN+listen_on_rtsig));
	pthread_sigmask(SIG_BLOCK, &immune_set, NULL);

#if(USS_LIBRARY_DEBUG == 1)
	//printf("SIGRTMIN=%i\n", (int)SIGRTMIN);
	//printf("immune to %i\n", (int)SIGRTMIN+listen_on_rtsig);
#endif

	//prepare parameters
	int fd;	
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGRTMIN+listen_on_rtsig);

	if(nonblock_on != 0)
	{
		//create new NONBLOCKING fd for realtime signal SIGRTMIN
		fd = signalfd(-1, &mask, SFD_NONBLOCK);
		if(fd == -1) {derr("problem with signal file descriptor"); return USS_ERROR_GENERAL;}	
	}
	else
	{
		//create new NONBLOCKING fd for realtime signal SIGRTMIN
		fd = signalfd(-1, &mask, 0);
		if(fd == -1) {derr("problem with signal file descriptor"); return USS_ERROR_GENERAL;}	
	}
	return fd;
}


/***************************************\
* message wrapper function				*
\***************************************/
void convert_uss_to_int(struct uss_address *a, struct uss_message *m, int *i)
{
	int wrapped_int = 0;
	if(m->message_type >= (int)(1<<USS_WRAPPED_INT_RTSIG_MESSAGE_TYPE_LEN)) dexit("send_rtsig: message_type oob");
	if(m->accelerator_type >= (int)(1<<USS_WRAPPED_INT_RTSIG_ACCEL_TYPE_LEN)) dexit("send_rtsig: message_type oob");
	if(m->accelerator_index >= (int)(1<<USS_WRAPPED_INT_RTSIG_ACCEL_INDEX_LEN)) dexit("send_rtsig: message_type oob");
	if(a->lid >= (int)(1<<USS_WRAPPED_INT_RTSIG_LOCAL_ADDRESS_LEN)) dexit("send_rtsig: message_type oob");
	
	wrapped_int |= (m->message_type<<USS_WRAPPED_INT_RTSIG_MESSAGE_TYPE_POS);
	wrapped_int |= (m->accelerator_type<<USS_WRAPPED_INT_RTSIG_ACCEL_TYPE_POS);
	wrapped_int |= (m->accelerator_index<<USS_WRAPPED_INT_RTSIG_ACCEL_INDEX_POS);
	wrapped_int |= (a->lid<<USS_WRAPPED_INT_RTSIG_LOCAL_ADDRESS_POS);
	
	*i = wrapped_int;
}

void convert_int_to_uss(int wrapped_int, struct uss_address *a, struct uss_message *m)
{
	int message_type_selector = ((1<<USS_WRAPPED_INT_RTSIG_MESSAGE_TYPE_LEN) - 1) << USS_WRAPPED_INT_RTSIG_MESSAGE_TYPE_POS;
	int accelerator_type_selector = ((1<<USS_WRAPPED_INT_RTSIG_ACCEL_TYPE_LEN) - 1) << USS_WRAPPED_INT_RTSIG_ACCEL_TYPE_POS;
	int accelerator_index_selector = ((1<<USS_WRAPPED_INT_RTSIG_ACCEL_INDEX_LEN) - 1) << USS_WRAPPED_INT_RTSIG_ACCEL_INDEX_POS;
	int lid_selector = ((1<<USS_WRAPPED_INT_RTSIG_LOCAL_ADDRESS_LEN) - 1) << USS_WRAPPED_INT_RTSIG_LOCAL_ADDRESS_POS;
	
	int message_type = (wrapped_int & message_type_selector) >> USS_WRAPPED_INT_RTSIG_MESSAGE_TYPE_POS;
	int accelerator_type = (wrapped_int & accelerator_type_selector) >> USS_WRAPPED_INT_RTSIG_ACCEL_TYPE_POS;
	int accelerator_index = (wrapped_int & accelerator_index_selector) >> USS_WRAPPED_INT_RTSIG_ACCEL_INDEX_POS;
	int lid = (wrapped_int & lid_selector) >> USS_WRAPPED_INT_RTSIG_LOCAL_ADDRESS_POS;
	
	//fill parameters that are return via ptr
	a->lid = lid;
		
	m->message_type = message_type;
	m->accelerator_type = accelerator_type;
	m->accelerator_index = accelerator_index;
}


/***************************************\
* message send/receive RTSIG private	*
\***************************************/
/*
 * send a message via realtime signals 
 *
 * return: 0 on success, -1 on target unreachable
 */
int rtsig_send(int signo, pid_t receiver_pid, int data)
{
#if(USS_DEBUG == 1)
	printf("sending signal to pid %i with message %i\n", 
			(int)receiver_pid, data);
#endif
	union sigval sv;
	sv.sival_int = data;
	int ret = sigqueue(receiver_pid, SIGRTMIN+(signo), sv); 
	return ret; 
}

/*
 * blocking receive
 *
 * can be awakend by a FLUSH
 */
ssize_t rtsig_blocking_read(int sfd, struct signalfd_siginfo *fdsi)
{
	return read(sfd, fdsi, sizeof(struct signalfd_siginfo));
}



//////////////////////////////////////////////
//											//
// static multiplexer						//
//											//
//////////////////////////////////////////////
/***************************************\
* multi talbe							*
\***************************************/
struct multi_table_entry
{
	pthread_t local_thread;
};
struct multi_table_entry multi_table[USS_MAX_LOCAL_THREADS];

/*
 * the multiplexer is required is one process wants to
 * register more threads than one
 * -> with the current mechanism of realtime signals a
 *    rtsig send from daemon can be received from any thread
 *    and would be 'consumed/wasted' there
 *
 * => the multiplexer is a stub receiving rtsigs in order to
 *    hide a TID in the integer value
 */
int multiplexer_started = 0;
pthread_t registration_multiplexer_thread;
pthread_mutex_t multiplexer_mtx = PTHREAD_MUTEX_INITIALIZER;


/***************************************\
* bit map								*
\***************************************/
short local_thread_bitmap[USS_MAX_LOCAL_THREADS];

/*
 *returns a free lid or negative if no more avail
 *depends on USS_MAX_LOCAL_THREADS
 */
int libuss_get_new_multi_table_index()
{
	int ret, i;
	int final_ret = -1;
	ret = pthread_mutex_lock(&multiplexer_mtx);
	if(ret != 0) dexit("thread_mutex_lock");

	for(i = 0; i < USS_MAX_LOCAL_THREADS; i++)
	{
		if(local_thread_bitmap[i] == 0)
		{
			local_thread_bitmap[i] = 1;
			final_ret = i;
			break;
		}
	}	
	
	ret = pthread_mutex_unlock(&multiplexer_mtx);
	if(ret != 0) dexit("thread_mutex_unlock");
	
	return final_ret;
}

/*
 *clears bit in bitmap
 */
void libuss_clear_multi_table_index(int x)
{
	int ret;
	ret = pthread_mutex_lock(&multiplexer_mtx);
	if(ret != 0) dexit("thread_mutex_lock");

	local_thread_bitmap[x] = 0;
	
	ret = pthread_mutex_unlock(&multiplexer_mtx);
	if(ret != 0) dexit("thread_mutex_unlock");
}

/***************************************\
* multiplexer registration thread		*
\***************************************/
/*
 *this thread hijacks outgoing registration attempts
 *and slices in a local ID
 *it is also a thread that can receive SIGRTMIN+0
 */
void* libuss_registration_multiplexer_thread(void *args)
{
	pthread_detach(pthread_self());
	int ret;
	int *fd_multiplexer_ptr = (int*)args;
	int fd_multiplexer = *fd_multiplexer_ptr;
	int fd_localthread, fd_daemon;

	//SIGRTMIN+0 has to be unlocked
	sigset_t unimmune_set;
	sigemptyset(&unimmune_set);
	sigaddset(&unimmune_set, (SIGRTMIN+0));
	pthread_sigmask(SIG_UNBLOCK, &unimmune_set, NULL);
	//
	//loop for each new accepted connection (create no new thread for each => many regs may be delayed)
	//
	while(1)
	{
		//
		//wait for reg attempt from a local thread that uses multiplexer
		//
		fd_localthread = accept(fd_multiplexer, NULL, NULL);
		if(fd_localthread==-1) {printf("dderror: accept failed\n"); exit(1);}
		
		//
		//now new connection to daemon
		//
		struct sockaddr_un client_addr;
		
		fd_daemon = socket(AF_UNIX, SOCK_STREAM, 0);
		if(fd_daemon == -1) {dexit("error creating socket -> quit");}
		
		memset(&client_addr, 0, sizeof(struct sockaddr_un));
		client_addr.sun_family = AF_UNIX;
		strncpy(client_addr.sun_path, USS_REGISTRATION_DAEMON_SOCKET, sizeof(client_addr.sun_path)-1);
		
		ret = connect(fd_daemon, (struct sockaddr*) &client_addr, sizeof(struct sockaddr_un));
		if(ret == -1) 
		{
			printf("error connecting socket\n");
			close(fd_localthread);
			dexit("multiplexer could not connect to daemon -> quit");
		}
		else
		{
		//
		//read meta_sched_addr_info from local thread
		//
		struct meta_sched_addr_info transport;
		memset(&transport, 0, sizeof(struct meta_sched_addr_info));
		int nof_br = read(fd_localthread, &transport, sizeof(struct meta_sched_addr_info));
		
		//if we received too small msai return error value
		if(nof_br != sizeof(struct meta_sched_addr_info))
		{
			dexit("received too small msai");
		}
		
		//
		//create multi_table entry
		//
		int index = libuss_get_new_multi_table_index();
		if(index < 0) 
		{
			close(fd_localthread);
			close(fd_daemon);
			dexit("too many threads per process");
		}
		else
		{		
			multi_table[index].local_thread = transport.tid;
			
			//
			//write modified meta_sched_addr_info to daemon
			//
			transport.addr.lid = index;
			write(fd_daemon, &transport, sizeof(struct meta_sched_addr_info));
			
			//
			//read uss_response from daemon
			//
			struct uss_registration_response resp;
			read(fd_daemon, &resp, sizeof(uss_registration_response));
			
			//
			//write modfied uss_response back to local thread
			//
			resp.client_addr.pid = getpid();
			resp.client_addr.lid = index;
			write(fd_localthread, &resp, sizeof(struct uss_registration_response));

#if(USS_DEBUG == 1)			
			printf("SCHED RESP CHECK: %i\n", resp.check);
#endif

			//
			//depending on sched response, keep or clear saved state for this local thread
			//
			if(resp.check == USS_CONTROL_SCHED_ACCEPTED)
			{
			}
			else if(resp.check == USS_CONTROL_SCHED_DECLINED)
			{
				libuss_clear_multi_table_index(index);
			}
			else
			{
				//sth odd happend
				dexit("wrong scheduler response");
			}
			}
			//close own connection with daemon
			close(fd_daemon);
		}
	}//end while	
	
	return NULL;
}


/***************************************\
* multiplexer message thread			*
\***************************************/
void incoming_sig_handler(int sig, siginfo_t *si, void *ucontext)
{
	struct uss_address addr;
	struct uss_message mess;
	union sigval sv = si->si_value;
	int wrapped_int = si->si_value.sival_int;

	convert_int_to_uss(wrapped_int, &addr, &mess);	

	pthread_sigqueue(multi_table[addr.lid].local_thread, SIGRTMIN+1, sv);
}


/***************************************\
* multiplexer start						*
\***************************************/
int libuss_start_multiplexer()
{
	//
	//try to create a multiplexer socket
	//
	int ret;
	ret = pthread_mutex_lock(&multiplexer_mtx);
	if(ret != 0) dexit("thread_mutex_lock");
	
	if(multiplexer_started == 0)
	{
		int ret, fd_multiplexer;
		struct sockaddr_un server_addr;
		
		//create a socket
		ret = remove(USS_REGISTRATION_MULTIPLEXER_SOCKET);
		if(ret == -1 && errno != ENOENT) {dexit("problem when trying to remove old socket");}
		
		fd_multiplexer = socket(AF_UNIX, SOCK_STREAM, 0);
		if(fd_multiplexer==-1) {dexit("dderror: creating socket\n");}
		
		memset(&server_addr, 0, sizeof(struct sockaddr_un));
		server_addr.sun_family = AF_UNIX;
		strncpy(server_addr.sun_path, USS_REGISTRATION_MULTIPLEXER_SOCKET, sizeof(server_addr.sun_path)-1);
		
		ret = bind(fd_multiplexer, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_un));
		if(fd_multiplexer==-1) {dexit("dderror: binding socket failed\n");}
		
		ret = listen(fd_multiplexer, 128);
		if(fd_multiplexer==-1) {dexit("dderror: listen on socket failed\n");}
			
		pthread_create(&registration_multiplexer_thread, NULL, libuss_registration_multiplexer_thread, &fd_multiplexer);
		
		struct sigaction sa;
		sa.sa_flags = SA_SIGINFO | SA_RESTART;
		sa.sa_sigaction = incoming_sig_handler;
		sigemptyset(&sa.sa_mask);
		ret = sigaction((SIGRTMIN+0), &sa, NULL);
		if(ret == -1) {printf("dderror: signal int not established\n"); exit(1);}
		
		multiplexer_started = 1;
	}
	
	ret = pthread_mutex_unlock(&multiplexer_mtx);
	if(ret != 0) dexit("thread_mutex_unlock");
	
	return 0;
}