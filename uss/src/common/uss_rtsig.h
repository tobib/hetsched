#ifndef RTSIG_H_INCLUDED
#define RTSIG_H_INCLUDED

#include "./uss_config.h"

/*
 * a uss_message will be wrapped into a normal integer
 * using bit-operations
 * these are the bit positions
 */
enum uss_wrapped_int_rtsig_pos
{
	USS_WRAPPED_INT_RTSIG_MESSAGE_TYPE_POS = 0,
	USS_WRAPPED_INT_RTSIG_ACCEL_TYPE_POS = 8,
	USS_WRAPPED_INT_RTSIG_ACCEL_INDEX_POS = 14,
	USS_WRAPPED_INT_RTSIG_LOCAL_ADDRESS_POS = 18
};

/*
 * a uss_message will be wrapped into a normal integer
 * using bit-operations
 * these are the field length
 */
enum uss_wrapped_int_rtsig_len
{
	USS_WRAPPED_INT_RTSIG_MESSAGE_TYPE_LEN = 3,
	USS_WRAPPED_INT_RTSIG_ACCEL_TYPE_LEN = 6,
	USS_WRAPPED_INT_RTSIG_ACCEL_INDEX_LEN = 4,
	USS_WRAPPED_INT_RTSIG_LOCAL_ADDRESS_LEN = 12
};



int rtsig_install_receiver(int listen_on_rtsig, int nonblock_on);

void convert_uss_to_int(struct uss_address *a, struct uss_message *m, int *i);
void convert_int_to_uss(int wrapped_int, struct uss_address *a, struct uss_message *m);

int rtsig_send(int signo, pid_t receiver_pid, int data);
ssize_t rtsig_blocking_read(int sfd, struct signalfd_siginfo *fdsi);

int libuss_start_multiplexer();

#endif