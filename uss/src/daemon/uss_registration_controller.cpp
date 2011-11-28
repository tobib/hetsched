#include "./uss_daemon.h"
#include "../common/uss_tools.h"
#include "./uss_registration_controller.h"
#include "./uss_scheduler.h"

using namespace std;

//////////////////////////////////////////////
//											//
// class uss_registration_controller		//
// interface definitions					//
//											//
//////////////////////////////////////////////

/***************************************\
* constructor and destructor			*
\***************************************/
uss_registration_controller::uss_registration_controller(class uss_comm_controller *cc)
{
	max_handle = 0;
	new_regs = 0;
	
	//creator thread should be main thread here
	creator_thread = pthread_self();
	
	//save link to communication controller (to setup connections during registration procedure)
	this->cc = cc;
	
	//prepare mutex and cond
	if(pthread_mutex_init(&reg_mutex, NULL) != 0) {printf("error with mutex init\n"); exit(-1);}
	if(pthread_mutex_init(&handle_mutex, NULL) != 0) {printf("error with mutex init\n"); exit(-1);}
	if(pthread_cond_init(&reg_cond, NULL) != 0) {printf("error with cond init\n"); exit(-1);}
}


uss_registration_controller::~uss_registration_controller()
{
	printf("reg_table destroyed\n");
	pthread_cond_destroy(&reg_cond);
	pthread_mutex_destroy(&handle_mutex);
	pthread_mutex_destroy(&reg_mutex);
}


/***************************************\
* reg_*_table add and remove			*
\***************************************/
/*
 * a reg_pending_entry should be removed if the scheduler has made
 * its decision
 */
int uss_registration_controller::remove_reg_pending_entry(int h)
{
	int ret;
	ret = pthread_mutex_lock(&reg_mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	//remove
	pthread_mutex_destroy(&reg_pending_table[h].mtx_status);
	pthread_cond_destroy(&reg_pending_table[h].cond_status);
	reg_pending_table.erase(h);
	
	ret = pthread_mutex_unlock(&reg_mutex);
	if(ret != 0) dexit("thread_mutex_unlock");
	return 0;
}


/*
 * upon each new registration attempt an entry will be in this table
 * until the scheduler has either accepted or declined it
 */
int uss_registration_controller::add_reg_pending_entry(struct meta_sched_addr_info *msai)
{
	//
	//get a fresh handle for this request
	//
	int handle = this->get_unique_handle();
	if(handle < 0) {printf("(derror) problem when getting a handle"); return -1;}
	int ret;
	
	//
	//prepare uss_reg_entry
	//
	struct uss_reg_pending_entry entry;
	entry.handle = handle;
	entry.msai = (*msai);
	entry.status = USS_CONTROL_NOT_PROCESSED;
	
	//
	//insert and inizialize into registration table
	//
	ret = pthread_mutex_lock(&reg_mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	//insert
	reg_pending_table.insert(make_pair(handle, entry));
	//initialize mutex and condition variable
	pthread_mutex_init(&(reg_pending_table[handle].mtx_status), NULL);
	pthread_cond_init(&(reg_pending_table[handle].cond_status), NULL);
	
	ret = pthread_mutex_unlock(&reg_mutex);
	if(ret != 0) dexit("thread_mutex_unlock");
	return handle;
}

/*
 * this is called by schedulers cleanup/is_finished method
 * COMMENT: also removes from the address from addr_table
 */
int uss_registration_controller::remove_reg_addr_entry(int h)
{
	int ret;
	
	struct uss_address a = get_address_of_handle(h);
	
	/*protect addr_table (because we do a find on it in some other thread)*/
	ret = pthread_mutex_lock(&reg_mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	addr_table.erase(a);
	reg_table.erase(h);
	
	ret = pthread_mutex_unlock(&reg_mutex);
	if(ret != 0) dexit("thread_mutex_unlock");
	return 0;
}


/*
 * this is only called when the scheduler accepted an reg_pending_entry
 */
int uss_registration_controller::add_reg_addr_entry(int handle, struct uss_address *addr)
{
	int ret;
	ret = pthread_mutex_lock(&reg_mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	//insert
	reg_table.insert(make_pair(handle, (*addr)));
	addr_table.insert(make_pair((*addr), handle));
	
	ret = pthread_mutex_unlock(&reg_mutex);
	if(ret != 0) dexit("thread_mutex_unlock");
	return 0;
}


/*
 * print an entry selected by handle h
 */
int uss_registration_controller::print_entry(int h)
{
	//printf("print test: %i %i \n", reg_table[h].msi_short.length, reg_table[h].msi_short.pid);	
	return 0;
}


/***************************************\
* registration helpers					*
\***************************************/
/*
 * return -1 on error
 */
int uss_registration_controller::get_nof_new_regs()
{
	return this->new_regs;
}


/*
 * returns or exits programm
 */
void uss_registration_controller::increase_new_regs(void)
{
	int ret;
	ret = pthread_mutex_lock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_lock"); exit(-1);}
	
	this->new_regs += 1;
	
	ret = pthread_mutex_unlock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_unlock"); exit(-1);}	
	return;
}


/*
 * returns or exits programm
 */
void uss_registration_controller::decrease_new_regs(void)
{
	int ret;
	ret = pthread_mutex_lock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_lock"); exit(-1);}
	
	this->new_regs -= 1;
	
	ret = pthread_mutex_unlock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_unlock"); exit(-1);}	
	return;
}

/*
 * return a handle or -1 on error
 */
int uss_registration_controller::get_new_reg(void)
{
	int ret;
	int final_ret = -1;
	type_reg_pending_table_iterator selected_reg_pending_table_entry;
	ret = pthread_mutex_lock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_lock"); exit(-1);}
	
	/*
	 *select only pending table entries with status USS_CONTROL_NOT_PROCESSED
	 *because other have already been process by scheduler(main thread) but not
	 *ultimately finalized by dispatcher(registration thread)
	 *COMMENT:
	 *no further mutex is required: this is the main thread so 
	 *status USS_CONTROL_* cannot change in the meantime
	 */
	selected_reg_pending_table_entry = this->reg_pending_table.begin();
	for(; selected_reg_pending_table_entry != this->reg_pending_table.end(); selected_reg_pending_table_entry++)
	{
		if((*selected_reg_pending_table_entry).second.status == USS_CONTROL_NOT_PROCESSED)
		{
			final_ret = (*selected_reg_pending_table_entry).second.handle;
		}
	}	
	
	ret = pthread_mutex_unlock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_unlock"); exit(-1);}	
	
	return final_ret;
}


/*
 * puts sched resp into reg_pending_table and signals the corresponding thread
 */
void uss_registration_controller::finish_registration(int handle, int accepted)
{
	int ret;
	ret = pthread_mutex_lock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_lock"); exit(-1);}
	ret = pthread_mutex_lock(&(this->reg_pending_table[handle].mtx_status));
	if(ret != 0) {derr("problem with pthread_mutex_lock"); exit(-1);}	
	
	if(accepted == USS_CONTROL_SCHED_ACCEPTED)
	{
		//sched accepted this new reg identified by handle
		this->reg_pending_table[handle].status = USS_CONTROL_SCHED_ACCEPTED;
	}
	else if(accepted == USS_CONTROL_SCHED_DECLINED)
	{
		//sched rejected this new reg
		this->reg_pending_table[handle].status = USS_CONTROL_SCHED_DECLINED;
	}
	else
	{
		//sth odd happend
		dexit("process_sched_response did sth odd");
	}
	
	ret = pthread_mutex_unlock(&(this->reg_pending_table[handle].mtx_status));
	if(ret != 0) {derr("problem with pthread_mutex_unlock"); exit(-1);}
	
	//signal the waiting registration dispatcher thread
	ret = pthread_cond_signal(&(this->reg_pending_table[handle].cond_status));
	if(ret != 0) {derr("problem with pthread_cond_signal"); exit(-1);}	
	
	ret = pthread_mutex_unlock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_unlock"); exit(-1);}
	
	//finished processing a new registration -> decrease nof_new_regs
	this->decrease_new_regs();
}


/***************************************\
* handle GET something					*
\***************************************/
int uss_registration_controller::get_handle_of_address(struct uss_address searched_address)
{
	int ret, final_ret = -1;
	ret = pthread_mutex_lock(&this->reg_mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	type_addr_table_iterator it = addr_table.find(searched_address);
	if(it != addr_table.end())
	{
		final_ret = (*it).second;
	}
	else
	{
		dexit("get_handle_of_addres ADDR not found");
	}
	
	ret = pthread_mutex_unlock(&this->reg_mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	return final_ret;
}

struct uss_address uss_registration_controller::get_address_of_handle(int han)
{
	int ret, final_ret = -1;
	ret = pthread_mutex_lock(&this->reg_mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	type_reg_table_iterator it = reg_table.find(han);
	
	ret = pthread_mutex_unlock(&this->reg_mutex);
	if(ret != 0) dexit("thread_mutex_unlock");
	
	if(it != reg_table.end())
	{
		//found handle in table
		return (*it).second;
	}
	else
	{
		//error or not found
		dexit("get_address_of_handle HANDLE not found");
		struct uss_address error_struct;
		memset(&error_struct, 0, sizeof(struct uss_address));
		return error_struct;
	}
}


/*
 * this will create a unique handle
 * -> later it should recycle old handles that are not used any more
 *    but no we will just increment the number
 */
int uss_registration_controller::get_unique_handle()
{
	//a handle will be unique in all of USS daemon
	/*
	 *in a young daemon will start counting up from 1
	 *
	 *if the daemon finished all work related to a handle, it is
	 *discarded into the 'reuse_handles' list and old handles can be
	 *recycled
	 *
	 *therefore this method first tries to recycle an old handle by looking into
	 *the reused_handles list
	 *
	 *if nothing is there, then a completely new handles it created until the maximum
	 *of 2 million is reached 
	 *(WARNING: this is considered an overflow and the daemon will exit)
	 *
	 *COMMENT:
	 *mutex protection because the main thread will enter on this list
	 *and iterator may change
	 */
	int ret, final_ret = -1;
	
	ret = pthread_mutex_lock(&(this->handle_mutex));
	if(ret != 0) {dexit("problem with pthread_mutex_lock");}
	
	set<int>::iterator iter = this->reuse_handles.begin();
	if(iter != this->reuse_handles.end())
	{
		int h = (*iter);
		
		//pop from recycling list
		reuse_handles.erase(h);
		
		//return new fetched value as new_handle
		final_ret = h;
	}
	else
	{
		 final_ret = ++max_handle;
	}
	
	ret = pthread_mutex_unlock(&(this->handle_mutex));
	if(ret != 0) {dexit("problem with pthread_mutex_unlock");}
	
	return final_ret;
}


struct meta_sched_addr_info uss_registration_controller::get_msai(int handle)
{
	int ret;
	type_reg_pending_table_iterator iter;
	ret = pthread_mutex_lock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_lock"); exit(-1);}
	
	iter = this->reg_pending_table.find(handle);
	
	ret = pthread_mutex_unlock(&(this->reg_mutex));
	if(ret != 0) {derr("problem with pthread_mutex_unlock"); exit(-1);}
	
	if(iter != this->reg_pending_table.end())
	{
		//success
		return (*iter).second.msai;
	}
	else
	{
		//search must not fail, since searching for a specific previously added entry
		dexit("get_msi failed");
		struct meta_sched_addr_info fail_msai;
		return fail_msai; 
	}
}


////////////////////////////////////////
//
//thread for registration
//using interface class uss_registration_controller
//
////////////////////////////////////////

/*
 * small helper for registration thread
 */
struct thread_helper
{
	uss_registration_controller *reg_controller;
	int *file_descriptor;
	pthread_mutex_t *mutex;
	pthread_cond_t *cond;
	int parameter_transported;
};

/*
 * handle_incomming_registrations()
 *
 * (created as a thread)
 *
 * explicitly does operations for each new
 * registrations
 */
void* handle_incoming_registration(void *ptr)
{
	#if(USS_DAEMON_DEBUG == 1)
	printf("[reg disp t] new incoming registration will be handled\n");
	#endif
	//detach so this needn't be joined anyhow
	pthread_detach(pthread_self());
	
	//read parameter which is struct thread_helper
	struct thread_helper *th = (struct thread_helper*) ptr;
	int ret, status;
	uss_registration_controller *rc = th->reg_controller;
	int *int_ptr = th->file_descriptor;
	int current_sfd = *int_ptr;
	
	ret = pthread_mutex_lock(th->mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	th->parameter_transported = 1;
	
	ret = pthread_mutex_unlock(th->mutex);
	if(ret != 0) dexit("thread_mutex_unlock");
	
	ret = pthread_cond_signal(th->cond);
	if(ret != 0) dexit("thread_cond_signal");
	
	//read msai from client over socket
	struct meta_sched_addr_info transport;
	memset(&transport, 0, sizeof(struct meta_sched_addr_info));
	int nof_br = read(current_sfd, &transport, sizeof(struct meta_sched_addr_info));
	
	//if we received too small msai return error value
	if(nof_br != sizeof(struct meta_sched_addr_info))
	{
		dexit("received too small msai");
	}
	
	//if this address is already present in daemon reject this request
	struct uss_registration_response resp;
	memset(&resp, 0, sizeof(struct uss_registration_response));
	
	int addr_already_exists = 0;
	ret = pthread_mutex_lock(&rc->reg_mutex);
	if(ret != 0) dexit("thread_mutex_lock");
	
	/*search with O(log(n)) and check if it has already been present*/
	type_addr_table_iterator it = rc->addr_table.find(transport.addr);
	if(it != rc->addr_table.end()) {addr_already_exists = 1;}
	
	ret = pthread_mutex_unlock(&rc->reg_mutex);
	if(ret != 0) dexit("thread_mutex_unlock");
	
	if(addr_already_exists)
	{
		resp.check = USS_CONTROL_SCHED_DECLINED;
		//printf("addr=%lld\n",transport.addr.fifo);
		write(current_sfd, &resp, sizeof(struct uss_registration_response));
		dexit("WARNING: got incoming registration from same address\n");
	}
	else
	{
		//REGISTRATION
		//setup the sending facility (opening a fifo created by library)
		rc->cc->install_sender(&transport.addr);
		
		//enter msi_short into registered_table (entrys state will be USS_CONTROL_NOT_PROCESSED)
		int new_handle = rc->add_reg_pending_entry(&transport);
						 rc->add_reg_addr_entry(new_handle, &transport.addr);
		if(new_handle == -1) {dexit("could not add reg_entry");}
	
		#if(USS_DAEMON_DEBUG == 1)
		printf("[reg disp t] add_reg_pending_entry with handle = %i\n", new_handle);
		#endif
		//increase number of jobs awaiting sched approval(producer)
		rc->increase_new_regs();
		
		//signal to main to handle a new registration
		//ret = pthread_kill(rc->creator_thread, SIGUSR1);
		//if(ret != 0) {derr("could not notify main_thread of new registration"); exit(-1);}

		//wait on condition variable of added line in reg_pending_table
		ret = pthread_mutex_lock(&(rc->reg_pending_table[new_handle].mtx_status));
		if(ret != 0) {derr("problem with pthread_mutex_lock"); exit(-1);}
		
		while(rc->reg_pending_table[new_handle].status == USS_CONTROL_NOT_PROCESSED)
		{
			ret = pthread_cond_wait(&(rc->reg_pending_table[new_handle].cond_status), 
									&(rc->reg_pending_table[new_handle].mtx_status));
			if(ret != 0) {derr("problem with pthread_cond_wait"); exit(-1);}
		}
		status = rc->reg_pending_table[new_handle].status;
		
		ret = pthread_mutex_unlock(&(rc->reg_pending_table[new_handle].mtx_status));
		if(ret != 0) {derr("problem with pthread_mutex_lock"); exit(-1);}
		
		//wait finished -> status is go/nogo from scheduler
		if(status == USS_CONTROL_SCHED_ACCEPTED) 
		{
			//scheduler accecpted this reg
			//=> write correct size back
			resp.check = USS_CONTROL_SCHED_ACCEPTED;
		}
		else if(status == USS_CONTROL_SCHED_DECLINED)
		{
			//scheduler declined this reg
			//=> write error back
			resp.check = USS_CONTROL_SCHED_DECLINED;
		}
		else
		{
			//sth odd happend
			dexit("wrong scheduler response?");
		}
	
		//write registration response back
		resp.daemon_addr.pid = getpid();
		#if(USS_FIFO == 1)
		resp.daemon_addr.fifo = 1;
		#elif(USS_RTSIG == 1)
		resp.daemon_addr.lid = 0;
		#endif
		resp.client_addr = transport.addr;
		
		write(current_sfd, &resp, sizeof(struct uss_registration_response));
		
		if(status == USS_CONTROL_SCHED_ACCEPTED) 
		{
			//move from pending to reg
			rc->remove_reg_pending_entry(new_handle);
		}
		else
		{
			//just remove
			rc->remove_reg_pending_entry(new_handle);
			rc->remove_reg_addr_entry(new_handle);
			
			//thread safe mark this handle as 'done'
			ret = pthread_mutex_lock(&(rc->handle_mutex));
			if(ret != 0) {dexit("problem with pthread_mutex_lock");}
			
			rc->reuse_handles.insert(new_handle);
			
			ret = pthread_mutex_unlock(&(rc->handle_mutex));
			if(ret != 0) {dexit("problem with pthread_mutex_unlock");}			
		}
		#if(USS_DAEMON_DEBUG == 1)
		printf("[reg disp t] finished dispatching a registration attempt\n");
		#endif
	}
	
	//signal that more new reg threads can be spawned
	ret = pthread_cond_signal(&(rc->reg_cond));
	if(ret != 0) {derr("problem with pthread_cond_signal"); exit(-1);}		

	ret = close(current_sfd);
	if(ret==-1) {printf("dderror: closing server socket failed\n\n"); exit(1);}
	return NULL;
}


/*
 * start_handle_incomming_registrations()
 *
 * (created as a thread)
 *
 * this will in turn start a new thread for each incoming
 * registration (for each socket connection)
 */
void* start_handle_incoming_registrations(void *ptr)
{
	//printf("\n[reg thread] started to handle incoming registrations\n");
	//detach so this needn't be joined anyhow
	pthread_detach(pthread_self());
	
	//
	//void pointer *ptr is address to registration_controller
	//
	uss_registration_controller *rc = (uss_registration_controller*) ptr;
	
	int sfd, ret;
	struct sockaddr_un server_addr;
	//
	//create a socket
	//
	ret = remove(USS_REGISTRATION_DAEMON_SOCKET);
	if(ret == -1 && errno != ENOENT) {dexit("problem when trying to remove old socket");}
	
	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sfd==-1) {printf("dderror: creating socket\n"); return NULL;}
	
	memset(&server_addr, 0, sizeof(struct sockaddr_un));
	server_addr.sun_family = AF_UNIX;
	strncpy(server_addr.sun_path, USS_REGISTRATION_DAEMON_SOCKET, sizeof(server_addr.sun_path)-1);
	
	ret = bind(sfd, (struct sockaddr*) &server_addr, sizeof(struct sockaddr_un));
	if(sfd==-1) {printf("dderror: binding socket failed\n"); return NULL;}
	
	ret = listen(sfd, 1028);
	if(sfd==-1) {printf("dderror: listen on socket failed\n"); return NULL;}
	#if(USS_DAEMON_DEBUG == 1)
	printf("[reg thread]   server listening\n");
	#endif
	
	//prepare mutexes for thread starts
	pthread_t thread;
	pthread_mutex_t mtx_parameter_transported;
	pthread_cond_t cond_parameter_transported;
	pthread_mutex_init(&mtx_parameter_transported, NULL);
	pthread_cond_init(&cond_parameter_transported, NULL);
	
	int fd_remote;
	struct thread_helper pthread_args;
	pthread_args.reg_controller = rc;
	pthread_args.file_descriptor = &fd_remote;
	pthread_args.mutex = &mtx_parameter_transported;
	pthread_args.cond = &cond_parameter_transported;
	pthread_args.parameter_transported = 0;
	//
	//loop for each new accepted connection (create new thread for each)
	//
	while(1)
	{
		//printf("[reg thread] server awaits connections by clients\n");
		fd_remote = accept(sfd, NULL, NULL);
		if(fd_remote==-1) {printf("dderror: accept failed\n"); exit(1);}
		#if(USS_DAEMON_DEBUG == 1)
		printf("[reg thread]   server gets connection by client creating new T\n");
		#endif
		//reset parameter_transported
		pthread_args.parameter_transported = 0;
		
		//ensure there are no more than USS_NOF_MAX_DISPATCHER_THREADS
		ret = pthread_mutex_lock(&rc->reg_mutex);
		if(ret != 0) dexit("thread_mutex_lock");
		while(rc->get_nof_new_regs() >= USS_NOF_MAX_DISPATCHER_THREADS)
		{
			ret = pthread_cond_wait(&rc->reg_cond, &rc->reg_mutex);
			if(ret != 0) dexit("thread_cond_wait");
		}
		ret = pthread_mutex_unlock(&rc->reg_mutex);
		if(ret != 0) dexit("thread_mutex_unlock");
		
		//create worker thread
		ret = pthread_create(&thread, NULL, handle_incoming_registration, (void*)&pthread_args);
		if(ret != 0) {dexit("creating thread");}
		
		//ensure parameters have been successfully acquired by worker thread
		ret = pthread_mutex_lock(pthread_args.mutex);
		if(ret != 0) dexit("thread_mutex_lock");
		while(pthread_args.parameter_transported == 0)
		{
			ret = pthread_cond_wait(pthread_args.cond, pthread_args.mutex);
			if(ret != 0) dexit("thread_cond_wait");
		}
		ret = pthread_mutex_unlock(pthread_args.mutex);
		if(ret != 0) dexit("thread_mutex_unlock");
	}
	
	//free socket
	ret = close(sfd);
	if(ret==-1) {printf("dderror: closing server socket failed\n\n"); exit(1);}
	else{printf("[reg t] shutdown of registration thread\n");}
	//free meta_sched_info_short transport struct?? -> no only malloc'ed memory needs free
	return NULL;
}
