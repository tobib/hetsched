#ifndef FIFO_H_INCLUDED
#define FIFO_H_INCLUDED

#include "./uss_config.h"

int fifo_install_receiver(struct uss_address *addr, int read, int write, int nonblocking);
int fifo_install_sender(struct uss_address *addr, int read, int write, int nonblocking);

int fifo_send(struct uss_message *message, int fd);
ssize_t fifo_blocking_read(struct uss_message *message, int fd);

#endif