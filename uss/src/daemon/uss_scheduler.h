#ifndef SCHEDULER_H_INCLUDED
#define SCHEDULER_H_INCLUDED

#include "./uss_daemon.h"
#include "./uss_comm_controller.h"
#include "./uss_registration_controller.h"
#include "../library/uss.h"

/***************************************\
* se									*
\***************************************/
/*
 * this scheduling entity (se) holds all relevant infos for a handle
 */
class uss_se
{
	public:
	uss_se(int handle, struct meta_sched_addr_info masi);
	~uss_se();
	
	//basic
	int handle;
	int execution_mode; //=accelerator_type
	int next_execution_mode; //set when sending an RUN_ON mess to this (can be updated when cleanup mess arrives)
	int already_send_free_cpu; //each handle can be in CPU-mode independant of run queues (=>save in SE)
	
	//which rq is this handle loaded into (can be -1 if it is nowhere)
	int enqueued_in_mq; //=accelerator_type
	int enqueued_in_rq; //=accelerator_index
	
	class uss_nanotime rruntime; //=real runtime that is independent of priorities
	class uss_nanotime vruntime; //=virtual runtime considering priorities
	
	class uss_nanotime min_granularity;
	
	int is_finished;
	
	struct meta_sched_addr_info msai;
	
	//additional
	pid_t corresponding_process;
	int progress_counter;
};

/*
 * this holds all scheduling entities (se)
 *
 * int: the unique handle 
 * struct uss_se: entry
 */
typedef map<int, struct uss_se, less<int> > uss_se_table;
typedef uss_se_table::iterator uss_se_table_iterator;


/***************************************\
* rq									*
\***************************************/
struct uss_rq_tree_entry
{
	uss_nanotime vruntime;
	int handle;
	
	uss_rq_tree_entry(uss_nanotime t, int h)
	{
		vruntime = t;
		handle = h;
	}
	
	bool operator==(const uss_rq_tree_entry& other) const
	{
		return (this->vruntime == other.vruntime && this->handle == other.handle);
	}
	
	bool operator< (const struct uss_rq_tree_entry& other) const
	{
		return (this->vruntime < other.vruntime || (this->vruntime == other.vruntime && this->handle < other.handle));
	}
};

/*
 * this holds vruntime as index and the corresponding
 * handle as index
 */
typedef set<uss_rq_tree_entry, less<uss_rq_tree_entry> > uss_rq_tree;
typedef uss_rq_tree::iterator uss_rq_tree_iterator;

/*
 * this is data struct for remembering the handle that is really running 
 * and stores everything related to message passing
 */
class uss_curr_state
{
	public:
	//basic
	int handle;
	
	//time
	/*
	 *this is the time when handle became current or the last
	 *point in time and update_curr has been performed
	 */
	class uss_nanotime exec_start; 
	
	//advanced
	int marked_runon_idle;	
	int already_send_message;

	uss_curr_state();
	~uss_curr_state();
};

/*
 * this is the main data structure in scheduler
 */
class uss_rq
{	
	public:
	pthread_mutex_t tree_mutex;	
	
	uss_rq(int, int);
	~uss_rq();
	
	//rq identification
	int accelerator_type;
	int accelerator_index;
	
	//curr information
	uss_curr_state curr;
	uss_nanotime min_vruntime;
	
	//list
	uss_rq_tree tree;
	int length;
};


/*
 * for each supported accelerator type there may be a runqueue in map
 * 
 * COMMENT:
 * it is not recommended to use this for IDLE and CPU, since they don't
 * have to be sorted or must maintain a "current element"
 */
typedef map<int, uss_rq, less<int> > uss_rq_list;
typedef uss_rq_list::iterator uss_rq_list_iterator;


struct uss_affinity_list_entry
{
	int affinity;
	int handle;
	
	uss_affinity_list_entry(int a, int h)
	{
		affinity = a;
		handle = h;
	}
	
	bool operator==(const uss_affinity_list_entry& other) const
	{
		return (this->affinity == other.affinity && this->handle == other.handle);
	}
	
	bool operator< (const struct uss_affinity_list_entry& other) const
	{
		return (this->affinity < other.affinity || (this->affinity == other.affinity && this->handle < other.handle));
	}
	
	bool operator> (const struct uss_affinity_list_entry& other) const
	{
		return (this->affinity > other.affinity || (this->affinity == other.affinity && this->handle > other.handle));
	}
};


typedef set<uss_affinity_list_entry, less<uss_affinity_list_entry> > uss_affinity_list_asc;
typedef uss_affinity_list_asc::iterator uss_affinity_list_asc_iterator;

typedef set<uss_affinity_list_entry, greater<uss_affinity_list_entry> > uss_affinity_list_des;
typedef uss_affinity_list_des::iterator uss_affinity_list_des_iterator;

class uss_mq
{
	public:
	uss_mq(int);
	~uss_mq();
	
	//mq info
	int accelerator_type;
	int nof_rq;
	int nof_all_handles;
	double centerpoint;
	
	//data structures to speed up load balancing
	uss_affinity_list_asc best_to_push; //[affinity,handle]
	uss_affinity_list_des best_to_pull; //[affinity,handle]
	
	//the runqueues
	uss_rq_list list;
};


typedef map<int, uss_mq, less<int> > uss_rq_matrix;
typedef uss_rq_matrix::iterator uss_rq_matrix_iterator;


/*
 * the CPU is special -> it has a unique rq with methods that do signaling
 */
class uss_urq
{	
	public:
	int accelerator_type;
	int accelerator_index;
	
	uss_urq();
	uss_urq(int, int);
	~uss_urq();
	
	//list
	set<int> tree;

};

struct uss_push_curve
{
	int min_push_affinity[USS_MAX_PUSH_CURVE_LEN];
};


/***************************************\
* scheduler								*
\***************************************/
class uss_scheduler
{
	private:
	pthread_t quick_dispatcher_thread;
	int bluemode, status;
	#if(USS_SYSLOAD_FROM_PROC == 1)
	int sysload_proc_fd;
	#endif
	double sysload_current;
	
	public:
	//tables
	uss_se_table se_table;
	uss_urq rq_idle;
	uss_urq rq_cpu;
	uss_rq_matrix rq_matrix;
	
	//removal helper
	set<int> tokill_list;
	pthread_mutex_t kill_mutex;
	pthread_mutex_t se_mutex;
	
	//clock
	uss_nanotime clock;
	
	//config paramters
	long min_granularity[USS_NOF_SUPPORTED_ACCEL]; //value in micro seconds
	uss_push_curve *push_curve[USS_NOF_SUPPORTED_ACCEL];
	
	//controller
	uss_comm_controller *cc;
	uss_registration_controller *rc;
	
	uss_scheduler(uss_comm_controller*, uss_registration_controller*);
	~uss_scheduler();
	
	//helper
	int is_accelerator_type_active(int accel_type);	
	uss_rq* get_rq_of_handle(int handle);
	uss_mq* get_mq_of_handle(int handle);
	uss_se* get_se_of_handle(int handle);
	int get_affinity_of_handle(int handle, int target_accelerator);
	
	//print functions
	void print_rq(int type, int index);
	void print_rq(uss_rq *rq);
	void print_mq(int type);
	void print_mq(uss_mq *mq);
	void print_queues();
	
	//rq management
	int create_rq(int type, int index);
	int delete_rq(int type, int index);
	
	//insert and remove from rq
	int insert_to_rq(struct uss_rq *rq, int handle);
	int remove_from_rq(struct uss_rq *rq, int handle);
	int check_handle_notsingle_notrunning(uss_rq *source_rq, int handle);	
	int move_to_rq(int source_handle, 
					class uss_mq *target_mq, class uss_rq *target_rq,
					class uss_mq *source_mq, class uss_rq *source_rq);
	
	//insert and remove from mq
	int get_best_rq_of_mq(class uss_mq *mq);
	int insert_to_mq(struct uss_mq *mq, int handle);
	int insert_to_mq(struct uss_mq *mq, int handle, int index);
	int remove_from_mq(struct uss_mq *mq, int handle);
	
	//insert and remove from special queues
	int insert_to_rq_idle(int handle);
	int remove_from_rq_idle(int handle);

	int insert_to_rq_cpu(int handle);
	int remove_from_rq_cpu(int handle);
		
	//time keeping
	void update_time();
	void update_sysload();
	
	//LONG TERM
	//add and remove a complete job from entire sched
	int add_job(int handle, struct meta_sched_addr_info msai);
	int remove_job(int handle);
	
	//remover called by daemon thread
	void remove_finished_jobs();
	
	//MID TERM
	//mid term functions
	void update_runtime(uss_rq *rq, uss_se *current_se);
	void update_curr(uss_rq *rq);
	void periodic_tick();
	
	int get_value_from_push_curve(struct uss_push_curve *pc, int x);
	void load_balancing();
	
	//SHORT TERM
	//quick response functions
	void handle_cleanup(int handle, int is_finished);
	void pick_next(struct uss_message m);
	int handle_message(struct uss_address a, struct uss_message m);
	
};
//SHORT TERM
//quick scheduling thread
void* quick_dispatcher(void* ptr);

#endif
