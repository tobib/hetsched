#include "./uss_tools.h"

void derr(const char *format)
{
	printf("(derror) ");
	printf("%s", format);
	printf(" errno=(%s)\n", strerror(errno));
}


void dexit(const char *format)
{
	printf("(derror) ");
	printf("%s", format);
	printf(" errno=(%s)\n", strerror(errno));
	exit(-1);
}

/*
 * dbug: print a struct meta_sched_info_short
 */
void print_msai(struct meta_sched_addr_info* msai)
{
	//validity check
	if(!msai) return;
	//print
	printf("printing struct meta_sched_info_short of pid: %i and length: %i\n", msai->addr.pid, msai->length);
	int i;
	for(i = 0; i < USS_MAX_MSI_TRANSPORT && i < msai->length; i++)
	{
		printf("type: %i affinity: %i flags: %i \n", 
				msai->accelerator_type[i],
				msai->affinity[i],
				msai->flags[i]);
	}
	printf("\n");
}
