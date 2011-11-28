#include "./uss_daemon.h"
#include "../common/uss_tools.h"
#include "../common/uss_rtsig.h"
#include "../common/uss_fifo.h"
#include "./uss_comm_controller.h"
#include "./uss_registration_controller.h"
#include "./uss_scheduler.h"

using namespace std;

//////////////////////////////////////////////
//											//
// class uss_communication_controller		//
// interface definitions					//
//											//
//////////////////////////////////////////////

/***************************************\
* constructor and destructor			*
\***************************************/
uss_comm_controller::uss_comm_controller() 
{
#if(USS_FIFO == 1)
	if(pthread_mutex_init(&this->fifo_mutex, NULL) != 0) {printf("error with mutex init\n"); exit(-1);}
#endif
}

uss_comm_controller::~uss_comm_controller()
{
#if(USS_FIFO == 1)
	pthread_mutex_destroy(&this->fifo_mutex);
#endif
}

/***************************************\
* helper								*
\***************************************/
#if(USS_FIFO == 1)
/* 
 *returns searched fd on success
 */
int uss_comm_controller::get_fd_of_address(struct uss_address addr)
{
	int ret, final_ret;
	ret = pthread_mutex_lock(&(this->fifo_mutex));
	if(ret != 0) dexit("thread_mutex_lock");
	
	uss_fifo_list_iterator selected_fifo_list_element = this->fifo_list.find(addr);
	if(selected_fifo_list_element == this->fifo_list.end()) {dexit("get_fd_of_address: not found but has to be there");}
	
	final_ret = (*selected_fifo_list_element).second;
	
	ret = pthread_mutex_unlock(&(this->fifo_mutex));
	if(ret != 0) dexit("thread_mutex_unlock");
	
	return final_ret;
}

/*
 *returns 0 on success
 */
int uss_comm_controller::delete_fd_of_address(struct uss_address addr)
{
	int ret;
	ret = pthread_mutex_lock(&(this->fifo_mutex));
	if(ret != 0) dexit("thread_mutex_lock");
	
	uss_fifo_list_iterator selected_fifo_list_element = this->fifo_list.find(addr);
	if(selected_fifo_list_element == this->fifo_list.end()) {dexit("delete_fd_of_address: not found but has to be there");}
	
	this->fifo_list.erase(selected_fifo_list_element);
	
	ret = pthread_mutex_unlock(&(this->fifo_mutex));
	if(ret != 0) dexit("thread_mutex_unlock");
	
	return 0;
}
#endif
/***************************************\
* installation (make any T a listener)	*
\***************************************/
/*
 * to create and obtain a new NONBLOCKING signal file descriptor
 * or create and open fifo
 *
 * (!) RTSIG: this will block SIGRTMIN for this thread only
 *
 * returns fd on success or -1 on error
 */
int uss_comm_controller::install_receiver(struct uss_address *addr)
{
#if(USS_FIFO == 1)
	return fifo_install_receiver(addr, 1, 0, 0);
#elif(USS_RTSIG == 1)
	//create blocking sig fd that listens on SIGRTMIN+0 
	return rtsig_install_receiver(0, 0);
#endif

}

/*
 * to open fifo and gets a fd (and also memorizes it!)
 *
 * (!) RTSIG: this will block SIGRTMIN for this thread only
 *
 * returns fd on success or -1 on error
 */
int uss_comm_controller::install_sender(struct uss_address *addr)
{
#if(USS_FIFO == 1)
	//get fd
	int ret;
	int fd = fifo_install_sender(addr, 0, 1, 0);
	if(fd == -1) {dexit("install_sender: failed");}
	
	ret = pthread_mutex_lock(&(this->fifo_mutex));
	if(ret != 0) dexit("thread_mutex_lock");
	
	this->fifo_list.insert(make_pair(*addr, fd));
	
	ret = pthread_mutex_unlock(&(this->fifo_mutex));
	if(ret != 0) dexit("thread_mutex_unlock");
	
	return fd;
#elif(USS_RTSIG == 1)
	//no need for a senderobject, signals can be simply send away
	return 0;
#endif	
}

/* 
 * when work is finished call this to terminate a connection
 *
 * returns 0 on success or -1 on error
 */
int uss_comm_controller::uninstall_sender(struct uss_address *addr)
{
#if(USS_FIFO == 1)
	return close(get_fd_of_address((*addr)));
#elif(USS_RTSIG == 1)
	//no need for a senderobject, signals can be simply send away
	return 0;
#endif	
}


/***************************************\
* message send/receive GENERIC public	*
\***************************************/
/*(public)
 * send a message
 *
 * uses send_rtsig
 */
int uss_comm_controller::send(struct uss_address receiver_address, struct uss_message message)
{
	int ret;
#if(USS_DAEMON_DEBUG == 1)
	//printf("-><- cc is sending message\n");
#endif
#if(USS_FIFO == 1)	
	ret = fifo_send(&message, get_fd_of_address(receiver_address));
#elif(USS_RTSIG == 1)	
	//wraps struct uss_message into a single int value
	int wrapped_int = 0;
	convert_uss_to_int(&receiver_address, &message, &wrapped_int);
	
	//send to pid (other part of uss_address is wrapped into int)
	ret = rtsig_send(0, receiver_address.pid, wrapped_int);
#endif
	return ret;
}

/*(public)
 * blocking receive
 *
 * can be awakend by a FLUSH
 *
 * uses blocking_read_rtsig
 * return: 0 on success, -1 on error
 */
int uss_comm_controller::blocking_read(int target_fd, struct uss_address *received_address, struct uss_message *message)
{
	int final_ret = -1;
#if(USS_FIFO == 1)	
	ssize_t read_size = fifo_blocking_read(message, target_fd);
	if(read_size != sizeof(struct uss_message)) dexit("blocking_read: read_size != so(mess)");
	else final_ret = 0;
	
	*received_address = message->address;
#elif(USS_RTSIG == 1)	
	//do the read
	struct signalfd_siginfo fdsi;
	ssize_t read_size = rtsig_blocking_read(target_fd, &fdsi);
	if(read_size != sizeof(struct signalfd_siginfo)) dexit("blocking_read: read_size != so(fdsi)");
	else final_ret = 0;
	
	//fdsi.ssi_int => unwrap int value into struct uss_message
	convert_int_to_uss(fdsi.ssi_int, received_address, message);
	//fdsi.ssi_pid => put into address
	received_address->pid = fdsi.ssi_pid;
#endif	
	return final_ret;
}
