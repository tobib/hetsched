#ifndef REGISTRATION_CONTROLLER_H_INCLUDED
#define REGISTRATION_CONTROLLER_H_INCLUDED

#include "./uss_daemon.h"
#include "./uss_comm_controller.h"

using namespace std;

//////////////////////////////////////////////
//											//
// class uss_registration_controller		//
// interface declaration					//
//											//
//////////////////////////////////////////////

/*
 * this is used for pending registrations 
 * from library to daemon (ie the scheduler) 
 *
 *-> they are stored here until the scheduler
 *   himself accepts them
 *   (tradeoff: one additional copy but decoupling
	  reg thread from scheduler thread)
 */
struct uss_reg_pending_entry
{
	int handle;
	struct meta_sched_addr_info msai;
	int status;
	pthread_mutex_t mtx_status;
	pthread_cond_t cond_status;
};


/*
 * this is registration pending table
 *
 * uss_library registers here and uss_scheduler
 * has to fetch items from this table
 * -> this decouples the regisration process
 *    from scheduler internals
 */
typedef map<int, struct uss_reg_pending_entry, less<int> > type_reg_pending_table;
typedef type_reg_pending_table::iterator type_reg_pending_table_iterator;


/*
 * this is the registered table
 *
 * is used for remembering the address
 * of registered applications
 */
typedef map<int, struct uss_address, less<int> > type_reg_table;
typedef type_reg_table::iterator type_reg_table_iterator;

/*
 * this is the address table
 *
 * is inevitable to have an O(log(n)) method to
 * retrieve the handle in get_address_of_handle()
 */
typedef map<struct uss_address, int, less<struct uss_address> > type_addr_table;
typedef type_addr_table::iterator type_addr_table_iterator;


class uss_registration_controller
{
	private:
	int max_handle;
	int get_unique_handle();
	int new_regs;
	
	
	public:
	//table
	type_reg_pending_table reg_pending_table;
	type_reg_table reg_table;
	type_addr_table addr_table;
	set<int> reuse_handles;	
	
	//controller
	class uss_comm_controller *cc;
	
	//control
	pthread_mutex_t reg_mutex;
	pthread_cond_t reg_cond;
	pthread_t creator_thread;
	
	pthread_mutex_t handle_mutex;
	
	uss_registration_controller(class uss_comm_controller *cc);
	~uss_registration_controller();
	
	//reg_*_table
	int add_reg_pending_entry(struct meta_sched_addr_info*);
	int remove_reg_pending_entry(int);
	int add_reg_addr_entry(int handle, struct uss_address*);
	int remove_reg_addr_entry(int);
	int print_entry(int);
	
	//register helpers
	int get_nof_new_regs(void);
	void increase_new_regs(void);
	void decrease_new_regs(void);
	int get_new_reg(void);
	void finish_registration(int, int);	

	//get
	struct uss_address get_address_of_handle(int han);
	int get_handle_of_address(struct uss_address address);
	struct meta_sched_addr_info get_msai(int);	
};

void* handle_incoming_registrations(void*);
void* start_handle_incoming_registrations(void*);

#endif
