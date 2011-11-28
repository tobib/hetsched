#ifndef TOOLS_H_INCLUDED
#define TOOLS_H_INCLUDED

#include "./uss_config.h"

void derr(const char *format);

void dexit(const char *format);

void print_msai(struct meta_sched_addr_info* msi_short);

#endif