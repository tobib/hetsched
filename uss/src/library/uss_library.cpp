#include "../common/uss_config.h"
#include "./uss.h"
#include "../common/uss_tools.h"
#include "../common/uss_rtsig.h"
#include "../common/uss_fifo.h"

//basic
#include <stdlib.h>
#include <stdio.h>
//socket
#include <sys/socket.h>
#include <sys/un.h>
//memeset
#include <string.h>
//read and write
#include <unistd.h>

//////////////////////////////////////////////
//											//
// communication functionality				//
//											//
//////////////////////////////////////////////
/*
 * returns a file descriptor or -1 on error
 */
int libuss_install_receiver(struct uss_address *addr)
{
#if(USS_FIFO == 1)
	return fifo_install_receiver(addr, 1, 0, 1);
#elif(USS_RTSIG == 1)
	//SIGRTMIN+1 if receiving from multiplexer
	addr->pid = getpid();
	addr->lid = 0;
	return rtsig_install_receiver(1, 1);
#endif
}

/*
 * returns a file descriptor or -1 on error
 */
int libuss_install_sender(struct uss_address *addr)
{
#if(USS_FIFO == 1)
	return fifo_install_sender(addr, 0, 1, 0);
#elif(USS_RTSIG == 1)
	//no need for a senderobject, signals can be simply send away
	return 0;
#endif
}

/***************************************\
* message send/receive 		 			*
\***************************************/
/*
 * return the actual value of run_on
 */
int update_run_on(int *run_on, int *device_id, int sfd)
{
#if(USS_FIFO == 1)
	struct uss_message m;
	ssize_t nof_br = fifo_blocking_read(&m, sfd);
	if(nof_br == sizeof(struct uss_message) /*&& errno != EAGAIN*/)
	{
		*run_on = m.accelerator_type;
		*device_id = m.accelerator_index;
	}
	else if(nof_br == (ssize_t)-1 && errno == EAGAIN)
	{
		//everything ok made empty nonblocking read
		//printf("EAGAIN nof_br=%i \n", (int)nof_br);
	}
	else if(nof_br == (ssize_t)0)
	{
		//EOF read
		dexit("update_run_on: daemon has crashed!");
	}
	else
	{
		//printf("so(mess)=%i nof_br=%i \n",(int)sizeof(struct uss_message), (int)nof_br);
		dexit("update_run_on: read too small");
	}
#elif(USS_RTSIG == 1)
	struct signalfd_siginfo fdsi;
	ssize_t nof_br = rtsig_blocking_read(sfd, &fdsi);
	if(nof_br == sizeof(struct signalfd_siginfo))
	{
		struct uss_address a;
		memset(&a, 0, sizeof(struct uss_address));
		struct uss_message m;
		convert_int_to_uss(fdsi.ssi_int, &a, &m);

		*run_on = m.accelerator_type;
		*device_id = m.accelerator_index;
	}
	else if(nof_br != sizeof(struct signalfd_siginfo) && errno == EAGAIN)
	{
		//everything ok made empty nonblocking read
	}
	else
	{
		dexit("update_run_on: read too small");	
	}
#endif
	return *run_on;
}

/*
 * this requires the communication method to use file descriptors
 * that can be made blocking or nonblocking via fcntl
 */
int waitfor_run_on(int *run_on, int *device_id, int sfd)
{
	int flags, ret, final_ret;

	//delete flag: O_NONBLOCK
	flags = fcntl(sfd, F_GETFL);
	if(flags == -1) {dexit("waitfor_run_on had problem with fcntl");}
	flags &= (~O_NONBLOCK);
	ret = fcntl(sfd, F_SETFL, flags);
	if(ret == -1) {dexit("waitfor_run_on had problem with fcntl");}
	
	//now do blocking read on modified sfd
	final_ret = update_run_on(run_on, device_id, sfd);
	
	//reset flag: O_NONBLOCK
	flags = fcntl(sfd, F_GETFL);
	if(flags == -1) {dexit("waitfor_run_on had problem with fcntl");}
	flags |= O_NONBLOCK;
	ret = fcntl(sfd, F_SETFL, flags);
	if(ret == -1) {dexit("waitfor_run_on had problem with fcntl");}

	return final_ret;
}

/*
 * returns 0 on succes, -1 on error
 */
int libuss_send_to_daemon(struct uss_address *source_address, struct uss_address *receiver_address, 
						struct uss_message *message, int daemon_fd)
{
	int ret;
#if(USS_FILE_LOGGING == 1)
	struct timeval tv;
	gettimeofday(&tv, NULL);
	int fd = open("./benchmark/usslog", O_WRONLY | O_APPEND);
	if(fd == -1) {dexit("dlog");}
	char buf[100] = {0x0};
	sprintf(buf, "%ld %ld : sending message cleanup (%i) to of accelerator_type %i\n", 
		(long)tv.tv_sec, (long)tv.tv_usec, message->message_type, message->accelerator_type);
	write(fd, buf, strlen(buf));
	close(fd);
#endif
#if(USS_FIFO == 1)	
	ret = fifo_send(message, daemon_fd);
#elif(USS_RTSIG == 1)	
	//wraps source address (because daemon needs a threads LID) and message into a single int value
	int wrapped_int = 0;
	convert_uss_to_int(source_address, message, &wrapped_int);
	//send to receiver addres (because we send to daemon no receiver LID is needed)
	ret = rtsig_send(0, receiver_address->pid, wrapped_int);
#endif
	return ret;
}


//////////////////////////////////////////////
//											//
// registration 							//
//											//
//////////////////////////////////////////////
/*
 * libuss_register_at_daemon
 *
 */
int libuss_register_at_daemon(struct meta_sched_info *msi, struct uss_address *my_addr, struct uss_address *daemon_addr, int *my_fd, int *daemon_fd)
{
	//
	//validity check
	//
	if(!msi) {printf("error: msi is NULL pointer -> quit"); return -1;}
	
	//
	//local variables
	//
	int ret;
	int fd;
	ssize_t size_ret;
	struct sockaddr_un target_addr;

	//
	//create a socket
	//
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd == -1) {printf("error creating socket -> quit"); return -1;}

	//
	//make this thread listen on virtual line
	//
	*my_fd = libuss_install_receiver(my_addr);
	if(*my_fd == -1) 
	{
		printf("(problem with receiver installation)\n");
		return USS_ERROR_GENERAL;
	}

#if(USS_RTSIG == 1)
	libuss_start_multiplexer(); //will start only if not active yet
#endif

	//
	//connect to daemon
	//		
	memset(&target_addr, 0, sizeof(struct sockaddr_un));
	target_addr.sun_family = AF_UNIX;
	
#if(USS_FIFO == 1)	
	strncpy(target_addr.sun_path, USS_REGISTRATION_DAEMON_SOCKET, sizeof(target_addr.sun_path)-1);
#elif(USS_RTSIG == 1)	
	strncpy(target_addr.sun_path, USS_REGISTRATION_MULTIPLEXER_SOCKET, sizeof(target_addr.sun_path)-1);
#endif

	ret = connect(fd, (struct sockaddr*) &target_addr, sizeof(struct sockaddr_un));
	if(ret == -1) {printf("libuss_register error connecting socket -> return \n"); return -1;}
	
	//
	//parse msi into meta_sched_addr_info 
	//
	int i;
	struct meta_sched_addr_info transport;
	struct meta_sched_info_element *temp = NULL;

	memset(&transport, 0, sizeof(struct meta_sched_addr_info));
	
	transport.addr = *my_addr;
	transport.tid = pthread_self();
	transport.length = 0;
	
	for(i = 0; i < USS_NOF_SUPPORTED_ACCEL; i++)
	{
		temp = msi->ptr[i];
		if(!temp)
		{
			//user doesn't want to use this accel
			continue;
		}
		else
		{
			if(transport.length >= USS_MAX_MSI_TRANSPORT)
			{
				printf("warning: user specified more accelerators than allowed by USS_MAX_MSI_TRANSPORT\n");
			}
			else
			{
				transport.accelerator_type[transport.length] = i;
				transport.affinity[transport.length] = temp->affinity;
				transport.flags[transport.length] = temp->flags;
				transport.length += 1;
			}
		}
	}
	//abort if user hasn't set any elements in his msi
	if(transport.length == 0) {printf("error: user has specified empty msi -> quit\n"); return -1;}
	
	//
	//sort msai using bubble sort
	//
	int exchanged;
	int n = transport.length;
	do
	{
		exchanged = 0;
		for(i = 0; i < (n-1); i++)
		{
			if(transport.affinity[i] < transport.affinity[i+1])
			{
				int temp_acc = transport.accelerator_type[i+1];
				int temp_aff = transport.affinity[i+1];
				int temp_fla = transport.flags[i+1];
				transport.accelerator_type[i+1] = transport.accelerator_type[i];
				transport.affinity[i+1] = transport.affinity[i];
				transport.flags[i+1] = transport.flags[i];
				transport.accelerator_type[i] = temp_acc;
				transport.affinity[i] = temp_aff;
				transport.flags[i] = temp_fla;
				exchanged = 1;
			}
		}
		n--;
	} while(exchanged == 1 && n > 0);
	
#if(USS_LIBRARY_DEBUG == 1)
	//printf("\nREGISTRATION\n");
	//print_msi_short(&transport);
#endif

	//
	//send meta_sched_addr_info to server
	//
	size_ret = write(fd, &transport, sizeof(struct meta_sched_addr_info));
	if(size_ret != sizeof(struct meta_sched_addr_info)) {dexit("libuss_register: write too small");}
	
	//
	//read response and analyze for success
	//
	struct uss_registration_response resp;
	ssize_t size_got = 0;
	while(size_got < (ssize_t)sizeof(struct uss_registration_response))
	{
		size_ret = read(fd, ((&resp)+size_got), ((ssize_t)sizeof(struct uss_registration_response) - size_got));
		if(size_ret == -1) {dexit("libuss_register: general error with read call");}
		size_got += size_ret;
	}
	if(size_ret != sizeof(struct uss_registration_response)) {dexit("libuss_register: read too small or unequal");}

	int check = resp.check;
	*daemon_addr = resp.daemon_addr;
	*my_addr = resp.client_addr;
	
	if(check == USS_CONTROL_SCHED_ACCEPTED) 
	{
		#if(USS_FIFO == 1)
		#if(USS_LIBRARY_DEBUG == 1)
		printf("my_addr->fifo=%ld\n", my_addr->fifo);
		#endif
		*daemon_fd = libuss_install_sender(daemon_addr);
		if(*daemon_fd == -1) 
		{
			printf("(problem with sender installation)\n");
			close(fd);
			return USS_ERROR_GENERAL;
		}
		#elif(USS_RTSIG == 1)
		//sending signals needs no senderobject
		#endif

		#if(USS_LIBRARY_DEBUG == 1)
		printf("(succesfully transported msi and acceped by sched)\n");	
		#endif		
		close(fd);
		return 0;
	}
	else if(check == USS_CONTROL_SCHED_DECLINED)
	{
		dexit("(scheduler did not accept registration)\n");
		ret = close(fd);
		return USS_ERROR_SCHED_DECLINED_REG;
	}
	return -1;
}


//////////////////////////////////////////////
//											//
// main library functionality				//
//											//
//////////////////////////////////////////////
/*
 * libuss_start
 *
 * provides main algorithm
 *
 * (dont use function pointer binding here, because it would
 *  require a rebind each time an accelerator is chosen
 *  -> maybe solve by inline calculation?)
 */
int libuss_start(struct meta_sched_info *msi, void *md, void *mcp, int *is_finished, int *run_on, int *device_id)
{
	//
	//local variables
	//
	int ret;
	
	/*
	 *run_on variable is provided from outside what allows other threads to check this ones status
	 */
	*run_on = USS_ACCEL_TYPE_IDLE;
	*device_id = 0;

	//
	//benchmark variables
	//
	#if(BENCHMARK_REGISTRATION_TIME == 1)
	struct timespec ts;
	#endif
	#if(BENCHMARK_CONTEXTSWITCH_TIME == 1)
	struct timespec cst;
	uint64_t cst_init_ns = 0; uint64_t cst_clean_ns = 0;
	char buf[100] = {0x0}; int bench_fd;
	#endif

	//
	//register at uss (fails if no daemon is started)
	//
	struct uss_address my_addr, daemon_addr;
	memset(&my_addr, 0, sizeof(struct uss_address));	
	memset(&daemon_addr, 0, sizeof(struct uss_address));
	int my_fd;
	int daemon_fd;

	#if(BENCHMARK_REGISTRATION_TIME == 1)
	if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {dexit("clock_gettime() failed");}
	uint64_t regtime_start_ns = ts.tv_sec*(1000000000) + ts.tv_nsec;
	#endif

	ret = libuss_register_at_daemon(msi, &my_addr, &daemon_addr, &my_fd, &daemon_fd);

	#if(BENCHMARK_REGISTRATION_TIME == 1)
	if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {dexit("clock_gettime() failed");}
	uint64_t regtime_stop_ns = ts.tv_sec*(1000000000) + ts.tv_nsec;
	printf("%llu\n", (unsigned long long int)(regtime_stop_ns - regtime_start_ns));
	#endif
	
	if(ret == -1) {printf("registering at daemon unsuccessful -> quit\n"); return USS_ERROR_GENERAL;}
	if(ret == USS_ERROR_SCHED_DECLINED_REG) {printf("scheduler declined registration\n"); return USS_ERROR_SCHED_DECLINED_REG;}
	if(ret == USS_ERROR_TRANSPORT) {printf("transport error occured\n"); return USS_ERROR_TRANSPORT;}
	
	struct meta_sched_info_element *selected;
	struct uss_message curr_message;
	int current_device_id;
	int do_main_atleast_once = 0;	
	
	//
	//main functionality
	//
	//update once
	update_run_on(run_on, device_id, my_fd);
	//loop
	while (!(*is_finished))
	{
		switch(*run_on)
		{
		case USS_ACCEL_TYPE_CUDA:
			#if(USS_LIBRARY_DEBUG == 1)
			printf("case: run CUDA\n");
			#endif
			current_device_id = *device_id;
			do_main_atleast_once = 0;
			selected = msi->ptr[USS_ACCEL_TYPE_CUDA];
			
			#if(BENCHMARK_CONTEXTSWITCH_TIME == 1)
			if(clock_gettime(CLOCK_MONOTONIC, &cst) != 0) {dexit("clock_gettime() failed");}
			cst_init_ns = cst.tv_sec*(1000000000) + cst.tv_nsec;
			bench_fd = open("./benchmark/cstlog", O_WRONLY | O_APPEND); if(bench_fd == -1) {dexit("bench_cst");}
			memset(buf, 0, (size_t)100);
			sprintf(buf, "i %lld\n", (long long int)(cst_init_ns)); //=time (ns) when this writer is before init
			write(bench_fd, buf, strlen(buf)); 
			memset(buf, 0, (size_t)100);
			sprintf(buf, "c %lld\n", (long long int)(cst_clean_ns)); //=time (ns) when this writer did a cleanup
			write(bench_fd, buf, strlen(buf)); 			
			close(bench_fd);
			#endif
			
			selected->init(md, mcp, current_device_id);
			
			while(((USS_ACCEL_TYPE_CUDA == *run_on && current_device_id == *device_id) || do_main_atleast_once == 0)
					&& !(*is_finished))
			{
			selected->main(md, mcp, current_device_id);
			update_run_on(run_on, device_id, my_fd);
			do_main_atleast_once = 1;
			}
			
			selected->free(md, mcp, current_device_id);
			
			#if(BENCHMARK_CONTEXTSWITCH_TIME == 1)
			if(clock_gettime(CLOCK_MONOTONIC, &cst) != 0) {dexit("clock_gettime() failed");}
			cst_clean_ns = cst.tv_sec*(1000000000) + cst.tv_nsec;
			#endif
			
			if((*is_finished)) 
			{
				#if(USS_FIFO == 1)	
				curr_message.address = my_addr;
				#endif
				curr_message.message_type = USS_MESSAGE_ISFINISHED;
				curr_message.accelerator_type = USS_ACCEL_TYPE_CUDA;
				curr_message.accelerator_index = current_device_id;
				ret = libuss_send_to_daemon(&my_addr, &daemon_addr, &curr_message, daemon_fd);
				if(ret != 0) {dexit("library could not send message!!");}
			}
			else
			{
				#if(USS_FIFO == 1)	
				curr_message.address = my_addr;
				#endif
				curr_message.message_type = USS_MESSAGE_CLEANUP_DONE;
				curr_message.accelerator_type = USS_ACCEL_TYPE_CUDA;
				curr_message.accelerator_index = current_device_id;
				ret = libuss_send_to_daemon(&my_addr, &daemon_addr, &curr_message, daemon_fd);
				if(ret != 0) {dexit("library could not send message!!");}
			}
			break;
			
		case USS_ACCEL_TYPE_FPGA:
			#if(USS_LIBRARY_DEBUG == 1)
			printf("case: run FPGA\n");
			#endif			
			current_device_id = *device_id;
			do_main_atleast_once = 0;
			selected = msi->ptr[USS_ACCEL_TYPE_FPGA];
			
			selected->init(md, mcp, current_device_id);
			
			while(((USS_ACCEL_TYPE_FPGA == *run_on && current_device_id == *device_id) || do_main_atleast_once == 0)
					&& !(*is_finished))
			{
			selected->main(md, mcp, current_device_id);
			update_run_on(run_on, device_id, my_fd);
			do_main_atleast_once = 1;
			}
			
			selected->free(md, mcp, current_device_id);
			
			if((*is_finished)) 
			{
				#if(USS_FIFO == 1)	
				curr_message.address = my_addr;
				#endif
				curr_message.message_type = USS_MESSAGE_ISFINISHED;
				curr_message.accelerator_type = USS_ACCEL_TYPE_FPGA;
				curr_message.accelerator_index = current_device_id;
				ret = libuss_send_to_daemon(&my_addr, &daemon_addr, &curr_message, daemon_fd);
				if(ret != 0) {dexit("library could not send message!!");}
			}
			else
			{
				#if(USS_FIFO == 1)	
				curr_message.address = my_addr;
				#endif
				curr_message.message_type = USS_MESSAGE_CLEANUP_DONE;
				curr_message.accelerator_type = USS_ACCEL_TYPE_FPGA;
				curr_message.accelerator_index = current_device_id;
				ret = libuss_send_to_daemon(&my_addr, &daemon_addr, &curr_message, daemon_fd);
				if(ret != 0) {dexit("library could not send message!!");}
			}
			break;

		case USS_ACCEL_TYPE_STREAM:
			#if(USS_LIBRARY_DEBUG == 1)
			printf("case: run FPGA\n");
			#endif			
			current_device_id = *device_id;
			do_main_atleast_once = 0;
			selected = msi->ptr[USS_ACCEL_TYPE_STREAM];
			
			selected->init(md, mcp, current_device_id);
			
			while(((USS_ACCEL_TYPE_STREAM == *run_on && current_device_id == *device_id) || do_main_atleast_once == 0)
					&& !(*is_finished))
			{
			selected->main(md, mcp, current_device_id);
			update_run_on(run_on, device_id, my_fd);
			do_main_atleast_once = 1;
			}
			
			selected->free(md, mcp, current_device_id);
			
			if((*is_finished)) 
			{
				#if(USS_FIFO == 1)	
				curr_message.address = my_addr;
				#endif
				curr_message.message_type = USS_MESSAGE_ISFINISHED;
				curr_message.accelerator_type = USS_ACCEL_TYPE_STREAM;
				curr_message.accelerator_index = current_device_id;
				ret = libuss_send_to_daemon(&my_addr, &daemon_addr, &curr_message, daemon_fd);
				if(ret != 0) {dexit("library could not send message!!");}
			}
			else
			{
				#if(USS_FIFO == 1)	
				curr_message.address = my_addr;
				#endif
				curr_message.message_type = USS_MESSAGE_CLEANUP_DONE;
				curr_message.accelerator_type = USS_ACCEL_TYPE_STREAM;
				curr_message.accelerator_index = current_device_id;
				ret = libuss_send_to_daemon(&my_addr, &daemon_addr, &curr_message, daemon_fd);
				if(ret != 0) {dexit("library could not send message!!");}
			}
			break;
			
		case USS_ACCEL_TYPE_CPU:
			#if(USS_LIBRARY_DEBUG == 1)
			printf("case: run CPU\n");
			#endif
			selected = msi->ptr[USS_ACCEL_TYPE_CPU];
			selected->init(md, mcp, 0);
			
			while(USS_ACCEL_TYPE_CPU == *run_on && !(*is_finished))
			{
			selected->main(md, mcp, 0);
			update_run_on(run_on, device_id, my_fd);
			/*CPU has no do_main_atleaat_once because its init/cleanup cost are low*/
			}
			
			selected->free(md, mcp, 0);
			if((*is_finished)) 
			{
				#if(USS_FIFO == 1)	
				curr_message.address = my_addr;
				#endif
				curr_message.message_type = USS_MESSAGE_ISFINISHED;
				curr_message.accelerator_type = USS_ACCEL_TYPE_CPU;
				curr_message.accelerator_index = 0;
				ret = libuss_send_to_daemon(&my_addr, &daemon_addr, &curr_message, daemon_fd);
				if(ret != 0) {dexit("library could not send message!!");}
			}
			else
			{
				#if(USS_FIFO == 1)	
				curr_message.address = my_addr;
				#endif
				curr_message.message_type = USS_MESSAGE_CLEANUP_DONE;
				curr_message.accelerator_type = USS_ACCEL_TYPE_CPU;
				curr_message.accelerator_index = 0;
				ret = libuss_send_to_daemon(&my_addr, &daemon_addr, &curr_message, daemon_fd);
				if(ret != 0) {dexit("library could not send message!!");}
			}
			break;
			
		case USS_ACCEL_TYPE_IDLE:
			/*fallthrough to default
			 */
			 
		default:
#if(USS_LIBRARY_DEBUG == 1)
			printf("case: IDLE\n");
#endif
			//this app thread has been 'idled' by daemon -> cant do anything until a message from daemon
			waitfor_run_on(run_on, device_id, my_fd);
			break;
			
		}//end switch
	}//end main while

	//
	//cleanup by closing the file descriptors
	//
	close(my_fd);
	close(daemon_fd);

	return ret;
}


//////////////////////////////////////////////
//											//
// helper for creating an msi				//
//											//
//////////////////////////////////////////////
/*
 * libuss_msi_insert
 *
 */
int libuss_fill_msi(struct meta_sched_info *msi, int type, int affinity, int flags, 
					int (*algo_init_function)(void*, void*, int), 
					int (*algo_main_function)(void*, void*, int), 
					int (*algo_free_function)(void*, void*, int))
{
	msi->ptr[type] = (struct meta_sched_info_element*) malloc(sizeof(struct meta_sched_info_element));
	struct meta_sched_info_element *element = msi->ptr[type];
	if(!element) {printf("Error with malloc\n"); return -1;}

	element->affinity = affinity;
	element->flags = flags;
	element->init = algo_init_function;
	element->main = algo_main_function;
	element->free = algo_free_function;
	
	return 0;
}

int libuss_free_msi(struct meta_sched_info *msi)
{
	for(int f = 0; f<USS_NOF_SUPPORTED_ACCEL; f++)
	{
		if(msi->ptr[f] != NULL) {free(msi->ptr[f]);}
	}
	return 0;
}
