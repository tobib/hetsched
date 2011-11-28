#include "./uss_config.h"
#include "./uss_fifo.h"
#include "./uss_tools.h"

/***************************************\
* installation 							*
\***************************************/
/*
 * to create and open a new file descriptor
 * and store created fifos index to addr->fifo
 *
 * returns fd or -1 on error
 */
int fifo_install_receiver(struct uss_address *addr, int read, int write, int nonblocking)
{
	int ret, final_ret = -1;
	char fifo_name[USS_FIFO_NAME_LEN];
	long fifo_index;
	if(addr->fifo == 0)
	{
		//create random new fifo
		struct timeval tv;	
		int fifo_creation_successful = 0;
		for(int i = 0; i<10 && !fifo_creation_successful; i++)
		{
			ret = gettimeofday(&tv, NULL);
			if(ret != 0) {dexit("fifo_install_receiver: gettimeofday");}
			fifo_index = ((long)tv.tv_sec * 1000000 + (long)tv.tv_usec); /*index equals time in ms*/ 
			
			snprintf(fifo_name, USS_FIFO_NAME_LEN, USS_FIFO_NAME_TEMPLATE, fifo_index);
			
			ret = mkfifo(fifo_name, S_IRUSR | S_IWUSR | S_IWGRP);
			if(ret == -1 && errno != EEXIST) {dexit("fifo_install_receiver: mkfifo");}
			else if(ret == -1 && errno == EEXIST) {/*continue*/}
			else {fifo_creation_successful = 1;}
		}
	}
	else
	{
		//create specific new fifo depending value currently inside of addr->fifo
		snprintf(fifo_name, USS_FIFO_NAME_LEN, USS_FIFO_NAME_TEMPLATE, addr->fifo);		
		unlink(fifo_name);
		
		fifo_index = addr->fifo;
		
		ret = mkfifo(fifo_name, S_IRUSR | S_IWUSR | S_IWGRP);
		if(ret == -1) {dexit("fifo_install_receiver: failed fo mkfifo with specific name");}		
	}
	
	int flags = 0;
	if(read == 1){flags |= O_RDONLY;}
	if(write == 1){flags |= O_WRONLY;}
	if(nonblocking == 1){flags |= O_NONBLOCK;}
	
#if(USS_DEBUG == 1)
	printf("install_receiver on filename %s\n", fifo_name);
#endif
	final_ret = open(fifo_name, flags);
	if(final_ret == -1) {dexit("fifo_install_receiver: open fifo");}
	
	addr->fifo = fifo_index;
	return final_ret;
}

/*
 * open a file descriptor
 * for the fifo specified in addr
 *
 * returns fd or -1 on error
 */
int fifo_install_sender(struct uss_address *addr, int read, int write, int nonblocking)
{
	int final_ret = -1;
	char fifo_name[USS_FIFO_NAME_LEN];
	snprintf(fifo_name, USS_FIFO_NAME_LEN, USS_FIFO_NAME_TEMPLATE, addr->fifo);
	
	int flags = 0;
	if(read == 1){flags |= O_RDONLY;}
	if(write == 1){flags |= O_WRONLY;}
	if(nonblocking == 1){flags |= O_NONBLOCK;}
#if(USS_DEBUG == 1)
	printf("install_sender on filename %s\n", fifo_name);
#endif
	final_ret = open(fifo_name, flags);
	if(final_ret == -1) {dexit("fifo_install_sender: prob when open fifo");}
	return final_ret;
}

/***************************************\
* send and receive						*
\***************************************/
/*
 * send a message via fifo
 *
 * return: 0 on success, -1 on target unreachable
 */
int fifo_send(struct uss_message *message, int fd)
{
	ssize_t size_ret;
	size_ret = write(fd, message, sizeof(struct uss_message));
	if(errno == EPIPE)
	{return -1;}
	else if(size_ret != sizeof(struct uss_message))
	{dexit("fifo_send: too small msg send");}
	
	return 0;
}

/*
 * blocking a message via fifo
 *
 * return ssize_t value equal to the bytes read
 */
ssize_t fifo_blocking_read(struct uss_message *message, int fd)
{
	return read(fd, message, sizeof(struct uss_message));
}


