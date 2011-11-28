#ifndef COMMUNICATION_CONTROLLER_H_INCLUDED
#define COMMUNICATION_CONTROLLER_H_INCLUDED

#include "./uss_daemon.h"

using namespace std;

//////////////////////////////////////////////
//											//
// class uss_communication_controller		//
// interface declaration					//
//											//
//////////////////////////////////////////////
#if(USS_FIFO == 1)
typedef map<struct uss_address, int, less<struct uss_address> > uss_fifo_list;
typedef uss_fifo_list::iterator uss_fifo_list_iterator;
#endif

class uss_comm_controller
{
	public:
	uss_comm_controller();
	~uss_comm_controller();

#if(USS_FIFO == 1)
	private:
	pthread_mutex_t fifo_mutex;
	uss_fifo_list fifo_list;	
	public:
	int get_fd_of_address(struct uss_address addr);
	int delete_fd_of_address(struct uss_address addr);
#endif
	
	int install_receiver(struct uss_address *addr);
	int install_sender(struct uss_address *addr);
	int uninstall_sender(struct uss_address *addr);
	
	int send(struct uss_address receiver_address, struct uss_message message);
	
	int blocking_read(int sfd, struct uss_address *received_address, struct uss_message *message);
};

#endif
