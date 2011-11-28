#include "./uss_daemon.h"
#include "../common/uss_tools.h"
#include "./uss_scheduler.h"
#include "./uss_comm_controller.h"
#include "./uss_registration_controller.h"
//////////////////////////////////////////////
//											//
// rq classes								//
//											//
//////////////////////////////////////////////
/***************************************\
* constructor and destructor			*
\***************************************/
uss_se::uss_se(int handle, struct meta_sched_addr_info msai)
{
	this->handle = handle;
	this->is_finished = 0;
	this->execution_mode = 0;
	this->next_execution_mode = 0;
	this->already_send_free_cpu = 0;
	this->min_granularity = 0;
	this->msai = msai;
}

uss_se::~uss_se()
{
	//nil
}


//////////////////////////////////////////////
//											//
// rq classes								//
//											//
//////////////////////////////////////////////
/***************************************\
* constructor and destructor			*
\***************************************/
uss_rq::uss_rq(int type, int index)
{
	this->accelerator_type = type;
	this->accelerator_index = index;
	this->length = 0;
	if(pthread_mutex_init(&tree_mutex, NULL) != 0) {printf("error with mutex init\n"); exit(-1);}
}

uss_rq::~uss_rq()
{
	//nil
}

uss_mq::uss_mq(int type)
{
	this->accelerator_type = type;
	this->nof_rq = 0;
	this->nof_all_handles = 0;
}

uss_mq::~uss_mq()
{
	//nil
}

uss_urq::uss_urq()
{
	//nil
}

uss_urq::uss_urq(int type, int index)
{
	this->accelerator_type = type;
	this->accelerator_index = index;
}

uss_urq::~uss_urq()
{
	//nil
}


uss_curr_state::uss_curr_state()
{
	this->handle = -1;
	this->marked_runon_idle = 0;
	this->already_send_message = 0;
}

uss_curr_state::~uss_curr_state()
{
	//nil
}


//////////////////////////////////////////////
//											//
// general scheduler constructs				//
//											//
//////////////////////////////////////////////
/***************************************\
* constructor and destructor			*
\***************************************/
uss_scheduler::uss_scheduler(uss_comm_controller *cc, uss_registration_controller *rc)
{
	//set important pointer
	this->cc = cc;
	this->rc = rc;
	
	//prepare idle and cpu list
	this->rq_idle = uss_urq(USS_ACCEL_TYPE_IDLE, 0);
	this->rq_cpu = uss_urq(USS_ACCEL_TYPE_CPU, 0);
	
	if(pthread_mutex_init(&kill_mutex, NULL) != 0) {printf("error with mutex init\n"); exit(-1);}
	if(pthread_mutex_init(&se_mutex, NULL) != 0) {printf("error with mutex init\n"); exit(-1);}
	
	//
	//set scheduler variables
	//
	this->bluemode = 0;
	
	//
	//get available devices
	//
	//automatically added by daemon thread with methods offered by scheduler

	//
	//read min_granularity in micro sec (all accel have same value)
	//
	#if(USS_MIN_GRANULARITY_FROM_FILE == 0)
	for(int i = 0; i < USS_NOF_SUPPORTED_ACCEL; i++)
	{
		//micro sec
		this->min_granularity[i] = USS_MIN_GRANULARITY;
	}
	#else
	
	#endif

	//
	//read curves (all accel utilize same curve now)
	//
	//alloc the space
	uss_push_curve *temp = (uss_push_curve*)malloc(sizeof(struct uss_push_curve));
	//fill this push curve manually
	memset(temp, 0, sizeof(struct uss_push_curve));
	temp->min_push_affinity[0] = 11;
	for(int i = 0; i < 10; i++)
	{
		temp->min_push_affinity[i+1] = (10-i);
	}
	for(int i = 11; i < USS_MAX_PUSH_CURVE_LEN; i++)
	{
		temp->min_push_affinity[i] = 1;
	}	
	//make avail to all accels
	for(int i = 0; i < USS_NOF_SUPPORTED_ACCEL; i++)
	{
		this->push_curve[i] = temp;
	}

	//
	//set start time
	//
	update_time();
	
	//
	//get fd for /proc/stat and then fetch cpu load
	//
	#if(USS_SYSLOAD_FROM_PROC == 1)
	this->sysload_proc_fd = open("/proc/stat", O_RDONLY);
	if(sysload_proc_fd == -1) {dexit("could not open /proc/stat for bluemode");}
	#endif
	update_sysload();
	
	//
	//launch SHORT-TERM scheduler as a thread
	//
	pthread_create(&this->quick_dispatcher_thread, NULL, quick_dispatcher, this);
}

uss_scheduler::~uss_scheduler()
{
	//free push curve memory
	pthread_mutex_destroy(&kill_mutex);
	pthread_mutex_destroy(&se_mutex);
	printf("[main thread] scheduler destroyed\n");
}


/***************************************\
* helper functions						*
\***************************************/
/*
 * check if any uss_rq for accel_type exists and return 1 on true 
 */
int uss_scheduler::is_accelerator_type_active(int accel_type)
{
	uss_rq_matrix_iterator it = this->rq_matrix.find(accel_type);
	if(it != this->rq_matrix.end())
		return 1;
	else
		return 0;
}

uss_rq* uss_scheduler::get_rq_of_handle(int handle)
{

	uss_se_table_iterator selected_se_table_entry = this->se_table.find(handle);
	if(selected_se_table_entry == this->se_table.end()) {return NULL;}
	uss_se *selected_se = &(*selected_se_table_entry).second;
	
	uss_rq_matrix_iterator selected_rq_matrix_entry = this->rq_matrix.find(selected_se->enqueued_in_mq);
	if(selected_rq_matrix_entry == this->rq_matrix.end()) {return NULL;}
	
	uss_mq *selected_mq = &(*selected_rq_matrix_entry).second;
	
	uss_rq_list_iterator selected_rq_list_entry = selected_mq->list.find(selected_se->enqueued_in_rq);
	if(selected_rq_list_entry == selected_mq->list.end()) {return NULL;}
	
	uss_rq *final_ret = &(*selected_rq_list_entry).second;
	
	
	return final_ret;
}

uss_mq* uss_scheduler::get_mq_of_handle(int handle)
{
	
	uss_se_table_iterator selected_se_table_entry = this->se_table.find(handle);
	if(selected_se_table_entry == this->se_table.end()) {return NULL;}
	uss_se *selected_se = &(*selected_se_table_entry).second;
	
	uss_rq_matrix_iterator selected_rq_matrix_entry = this->rq_matrix.find(selected_se->enqueued_in_mq);
	if(selected_rq_matrix_entry == this->rq_matrix.end()) {return NULL;}
	
	uss_mq *final_ret = &(*selected_rq_matrix_entry).second;
	
	return final_ret;
}

int uss_scheduler::get_affinity_of_handle(int handle, int target_accelerator)
{
	int final_ret = -1;
	
	uss_se_table_iterator selected_se_table_entry = this->se_table.find(handle);
	if(selected_se_table_entry == this->se_table.end()) {return NULL;}
	uss_se *selected_se = &(*selected_se_table_entry).second;	
	
	for(int i = 0; i<USS_MAX_MSI_TRANSPORT; i++)
	{
		if(selected_se->msai.accelerator_type[i] == target_accelerator)
		{
			final_ret = selected_se->msai.affinity[i];
			goto found_affinity;
		}
	}
	dexit("get_affinity_of_handle: no affinity found what should never happen");
	
	found_affinity:
	return final_ret;
}

/***************************************\
* print functions						*
\***************************************/
void uss_scheduler::print_rq(int type, int index)
{
	uss_rq_matrix_iterator mat = rq_matrix.find(type);
	if(mat == rq_matrix.end()) return;
	
	uss_rq_list_iterator lis = (*mat).second.list.find(index);
	if(lis == (*mat).second.list.end()) return;
	
	uss_rq *r = &(*lis).second;
	uss_rq_tree_iterator it4 = r->tree.begin();
	printf("[%i]",r->curr.handle);
	for(; it4 != r->tree.end(); it4++)
	{
		printf(" %lld ", (long long int)(*it4).vruntime.time);
		printf(" %i ", (*it4).handle);
	}	
	printf("\n");
}

void uss_scheduler::print_mq(int type)
{
	uss_rq_matrix_iterator mat = rq_matrix.find(type);
	if(mat == rq_matrix.end()) return;
	
	uss_rq_list_iterator lis = (*mat).second.list.begin();
	for(; lis != (*mat).second.list.end(); lis++)
	{
		print_rq(type, (*lis).first);
	}
	return;
}

void uss_scheduler::print_rq(uss_rq *rq)
{
	if(rq == NULL) {dexit("print_rq got null-ptr consider this a fatal now");}
	printf("\n| rq %i index %i | #elements %i ", 
			rq->accelerator_type, rq->accelerator_index, (int)rq->tree.size());	
	
	printf("| curr = %i  mri=%i asm=%i |", rq->curr.handle, rq->curr.marked_runon_idle, rq->curr.already_send_message);
	uss_rq_tree_iterator tree_iter = rq->tree.begin();
	for(; tree_iter != rq->tree.end(); tree_iter++)
	{
		printf(" (%lld,%i)", (long long int)(*tree_iter).vruntime.time, (*tree_iter).handle);
	}
	return;
}

void uss_scheduler::print_mq(uss_mq *mq)
{
	if(mq == NULL) {dexit("print_mq got null-ptr consider this a fatal now");}
	printf("\n| mq %i with %i handles | centerpoint = %f", 
			mq->accelerator_type, mq->nof_all_handles,mq->centerpoint);
	printf("\n___________________________________");
	
	printf("\n| push list: ");
	uss_affinity_list_asc_iterator push_iter = mq->best_to_push.begin();
	for(; push_iter != mq->best_to_push.end(); push_iter++)
	{
		printf(" (%i,%i) ", (*push_iter).affinity , (*push_iter).handle);
	}
	
	printf("\n| pull list: ");
	uss_affinity_list_des_iterator pull_iter = mq->best_to_pull.begin();
	for(; pull_iter != mq->best_to_pull.end(); pull_iter++)
	{
		printf(" (%i,%i) ", (*pull_iter).affinity , (*pull_iter).handle);
	}
	
	uss_rq_list_iterator rq_iter = mq->list.begin();
	for(; rq_iter != mq->list.end(); rq_iter++)
	{
		print_rq(&(*rq_iter).second);
	}
	printf("\n\n");
	return;
}

void uss_scheduler::print_queues()
{
	uss_rq_matrix_iterator mat_iter = rq_matrix.begin();
	for(; mat_iter != rq_matrix.end(); mat_iter++)
	{
		print_mq(&(*mat_iter).second);
	}
	return;
}

/***************************************\
* rq management (create and delete)		*
\***************************************/
/*
 * creates a new rq and (if none of this type exists) a new mq
 *
 *WARNING:
 *if mqs/rqs should be created at runtime, then this must be mutex proceted
 *against dispatcher thread
 *
 *WARNING:
 *the centerpoint calculation used for the push-mechansim distributes
 *at global "+1" of a new job to all available mqs
 *->if mqs/rqs should be created at runtime, then upon creation/deletion
 *  these centerpoints have to be adapted!
 */
int uss_scheduler::create_rq(int type, int index)
{
	//check if a uss_multiqueue for 'type' exists
	uss_rq_matrix_iterator searched_multiqueue_iter;
	searched_multiqueue_iter = this->rq_matrix.find(type);
	if(searched_multiqueue_iter == this->rq_matrix.end())
	{
		//create multiqueue once
		pair<uss_rq_matrix_iterator,bool> ret1;
		ret1 = this->rq_matrix.insert(make_pair(type, uss_mq(type)));
		if(ret1.second == true) 
		{
			searched_multiqueue_iter = ret1.first;
		}
		else
		{
			dexit("problem while creating rq");
		}
	}
	
	//insert new uss_rq into uss_multiqueue
	uss_mq *searched_multiqueue = &searched_multiqueue_iter->second;
	pair<uss_rq_list_iterator,bool> ret2;
	ret2 = searched_multiqueue->list.insert(make_pair(index, uss_rq(type, index)));
	searched_multiqueue->nof_rq++;
	
	//check if creation successful (maybe it already existed before)
	if(ret2.second == false)
	{
		dexit("problem while creating rq or insertion of any already existing rq");
	}
	
	return 0;
}

/*
 * deletes a rq and (if none of this type exists) a mq
 *
 *WARNING:
 *if mqs/rqs should be created at runtime, then this must be mutex proceted
 *against dispatcher thread
 */
int uss_scheduler::delete_rq(int type, int index)
{
	return 0;
}

/***************************************\
* rq insert and remove					*
\***************************************/
uss_nanotime get_average_vruntime_of_rq(class uss_rq *rq)
{
	return uss_nanotime(rq->min_vruntime.time + (uint64_t)50000000);
}

/*
 * insert a handle into a rq and update se values
 * (this is called by insert_to_mq)
 *
 * COMMENT:
 * this must be mutex protected since the quick
 * short-term scheduling thread will read from this
 * -> lock rq mutex because the only danger is when
 *    an rq->tree entry is made current
 *
 *returns the number of inserted handles
 */
int uss_scheduler::insert_to_rq(class uss_rq *rq, int handle)
{
	int ret, final_ret = 0;
	//
	//prepare element to be entered into uss_rq_tree tree
	//
	/*
	 *WARNING:
	 *currently the inserted element gets min_vruntime + 50ms timestamp
	 */
	uss_nanotime t = get_average_vruntime_of_rq(rq);
	
	//
	//insert in private/local data structure 'tree'
	//
	
	//do the insertion
	pair<uss_rq_tree_iterator,bool> pair_ret;
	
	ret = pthread_mutex_lock(&rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
		
	pair_ret = rq->tree.insert(uss_rq_tree_entry(t, handle));
			   rq->length++;
	
	ret = pthread_mutex_unlock(&rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
	
	if(pair_ret.second == true)
	{
		final_ret = 1;
		//update se of handle
		ret = pthread_mutex_lock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_lock\n");}
		uss_se_table_iterator selected_se_table_entry = this->se_table.find(handle);
		if(selected_se_table_entry == this->se_table.end()) {dexit("handle had no se entry");}
		uss_se *selected_se = &(*selected_se_table_entry).second;
		selected_se->enqueued_in_mq = rq->accelerator_type;
		selected_se->enqueued_in_rq = rq->accelerator_index;
		selected_se->vruntime = t;
		ret = pthread_mutex_unlock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_unlock\n");}		
	}
	return final_ret;
}

/*
 * remove a handle into a rq and update se values
 * (this is called by remove_from_mq)
 *
 * COMMENT:
 * this must be mutex protected since the quick
 * short-term scheduling thread will read from this
 * -> lock rq mutex because the only inter-thread danger is when
 *    an rq->tree entry is made current
 *    (early lock required because otherwise it may become current 
 *    in the meantime)
 *
 *returns the number of removed handles
 */
int uss_scheduler::remove_from_rq(class uss_rq *rq, int handle)
{
	int ret, final_ret = 0;
	ret = pthread_mutex_lock(&rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	if(rq->curr.handle != handle)
	{
		//we need se information to find proper element in a uss_rq's tree
		ret = pthread_mutex_lock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_lock\n");}
		
		uss_se_table_iterator selected_se_table_entry = this->se_table.find(handle);
		if(selected_se_table_entry == this->se_table.end()) {dexit("remove_from_rq: handle had no se entry");}
		uss_se *selected_se = &(*selected_se_table_entry).second;
		
		selected_se->enqueued_in_mq = -1;
		selected_se->enqueued_in_rq = -1;
		
		uss_nanotime t = selected_se->vruntime;
		
		ret = pthread_mutex_unlock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_unlock\n");}

		//do the removal with the "old" se information
		final_ret = rq->tree.erase(uss_rq_tree_entry(t, handle));	
					rq->length--;
	}
	ret = pthread_mutex_unlock(&rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
	
	return final_ret;
}


/***************************************\
* mq insert and remove					*
\***************************************/
/*
 * small helper to find the 'emptiest' rq in a mq
 * returns the index in mq
 */
int uss_scheduler::get_best_rq_of_mq(class uss_mq *mq)
{
	uss_rq_list_iterator it;
	/*
	 *WARNING:
	 *all rq are assumed to have similar properties
	 *and each rq is treated the same way
	 */
	//find the floor value of this mq before increasing number
	/*
	 *MIN = #elements / #rq ] rounded down
	 *this is the value a rq can have if all handles where equally distributed
	 *over all rq of the mq
	 *->it is good to insert in a rq that has a length of 'min' or less
	 */
	int min = floor(mq->nof_all_handles / mq->nof_rq);
	
	//go through all rqs and return the index of first rq that has length less or equal 'min'
	it = mq->list.begin();
	if(it == mq->list.end()) {dexit("best_rq_of_mq: mq without any rq");}
	
	for(; it != mq->list.end(); it++)
	{	
		if((int) (*it).second.tree.size() <= min)
		{
			return (*it).first;
		}
	}
	dexit("get_best_rq_of_mq: no element in mq found that is <= min");
	return -1;
}

/*
 * insert a handle into a mq which involves
 * selecting an internal rq and inserting it there
 * as well as updating the mq's data records
 * (this used by load balancing and registration functionality)
 *
 * if the index number to the rq is specified by the parameter 'index' then
 * it is inserted there directly and not in a balanced fashion
 *
 * [only used by add_job()]
 */
int uss_scheduler::insert_to_mq(class uss_mq *mq, int handle, int index)
{
	int ret = -1;
	uss_rq_list_iterator it;
	//
	//select an internal rq and insert there
	//
	if(index == -1)
	{
		/*
		 *search for best rq to insert
		 */
		it = mq->list.find(get_best_rq_of_mq(mq));
		if(it == mq->list.end()) dexit("insert_to_mq: insertion to mq without any proper rq");
		ret = insert_to_rq(&(*it).second, handle);
	}
	else
	{
		/*
		 *directly insert to this mq's rq specified by index parameter
		 */
		it = mq->list.find(index);
		if(it == mq->list.end()) dexit("insert_to_mq: direct insertion to mq without any proper rq");
		ret = insert_to_rq(&(*it).second, handle);
	}	
	
	//insertion must never fail!
	if(ret == -1 || ret == 0) dexit("no insertion done during insert_to_mq, problem with min value");
	
	//increase counters
	mq->nof_all_handles++;
	
	//just insert to topush list (topull list is handled by add_job())
	ret = pthread_mutex_lock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}	
	
	int affinity = get_affinity_of_handle(handle, mq->accelerator_type);
			
	ret = pthread_mutex_unlock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}	
	
	mq->best_to_push.insert(uss_affinity_list_entry(affinity, handle));
	
	return 0;
}

/*
 * this automatically inserts in a balanced fashion by specifying 
 * index as -1
 *
 * [only used by add_job()]
 */
int uss_scheduler::insert_to_mq(class uss_mq *mq, int handle)
{
	return insert_to_mq(mq, handle, -1);
}


/*
 * remove a handle from a mq which involves
 * deletion from the internal rq
 * as well as updating the mq's data records
 *
 * [only used by remove_job()]
 */
int uss_scheduler::remove_from_mq(class uss_mq *mq, int handle)
{
	int ret, final_ret = -1;
	//just issue the remove to corresponding rq with proper index taken from se_table
	ret = pthread_mutex_lock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	uss_se_table_iterator selected_se_entry = this->se_table.find(handle);
	if(selected_se_entry == this->se_table.end()) dexit("handle not in se_table");
	int index = (*selected_se_entry).second.enqueued_in_rq;
	
	ret = pthread_mutex_unlock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
	
	uss_rq_list_iterator it2 = mq->list.find(index);
	if(it2 == mq->list.end()) dexit("remove_from_rq: no such rq with index found");
	uss_rq *selected_rq = &(*it2).second;
	
	/*
	 *remove_from_rq return the number of handles removed
	 */
	final_ret = remove_from_rq(selected_rq, handle);
	
	if(final_ret == 1)
	{
		//decrease counters
		mq->nof_all_handles--;
		
		//just remove from topush list (topull list is handled by remove_job())
		ret = pthread_mutex_lock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_lock\n");}
		
		int affinity = get_affinity_of_handle(handle, mq->accelerator_type);
		
		ret = pthread_mutex_unlock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_unlock\n");}	
		
		mq->best_to_push.erase(uss_affinity_list_entry(affinity, handle));	

		//
		//do internal loadbalancing to have equally distributed rq fill levels
		//
		
	}
	return final_ret;
}

/***************************************\
* moving in between rq (is mq aware!)	*
\***************************************/
/*
 * check if handle is not the only element in its rq and
 * if handle is not currently running on any accel
 */
int uss_scheduler::check_handle_notsingle_notrunning(uss_rq *source_rq, int handle)
{
	return ((source_rq->length > 1) && (source_rq->curr.handle != handle));
}

/*
 * returns nof moved handles
 */
int uss_scheduler::move_to_rq(int source_handle, 
							class uss_mq *target_mq, class uss_rq *target_rq,
							class uss_mq *source_mq, class uss_rq *source_rq)
{
	int ret, final_ret = 0, instant_return = 0;
	
	//check if any pointer is NULL to avoid problems
	if(source_rq == NULL || target_rq == NULL ||
	   source_mq == NULL || target_mq == NULL) 
	{dexit("move_to_rq: null-ptr as parameter");}
	
	/*MUTEX RQ LOCKED
	 *load balancing must be mutex protected against dispatcher thread who makes
	 *handles current
	 *
	 *COMMENT:
	 *secure both the source rq and the one that pulls
	 */
	ret = pthread_mutex_lock(&target_rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	ret = pthread_mutex_lock(&source_rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	ret = pthread_mutex_lock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	uss_se_table_iterator selected_se_table_entry = this->se_table.find(source_handle);
	if(selected_se_table_entry == this->se_table.end()) {dexit("move_to_rq: no se tab entry");}
	uss_se *selected_se = &(*selected_se_table_entry).second;
	
	if(selected_se == NULL) dexit("move no se");
	if(selected_se->is_finished) {instant_return = 1;}
	
	ret = pthread_mutex_unlock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}

	/*
	 *verify that this handle is not curr or the only element in its rq
	 *(maybe check if it is an is_finished=1 element)
	 */
	if(check_handle_notsingle_notrunning(source_rq, source_handle) && instant_return == 0)
	{
		//
		//move source_handle from source_rq to target_rq
		//
		ret = pthread_mutex_lock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_lock\n");}
		
		source_rq->tree.erase(uss_rq_tree_entry(selected_se->vruntime, source_handle));
		source_rq->length--;
		source_mq->nof_all_handles--;
		
		selected_se->enqueued_in_mq = target_mq->accelerator_type;
		selected_se->enqueued_in_rq = target_rq->accelerator_index;
		selected_se->vruntime = get_average_vruntime_of_rq(target_rq);
		
		ret = pthread_mutex_unlock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_unlock\n");}
		
		pair<uss_rq_tree_iterator,bool> pair_ret;
		pair_ret = target_rq->tree.insert(uss_rq_tree_entry(get_average_vruntime_of_rq(target_rq), source_handle));
				   target_rq->length++;
				   target_mq->nof_all_handles++;

		if(pair_ret.second == false) {dexit("move_to_rq: insert failed");}
		
		//
		//refresh best_to_pull and best_to_push lists of both mqs
		//
		if(source_mq == target_mq)
		{
			//no need to update any mq lists
		}
		else
		{
			ret = pthread_mutex_lock(&this->se_mutex);
			if(ret != 0) {dexit("thread_mutex_lock\n");}
			
			int affinity_in_source = get_affinity_of_handle(source_handle, source_mq->accelerator_type);
			source_mq->best_to_push.erase(uss_affinity_list_entry(affinity_in_source, source_handle));
			source_mq->best_to_pull.insert(uss_affinity_list_entry(affinity_in_source, source_handle));
			
			int affinity_in_target = get_affinity_of_handle(source_handle, target_mq->accelerator_type);
			target_mq->best_to_push.insert(uss_affinity_list_entry(affinity_in_target, source_handle));
			target_mq->best_to_pull.erase(uss_affinity_list_entry(affinity_in_target, source_handle));
			
			ret = pthread_mutex_unlock(&this->se_mutex);
			if(ret != 0) {dexit("thread_mutex_unlock\n");}
		}
		
		final_ret = 1;
	}

	ret = pthread_mutex_unlock(&source_rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
	ret = pthread_mutex_unlock(&target_rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
	/*MUTEX RQ UNLOCKED
	 */
	return final_ret;
}

/***************************************\
* special queues insert and remove		*
\***************************************/
int uss_scheduler::insert_to_rq_idle(int handle)
{
	this->rq_idle.tree.insert(handle);
	return 0;
}

int uss_scheduler::remove_from_rq_idle(int handle)
{
	this->rq_idle.tree.erase(handle);
	return 0;
}

/*
 * WARNING:
 * this is a CPU-only queue and elements inside here are not scheduled by USS
 * and remain under control of normal CFS
 */
int uss_scheduler::insert_to_rq_cpu(int handle)
{
	this->rq_cpu.tree.insert(handle);
	
	struct uss_address addr;
	struct uss_message mess;
	
	addr = rc->get_address_of_handle(handle);
	
	mess.message_type = USS_MESSAGE_RUNON;
	mess.accelerator_type = USS_ACCEL_TYPE_CPU;
	mess.accelerator_index = 0;
	
	int ret = this->cc->send(addr, mess);
	if(ret == -1) {dexit("insert_to_rq_cpu: failed to send message");}
	
	return 0;
}

int uss_scheduler::remove_from_rq_cpu(int handle)
{
	this->rq_cpu.tree.erase(handle);
	
	struct uss_address addr;
	struct uss_message mess;
	
	addr = rc->get_address_of_handle(handle);
	
	mess.message_type = USS_MESSAGE_RUNON;
	mess.accelerator_type = USS_ACCEL_TYPE_IDLE;
	mess.accelerator_index = 0;
	
	int ret = this->cc->send(addr, mess);
	if(ret == -1) {dexit("remove_from_rq_cpu: failed to send message");}	
	
	return 0;
}


//////////////////////////////////////////////
//											//
// LONG TERM SCHEDULING 					//
//											//
//////////////////////////////////////////////

/***************************************\
* add and remove job from entire sched	*
\***************************************/
/*
 * called by daemon thread if
 * a new registration occured
 * -> the scheduler decides whether to 
 *    accept or decline this new request
 */
int uss_scheduler::add_job(int handle, struct meta_sched_addr_info msai)
{
	int ret;
	//
	//create se for this job and insert to se_table holding all global entries
	//
	struct uss_se temp(handle, msai);
	pair<uss_se_table_iterator,bool> retp;
	
	ret = pthread_mutex_lock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	retp = se_table.insert(make_pair(handle, temp));
	
	ret = pthread_mutex_unlock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	if(retp.second == false)
	{
		dexit("add_job: failed to create an se entry with handle");
		return USS_CONTROL_SCHED_DECLINED;
	}
	else
	{
	//
	//insert to best multiqueue
	//
	/*
	 *it is ok to work with pointers here, they remain valid in stl map/set (do mutex protection bof find)
	 *removing a job/handle/se should be done by daemon thread 
	 *the dispatcher thread only can set a mark in SE that this handle has finished!
	 */
	uss_se_table_iterator selected_se_entry = retp.first;
	uss_rq_matrix_iterator selected_matrix_entry;
	uss_mq *selected_mq;
	uss_mq *insert_mq;
	int ret;
	int add_job_successful = 0;
	for(int i = 0; i<msai.length && i<USS_MAX_MSI_TRANSPORT; i++)
	{
		if(is_accelerator_type_active(msai.accelerator_type[i]))
		{
			//msai is sorted by best accel in first position
			selected_matrix_entry = this->rq_matrix.find(msai.accelerator_type[i]);
			if(selected_matrix_entry == this->rq_matrix.end()) 
			{
				//it is active so getting iterator should work
				dexit("add_job: could not find mq that should exist");
			}
			insert_mq = &(*selected_matrix_entry).second;
			
			ret = this->insert_to_mq(insert_mq, handle);
			if(ret == 0) {add_job_successful = 1; break;}
		}
		else if(msai.accelerator_type[i] == USS_ACCEL_TYPE_CPU)
		{
			/*
			 *WARNING:
			 *cpu affine application are given to the CPU-only queue now!
			 *they are never touched again
			 */
			//ignore at the moment
		}
	}
	if(add_job_successful == 0)
	{
		//LONG TERM: if none has been found then decline this registration thus only se is to remove
		/*
		 *normally three steps would have to be done to remove, but in this case there is
		 *no entry in any mq and the registration controller will delete reg entries
		 *automatically
		 *->just delete se
		 */
		ret = pthread_mutex_lock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_lock\n");}
		
		this->se_table.erase(handle);
		
		ret = pthread_mutex_unlock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_lock\n");}
		dexit("add_job: found ne accelerator for incoming reg (this should not happen in this ver)");
		return USS_CONTROL_SCHED_DECLINED;
	}
	else
	{
		//update best_to_pull lists in all mqs mentioned in this msai 
		/*
		 *also prepare a temporary list that holds all accelerator_types that
		 *are available including their affinity
		 * -> this can be used to distribute the "+1" in the available mq centerpoints
		 */
		map<int,int,less<int> > centerpoint_helper; //[type,affinity]
		for(int i = 0; i<msai.length && i<USS_MAX_MSI_TRANSPORT; i++)
		{
			selected_matrix_entry = this->rq_matrix.find(msai.accelerator_type[i]);
			//getting iterator is allowed to fail then skip to next
			if(selected_matrix_entry != this->rq_matrix.end()) 
			{
				//best_to_pull
				selected_mq = &(*selected_matrix_entry).second;
				if(selected_mq != insert_mq)
				{
					selected_mq->best_to_pull.insert(uss_affinity_list_entry(msai.affinity[i], handle));
				}
				//centerpoint
				centerpoint_helper.insert(make_pair(msai.accelerator_type[i], msai.affinity[i]));
			}
		}
	
		//calculate centerpoint increment
		int centerpoint_sum = 0;
		map<int,int,less<int> >::iterator centerpoint_helper_iterator; //[type,affinity]
		centerpoint_helper_iterator = centerpoint_helper.begin();
		for(; centerpoint_helper_iterator != centerpoint_helper.end(); centerpoint_helper_iterator++)
		{
			centerpoint_sum += (*centerpoint_helper_iterator).second;
		}
		
		//update centerpoints in all mqs relative to sum
		centerpoint_helper_iterator = centerpoint_helper.begin();
		for(; centerpoint_helper_iterator != centerpoint_helper.end(); centerpoint_helper_iterator++)
		{
			selected_matrix_entry = this->rq_matrix.find((*centerpoint_helper_iterator).first);
			//getting iterator is NOT allowed to fail
			if(selected_matrix_entry != this->rq_matrix.end()) 
			{
				//centerpoint
				selected_mq = &(*selected_matrix_entry).second;
				selected_mq->centerpoint += ((double)(*centerpoint_helper_iterator).second / (double)centerpoint_sum);
			}
			else
			{
				dexit("add_job: centerpoint_helper contains type that should exist");
			}
		}
		
		//call periodic_tick
		this->periodic_tick();

		#if(USS_DAEMON_DEBUG == 1)
		printf("[main thread] scheduler job with handle %i enqueued\n", handle);	
		#endif
		return USS_CONTROL_SCHED_ACCEPTED;
	}
	}
}

/*
 *called to completely remove all traces of a handle that has finished
 *from the USS
 */
int uss_scheduler::remove_job(int handle)
{
	int ret;
	//1) remove from any mq and rq
	/*
	 *remove_from mq returns the nof elements removed
	 *in general it is possible that an handle cannot be
	 *removed because it is current now
	 *->this should not happen in this version
	 */
	ret = pthread_mutex_lock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}	
	
	uss_mq *selected_mq = get_mq_of_handle(handle);
	if(selected_mq == NULL) {dexit("remove_job: null-pointer");}
	
	ret = pthread_mutex_unlock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
	
	ret = remove_from_mq(selected_mq, handle);
	if(ret == 0) {dexit("remove_job: rem failed, but in this version this must not happen");}
	/*
	 *also clean topull list
	 */
	ret = pthread_mutex_lock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}	
	
	uss_rq_matrix_iterator selected_matrix_entry;
	uss_se_table_iterator selected_se_table_entry = this->se_table.find(handle);
	if(selected_se_table_entry == this->se_table.end()) {dexit("remove job: no se of handle");}
	uss_se *selected_se = &(*selected_se_table_entry).second;
	
	if(selected_se == NULL) {dexit("remove_job: se doesn't exist any more but it should still be around");}
	struct meta_sched_addr_info msai = (selected_se->msai);
	
	ret = pthread_mutex_unlock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
	
	map<int,int,less<int> > centerpoint_helper; //[type,affinity]
	for(int i = 0; i<msai.length && i<USS_MAX_MSI_TRANSPORT; i++)
	{
		selected_matrix_entry = this->rq_matrix.find(msai.accelerator_type[i]);
		//getting iterator is allowed to fail then skip to next
		if(selected_matrix_entry != this->rq_matrix.end()) 
		{
			selected_mq = &(*selected_matrix_entry).second;
			selected_mq->best_to_pull.erase(uss_affinity_list_entry(msai.affinity[i], handle));
			//centerpoint
			centerpoint_helper.insert(make_pair(msai.accelerator_type[i], msai.affinity[i]));
		}
	}
	
	//calculate centerpoint decrement
	int centerpoint_sum = 0;
	map<int,int,less<int> >::iterator centerpoint_helper_iterator; //[type,affinity]
	centerpoint_helper_iterator = centerpoint_helper.begin();
	for(; centerpoint_helper_iterator != centerpoint_helper.end(); centerpoint_helper_iterator++)
	{
		centerpoint_sum += (*centerpoint_helper_iterator).second;
	}
	
	//update centerpoints in all mqs relative to sum
	centerpoint_helper_iterator = centerpoint_helper.begin();
	for(; centerpoint_helper_iterator != centerpoint_helper.end(); centerpoint_helper_iterator++)
	{
		selected_matrix_entry = this->rq_matrix.find((*centerpoint_helper_iterator).first);
		//getting iterator is NOT allowed to fail
		if(selected_matrix_entry != this->rq_matrix.end()) 
		{
			//best_to_pull
			selected_mq = &(*selected_matrix_entry).second;
			selected_mq->centerpoint -= ((double)(*centerpoint_helper_iterator).second / (double)centerpoint_sum);
		}
		else
		{
			dexit("remvome_job: centerpoint_helper contains type that should exist");
		}
	}	
	
	//2) remove se entry
	ret = pthread_mutex_lock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	this->se_table.erase(handle);
	
	ret = pthread_mutex_unlock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	//3) remove entries in reg_addr table!
	rc->remove_reg_addr_entry(handle);
	
	//4) tell registration controller that this handle is free and can be reused
	ret = pthread_mutex_lock(&(rc->handle_mutex));
	if(ret != 0) {dexit("problem with pthread_mutex_lock");}
	
	rc->reuse_handles.insert(handle);
	
	ret = pthread_mutex_unlock(&(rc->handle_mutex));
	if(ret != 0) {dexit("problem with pthread_mutex_unlock");}
	
	return 0;
}


//////////////////////////////////////////////
//											//
// MID TERM SCHEDULING 						//
//											//
//////////////////////////////////////////////
// the mid term scheduler is responsible for
// updating the time and each runqueue
/***************************************\
* time keeping functions				*
\***************************************/
/*
 * on multiple occasions update_time will be called
 * to keep the time
 */
void uss_scheduler::update_time()
{
	//main time value
	struct timespec ts;
	if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {dexit("update_time() failed");}
	
	uint64_t t1 = ts.tv_nsec;
	uint64_t t2 = ts.tv_sec*(1000000000);
	
	this->clock.time = (t1+t2);
}

/*
 * elevate CPU load to use this as a scheduling parameter
 */
void uss_scheduler::update_sysload()
{
	#if(USS_SYSLOAD_FROM_PROC == 1)
	/*
	 *this reads the current number of processes
	 *that are using or waiting on a processor (all CPUs of the system)
	 *
	 *it is devided by the number of CPU in the system and stored in sysload
	 */
	//WARNING: not yet implemented

	#elif(USS_SYSLOAD_FROM_SYSCALL == 1)
	/*
	 *this reads only loadavg (avg over 1 minute)
	 */
	struct sysinfo si;
	sysinfo(&si);
	this->sysload_current = si.loads[0];
	#endif
	
	//bluemode: check if CPU average runqueues are over config value
	#if(USS_BLUEMODE == 1)
	bluemode = 1;
	#endif
	/*WARNING USING PERMANENT BLUEMODE NOW
	if(this->sysload_current <= USS_CPU_THRESHOLD)
	{this->bluemode = 1;}
	else
	{this->bluemode = 0;}
	*/
}

/***************************************\
* mid term functions					*
\***************************************/
/*
 * calculate vruntime
 */
void uss_scheduler::update_runtime(uss_rq *rq, uss_se *current_se)
{
	if(rq == NULL || current_se == NULL) {dexit("update_runtime: nullpointer");}
	//(A) update SE
	/*
	 *we compare the min_granularity against the real runtime to ensure that
	 *even a low priorized handles gets enough time thats more than is init/cleanup
	 *cost
	 *=> a low prio process may accumulate much vruntime when selected and high prio
	 *   processes will run significantly longer after that
	 *
	 *WARNING: 
	 *no load weights or prio considered in vruntime yet => vruntime = realtime!
	 */
	class uss_nanotime delta_exec;
	class uss_nanotime delta_exec_weightend;	
	class uss_nanotime previous_vruntime;
	
	//save old vruntime for later
	previous_vruntime.time = current_se->vruntime.time;
	
	delta_exec.time = (this->clock.time - rq->curr.exec_start.time);
	delta_exec_weightend.time = (delta_exec.time * 1); /*WARNING: later use function here for prio/loadw*/
	
	current_se->rruntime.time += delta_exec.time;
	current_se->vruntime.time += delta_exec_weightend.time;
	rq->curr.exec_start.time = this->clock.time;
	
	//(B) update RQ
	rq->tree.erase(uss_rq_tree_entry(previous_vruntime, current_se->handle));
	rq->tree.insert(uss_rq_tree_entry(current_se->vruntime, current_se->handle));	
}

/*
 * main scheduling logic is here
 */
void uss_scheduler::update_curr(uss_rq *rq)
 {
	//
	//check if there is something running at all
	//
	int ret;
 
	if(rq->curr.handle <= 0) 
	{
		//if nothing is running pick next!
		/*
		 *normally a rq can only be idle because a cleanup message had been 
		 *received and no more element was there!
		 *-> so it is safe to pick a next element
		 *   (even when the quick thread changes the current, there will be always a value 
		 *    larger than 0 and if an accel is idle a further CU will never be received)
		 */
		struct uss_message m;

		m.message_type = 0;
		m.accelerator_type = rq->accelerator_type;
		m.accelerator_index = rq->accelerator_index;

		pick_next(m);
	}
	else
	{		
		//MUTEX PROTECTED AGAINST pick_next()
		ret = pthread_mutex_lock(&rq->tree_mutex);
		if(ret != 0) {dexit("thread_mutex_lock\n");}
		
		int current_handle = rq->curr.handle;
		if(current_handle > 0) /*rq may have become empty since last check*/
		{
			//
			//refresh the r(eal)runtime and vruntime of current_handle
			//
			ret = pthread_mutex_lock(&this->se_mutex);
			if(ret != 0) {dexit("thread_mutex_lock\n");}
			
			uss_se_table_iterator current_se_table_entry = this->se_table.find(current_handle);
			if(current_se_table_entry == this->se_table.end()) dexit("insert: se of handle NA");
			uss_se *current_se = &(*current_se_table_entry).second;	

			update_runtime(rq, current_se);

			//
			//if leftmost is NOT current and in CPU-mode then make it idle
			//
			/*
			 *three conditions have to be met
			 *1) leftmost not current (then it can't be in CPU-mode)
			 *2) should not be finished
			 *3) is in CPU-mode (this is the goal)
			 *4) this SE/handle hasn't been issued to leave CPU-mode
			 */
			uss_rq_tree_iterator leftmost = rq->tree.begin();
			int leftmost_handle = (*leftmost).handle;
			uss_se_table_iterator leftmost_se_table_entry = this->se_table.find(leftmost_handle);
			uss_se *leftmost_se = &(*leftmost_se_table_entry).second;	
			
			if(leftmost_handle != current_handle 
				&& leftmost_se->is_finished == 0 
				&& leftmost_se->execution_mode == USS_ACCEL_TYPE_CPU
				&& leftmost_se->already_send_free_cpu == 0)
			{
				struct uss_address addr;
				struct uss_message mess;
				
				addr = rc->get_address_of_handle(leftmost_handle);
				
				mess.message_type = USS_MESSAGE_RUNON;
				mess.accelerator_type = USS_ACCEL_TYPE_IDLE;
				mess.accelerator_index = 0;
				
				ret = this->cc->send(addr, mess);
				if(ret == -1) {dexit("update_curr: failed to send message");}

				leftmost_se->next_execution_mode = USS_ACCEL_TYPE_IDLE;
				leftmost_se->already_send_free_cpu = 1;			
			}
			//
			//check if a message has to be send to preempt current after update done
			//
			/*
			 *three conditions have to be met
			 *1) a) the leftmost entry is not the current handle 
			 *   b) OR marked by load_balancer
			 *		BUT ONLY IF: leftmost is not finished!
			 *2) the current minimal granularity has been depleted
			 *3) no message has been send before
			 *4) NEEDED? leftmost_se->execution mode should be idle (ensure that it has been prepared)
			 *
			 */
			if( ((leftmost_handle != current_handle || rq->curr.marked_runon_idle) && leftmost_se->is_finished == 0)
				&& current_se->rruntime.time >= current_se->min_granularity.time 
				&& rq->curr.already_send_message == 0)
			{
				struct uss_address addr;
				struct uss_message mess;
				
				addr = rc->get_address_of_handle(current_handle);
				
				/*BLUEMODE*/
				int selected_idle_mode = USS_ACCEL_TYPE_IDLE;
				#if(USS_BLUEMODE == 1)
				if(bluemode == 1)
				{selected_idle_mode = USS_ACCEL_TYPE_CPU;}
				else
				{selected_idle_mode = USS_ACCEL_TYPE_IDLE;}
				#endif
				
				mess.message_type = USS_MESSAGE_RUNON;
				mess.accelerator_type = selected_idle_mode;
				mess.accelerator_index = 0;
				
				ret = this->cc->send(addr, mess);
				if(ret == -1) {dexit("update_curr: failed to send message");}
				
				#if(USS_FILE_LOGGING == 1)
				struct timeval tv;
				gettimeofday(&tv, NULL);
				int fd = open("./benchmark/usslog", O_WRONLY | O_APPEND);
				if(fd == -1) {dexit("dlog");}
				char buf[100] = {0x0};
				sprintf(buf, "%ld %ld : sending message runon with %i to handle %i done\n", 
						(long)tv.tv_sec, (long)tv.tv_usec, selected_idle_mode, current_handle);
				write(fd, buf, strlen(buf));
				close(fd);
				#endif
	
				current_se->next_execution_mode = selected_idle_mode;
				rq->curr.already_send_message = 1;
			}
			
			ret = pthread_mutex_unlock(&this->se_mutex);
			if(ret != 0) {dexit("thread_mutex_lock\n");}
			
			//
			//check if a rebound needs to be send
			//
			/*
			 *three conditions have to be met
			 *1) a message has been send before
			 *2) the leftmost entry is the current handle
			 *3) no rebound message has been send before
			 *
			 *COMMENT:
			 *the rebound signals should be discarded in ALL 
			 *other places(library:read_message) except the
			 *place(library:read_message) in loop around main
			 *
			 *WARNING: 
			 *NOT IMPLEMENTED YET because this is a rare case 
			 *esp due to minimal granularity it is unlikely that
			 *a handle will be leftmost again
			 * -> only when second element of a rq with two elements 
			 *    is loadbalanced
			 */
		}
		 
		ret = pthread_mutex_unlock(&rq->tree_mutex);
		if(ret != 0) {dexit("thread_mutex_unlock\n");}		 
		 
	 }
 }
 
void uss_scheduler::periodic_tick()
{
	//printf("TICK\n");
	//print_rq();
	
	//
	//update time
	//
	this->update_time();

	//
	//for EACH run queue do
	//
	uss_rq_matrix_iterator rq_matrix_entry = this->rq_matrix.begin();
	for(; rq_matrix_entry != this->rq_matrix.end(); rq_matrix_entry++)
	{
		uss_mq *selected_mq = &(*rq_matrix_entry).second;
		uss_rq_list_iterator rq_list_entry = selected_mq->list.begin();
		for(; rq_list_entry != selected_mq->list.end(); rq_list_entry++)
		{
			uss_rq *selected_rq = &(*rq_list_entry).second;
			//perform update_curr for each rq
			/*
			 *[1] check if none is running, then pick next
			 *[2] update vruntime of currently running se 
			 */
			this->update_curr(selected_rq);
			//printf("update_curr called for [[rqtype=%i rqindex=%i]]\n", selected_rq->accelerator_type, selected_rq->accelerator_index);
		}
	}
}

/***************************************\
* load balancing						*
\***************************************/
/*
 * this is a small helper that return a min_push_affinity
 * out of a given uss_push_curve
 * (therefore the load_balancing logic needn't know about the
 *  implementation of a push_curve)
 */
int uss_scheduler::get_value_from_push_curve(uss_push_curve *pc, int x)
{
	if(x < 0) 
	{
		dexit("get_value_from_push_curve: called with negative x");
		return 0;
	}
	else if(x >= 0 && x < USS_MAX_PUSH_CURVE_LEN)
	{
		//return function value of curve
		return pc->min_push_affinity[x];
	}
	else
	{
		//return the last function value (it is chosen for all large values that exceed curve size)
		return pc->min_push_affinity[(USS_MAX_PUSH_CURVE_LEN - 1)];
	}
}

/*
 * >load balancing<
 *
 * this uses a push/pull paradigm
 *
 *pull: 
 * if a rq is empty (or about to go empty)
 *
 *push: 
 * depending on a given per-accel curve and a centerpoint
 * a too-full mq moves handles to other mqs
 */
void uss_scheduler::load_balancing()
{
	int ret = -1;
	uss_mq *selected_mq = NULL;
	uss_rq_list_iterator selected_rq_list_entry;
	uss_rq *selected_rq = NULL;
	uss_se *selected_se = NULL;
	int topull_handle, topush_handle;
	uss_mq *source_mq = NULL;
	uss_rq *source_rq = NULL;	
	
	//pull
	/*
	 *to refill empty rqs is very time critical to do it first
	 */
	uss_rq_matrix_iterator selected_rq_matrix_entry = this->rq_matrix.begin();
	for(; selected_rq_matrix_entry != this->rq_matrix.end(); selected_rq_matrix_entry++)
	{
		//do check
		/*while iterating through all mqs if (#elements/#rq) is less than 1 there has
		 *to be en empty queue
		 *(each mq is balanced inside with every insertion or removal operation)
		 */
		selected_mq = &(*selected_rq_matrix_entry).second;
		if(((double)selected_mq->nof_all_handles / (double)selected_mq->nof_rq) < (double)1)
		{
			//go through all rqs to find empty rq (there may be more than one thats empty)
			selected_rq_list_entry = selected_mq->list.begin();
			for(; selected_rq_list_entry != selected_mq->list.end(); selected_rq_list_entry++)
			{
				selected_rq = &(*selected_rq_list_entry).second;
				if(selected_rq->length == 0)
				{
					//pick a new handle for selected empty rq
					uss_affinity_list_des_iterator selected_affinity_list_entry = selected_mq->best_to_pull.begin();
					for(; selected_affinity_list_entry != selected_mq->best_to_pull.end(); selected_affinity_list_entry++)
					{
						//get handle of current affinity list element
						topull_handle = (*selected_affinity_list_entry).handle;

						ret = pthread_mutex_lock(&this->se_mutex);
						if(ret != 0) {dexit("thread_mutex_lock\n");}						
						
						source_mq = get_mq_of_handle(topull_handle);
						source_rq = get_rq_of_handle(topull_handle);
						
						ret = pthread_mutex_unlock(&this->se_mutex);
						if(ret != 0) {dexit("thread_mutex_unlock\n");}
						
						ret = move_to_rq(topull_handle, 
										selected_mq, selected_rq, //target is pulling rq
										source_mq, source_rq); //source is the rq currently holding topull_handle
						
						if(ret == 1) {break;} //success
					}
				}
			}//end: all rq of a mq refilled if possible
		}
	}//end: pull for all mq of system
	
	/*
	 *do an immediate update to activate rq's that were now refilled
	 */
	periodic_tick();
	
	//push
	/*
	 *secondary functionality to avoid too full lists by
	 *moving the 'best to mirgrate' handles to their second best accelerator
	 *
	 *all handles of the best_to_push list are sorted by ascending affinity
	 *causing the 'worst' handles to accumulate at the beginning of the list
	 *-> all X elements with X = (nof_mq_element - CP)
	 *   try to move them to another mq
	 *   -> a curve says that depending on the value of X only handles with an
	 *      good alternative accelerator are pushed away
	 */
	selected_rq_matrix_entry = this->rq_matrix.begin();
	for(; selected_rq_matrix_entry != this->rq_matrix.end(); selected_rq_matrix_entry++)
	{
		//do check
		/*
		 *while iterating through all mqs calculate the X for each of them
		 *and if X is positive try a push
		 */
		selected_mq = &(*selected_rq_matrix_entry).second;
		int centerpoint = ceil(selected_mq->centerpoint);
		int x = selected_mq->nof_all_handles - centerpoint;
		if(x > 0 && selected_mq->nof_all_handles > selected_mq->nof_rq)
		{
			//pushing necessary -> try to push ONE of X elements away
			/*
			 *(second condition: dont push when a rq would be idle after push)
			 *
			 *get the min_push_affinity from curve for this accelerator technology
			 *this is a function of the number of handles above the centerpoint (X)
			 *
			 *min_push_affinity = f(X)
			 *
			 *to achieve a good balance, we only push to other accelerators that also have
			 *a good affinity -> an affinity that is at least min_push_affinity
			 *
			 *if X grows (ie the rq get too full) we are more likely willing to push to a rq
			 *that has bad performance
			 */
			uss_push_curve *selected_push_curve = this->push_curve[selected_mq->accelerator_type];
			if(selected_push_curve == NULL) {dexit("load_balancing: no push curve");}
			int min_push_affinity = get_value_from_push_curve(selected_push_curve, x);
			
			int nof_tries = 0; int loop_condition = 0; int push_only_one_per_mq = 0;
			uss_affinity_list_asc_iterator selected_affinity_list_entry = selected_mq->best_to_push.begin();			
			for(; (push_only_one_per_mq == 0 && selected_affinity_list_entry != selected_mq->best_to_push.end()); selected_affinity_list_entry++)
			{
				topush_handle = (*selected_affinity_list_entry).handle;
				
				ret = pthread_mutex_lock(&this->se_mutex);
				if(ret != 0) {dexit("thread_mutex_lock\n");}	
			
				uss_se_table_iterator selected_se_table_entry = this->se_table.find(topush_handle);
				if(selected_se_table_entry == this->se_table.end()) {dexit("lb: no se of handle");}
				selected_se = &(*selected_se_table_entry).second;	
				
				loop_condition = ((selected_se->execution_mode == USS_ACCEL_TYPE_IDLE 
									|| selected_se->execution_mode == USS_ACCEL_TYPE_CPU)				
									&& selected_se->is_finished == 0);

				ret = pthread_mutex_unlock(&this->se_mutex);
				if(ret != 0) {dexit("thread_mutex_unlock\n");}	
				
				if(loop_condition)
				{
					/*
					 *search topush_handles msai but just as long as min_push_affinity permits
					 *(the scheduling info are sorted by descending affinity, so we can
					 * stop when threshold is reached)
					 */
					ret = pthread_mutex_lock(&this->se_mutex);
					if(ret != 0) {dexit("thread_mutex_lock\n");}	
					
					struct meta_sched_addr_info *msai = &(selected_se->msai);
					
					ret = pthread_mutex_unlock(&this->se_mutex);
					if(ret != 0) {dexit("thread_mutex_unlock\n");}	
					
					int to_try_affinity, to_try_accelerator;
					for(int i = 0; i < msai->length; i++)
					{
						to_try_affinity = msai->affinity[i]; 
						if(to_try_affinity < min_push_affinity) {continue;}
						
						to_try_accelerator = msai->accelerator_type[i];
						if(to_try_accelerator != selected_mq->accelerator_type &&
						   is_accelerator_type_active(to_try_accelerator))
						 {
							//specify target where the topush_handle should be moved
							uss_rq_matrix_iterator target_rq_matrix_entry = this->rq_matrix.find(to_try_accelerator);
							if(target_rq_matrix_entry == this->rq_matrix.end()) {dexit("lb: rq_matrix_entry should exist");}
							uss_mq *target_mq = &(*target_rq_matrix_entry).second;
							
							uss_rq_list_iterator target_rq_list_entry = target_mq->list.find(get_best_rq_of_mq(target_mq));
							uss_rq *target_rq = &(*target_rq_list_entry).second;
							
							ret = pthread_mutex_lock(&this->se_mutex);
							if(ret != 0) {dexit("thread_mutex_lock\n");}
							
							uss_mq *source_mq = get_mq_of_handle(topush_handle);
							uss_rq *source_rq = get_rq_of_handle(topush_handle);
							
							ret = pthread_mutex_unlock(&this->se_mutex);
							if(ret != 0) {dexit("thread_mutex_unlock\n");}
							//move!
							ret = 0;
							ret = move_to_rq(topush_handle,
											target_mq, target_rq,
											source_mq, source_rq);
							if(ret > 0) {push_only_one_per_mq = 1; break;} //move success done with with handle
							else {continue;} //move may have failed because it became active in the meantime
						 }
					}//end: tries all alternative accelerators for this to topush_handle
				}
				
				nof_tries++;
				if(nof_tries == x) {break;}
			}//end: loop over X last affinity_list elements
		}
	 }//end: push for all mq of system
}


/***************************************\
* remover called by daemon thread		*
\***************************************/
void uss_scheduler::remove_finished_jobs()
{
	set<int>::iterator it;
	//loop and remove at most 100 finished handles from the system
	for(int i = 0; i<1000; i++)
	{
		it = tokill_list.begin();
		if(it == tokill_list.end()) {break;}
#if(USS_DAEMON_DEBUG == 1)
		printf("-><- removing (%i)\n",(*it));
#endif		
		remove_job((*it));

		tokill_list.erase(it);
	}
}
 
//////////////////////////////////////////////
//											//
// SHORT TERM SCHEDULING 					//
//											//
//////////////////////////////////////////////
// the short term scheduler is responsible for
// quickly performing the acutal switch on an
// accelerator when a new signal is recieved
// -> this will be its own thread as it receives
//    all messages!

/***************************************\
* quick response functions				*
\***************************************/
/*
 * this handle send a message that it finished all its work
 * and needs to carefully removed (not by quick dispatcher thread!)
 * -> it is marked in the SE as 'is_finished' 
 * => a finished handle will never be picked to run again because
 *    1) the handle invalidated in this function could not have been scheduled 
 *       anyhow before this cleanup/finished message arrived
 *       because it was current
 *    2) load balancing may happen when the "finished-check"
 *		 happend before this invalidationt and the "current-check" after the pick_next
 *		 but this doesn't matter
 * the schedulers tokill_list contains all handles that can be safely removed by
 * daemons main loop
 */
void uss_scheduler::handle_cleanup(int handle, int is_finished)
{
	int ret;
	ret = pthread_mutex_lock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	uss_se_table_iterator selected_se_table_entry = this->se_table.find(handle);
	if(selected_se_table_entry == this->se_table.end()) dexit("pick_next: no se for handle");
	uss_se *selected_se = &(*selected_se_table_entry).second;
	
	//always do this (a cleanup mess has made next_exec_mode IDLE or CPU)
	selected_se->execution_mode = selected_se->next_execution_mode;
	
	//if we in CPU-mode a cleanup indicated CPU-release
	if(selected_se->already_send_free_cpu == 1) {selected_se->already_send_free_cpu = 0;}
	
	//just update the vruntime for the element that ran on this rq until now
	//int previous_handle = selected_rq->curr.handle;
	//if(previous_handle != -1)
	//{
	//	update_runtime(selected_rq, );	
	//}
	
	if(is_finished)
	{
		selected_se->is_finished = 1;
		tokill_list.insert(handle);
	}
	
	ret = pthread_mutex_unlock(&this->se_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
}

/*
 * pick next selects leftmost entry rq
 * -> the rq(accel_type, index) is selected depending
 *    on information inside of message
 */
void uss_scheduler::pick_next(struct uss_message m)
{
	int ret;
	//
	//don't pick next for cpu (only happens on is_finished)
	//
	if(m.accelerator_type == USS_ACCEL_TYPE_CPU) return;
	
	//
	//go to proper rq and pick leftmost handle as next to run on (accel_type, index)
	//
	uss_rq_matrix_iterator selected_uss_rq_matrix_entry;
	selected_uss_rq_matrix_entry = this->rq_matrix.find(m.accelerator_type);
	if(selected_uss_rq_matrix_entry == this->rq_matrix.end()) dexit("pick_next: no such rq");
	
	uss_rq_list_iterator selected_uss_rq_list_entry;
	selected_uss_rq_list_entry = (*selected_uss_rq_matrix_entry).second.list.find(m.accelerator_index);
	if(selected_uss_rq_list_entry == (*selected_uss_rq_matrix_entry).second.list.end()) dexit("pick_next: no such rq");
	
	uss_rq *selected_rq = &(*selected_uss_rq_list_entry).second;
	
	int picked_handle = 0;
	int next_found = 0;
	
	ret = pthread_mutex_lock(&selected_rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_lock\n");}
	
	//pick leftmost tree_entry
	uss_rq_tree_iterator selected_tree_entry = (*selected_rq).tree.begin();
	if(selected_tree_entry == (*selected_rq).tree.end()) {next_found = -1;}
		
	while(next_found == 0)
	{
		picked_handle = (*selected_tree_entry).handle;
		
		//get handle's se to check if it is a finished one
		ret = pthread_mutex_lock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_lock\n");}
		
		uss_se_table_iterator picked_se_table_entry = this->se_table.find(picked_handle);
		if(picked_se_table_entry == this->se_table.end()) dexit("pick_next: no se for handle (picked)");
		uss_se *picked_se = &(*picked_se_table_entry).second;
		
		if(picked_se->is_finished == 0 && picked_se->execution_mode == USS_ACCEL_TYPE_IDLE)
		{
			//chose this one!
			next_found = 1;
	
			//
			//update this rq's curr structure
			//
			selected_rq->curr.handle = picked_handle;
			selected_rq->curr.marked_runon_idle = 0;
			selected_rq->curr.already_send_message = 0;
			selected_rq->curr.exec_start = this->clock;
			
			//
			//update this se's status and set min_granularity for this run!
			//
			picked_se->execution_mode = m.accelerator_type;
			/*
			 *min_inc_granularity= deltavruntime + 2xloadtime + abg
			 *WARNING:
			 *(2xloadtime is const=50ms now, late measure it)
			 */
			uint64_t delta = 0;
			selected_tree_entry++;
			if(selected_tree_entry != (*selected_rq).tree.end())
			{
				int secondbest_handle = (*selected_tree_entry).handle;
				
				uss_se_table_iterator secondbest_se_table_entry = this->se_table.find(secondbest_handle);
				if(secondbest_se_table_entry == this->se_table.end()) dexit("pick_next: no se for handle (secb)");
				uss_se *secondbest_se = &(*secondbest_se_table_entry).second;
				
				if(picked_se->rruntime.time < secondbest_se->rruntime.time) 
				{delta = secondbest_se->rruntime.time - picked_se->rruntime.time;}
			}
			picked_se->min_granularity.time = delta
											+ (uint64_t)50000000 
											+ ((uint64_t)(this->min_granularity[m.accelerator_type])*1000)
											+ picked_se->rruntime.time; //careful later this se's rruntime is checked again
			//
			//send message via cc to uss_address
			//
			struct uss_message n;
			n.message_type = USS_MESSAGE_RUNON;
			n.accelerator_type = m.accelerator_type;
			n.accelerator_index = m.accelerator_index;
		
			ret = this->cc->send(rc->get_address_of_handle(picked_handle), n);	
			if(ret == -1) {dexit("update_curr: failed to send message");}
		
			#if(USS_FILE_LOGGING == 1)
			struct timeval tv;
			gettimeofday(&tv, NULL);
			int fd = open("./benchmark/usslog", O_WRONLY | O_APPEND);
			if(fd == -1) {dexit("dlog");}
			char buf[100] = {0x0};
			sprintf(buf, "%ld %ld : sending message runon with %i to handle %i done\n", 
					(long)tv.tv_sec, (long)tv.tv_usec, m.accelerator_type, picked_handle);
			write(fd, buf, strlen(buf));
			close(fd);
			#endif
			#if(USS_DAEMON_DEBUG == 1)
			printf("PICK NEXT send RUNON to handle %i accel_type=%i accel_index=%i\n", 
					picked_handle, n.accelerator_type, n.accelerator_index);
			#endif
		}
		else
		{
			//chose next one
			selected_tree_entry++;
			if(selected_tree_entry != (*selected_rq).tree.end())
			{
				//start anew
			}
			else
			{
				next_found = -1;		
			}
		}
		
		ret = pthread_mutex_unlock(&this->se_mutex);
		if(ret != 0) {dexit("thread_mutex_unlock\n");}
	}
	
	switch(next_found)
	{
		case -1:
			//mark this rq's curr structure as IDLE
			selected_rq->curr.handle = -1;
			selected_rq->curr.marked_runon_idle = 0;
			selected_rq->curr.already_send_message = 0;
			selected_rq->curr.exec_start = this->clock;		
			break;
		case 1:
			//do nothing
			break;
		default:
			dexit("pick_next; landed in default case");
			break;
	}
		
	ret = pthread_mutex_unlock(&selected_rq->tree_mutex);
	if(ret != 0) {dexit("thread_mutex_unlock\n");}
	
	return;
}

/*
 * called by quick_dispatcher thread to select 
 * an operation depending on message type
 */
int uss_scheduler::handle_message(struct uss_address a, struct uss_message m)
{
	switch(m.message_type)
	{
	case USS_MESSAGE_NOT_SET:
		//received dummy
		break;
		
	case USS_MESSAGE_CLEANUP_DONE:
		//received cleanup
		this->handle_cleanup(rc->get_handle_of_address(a), 0);
		this->pick_next(m);
		break;
		
	case USS_MESSAGE_STATUS_REPORT:
		//received status report
		break;
		
	case USS_MESSAGE_ISFINISHED:
		//same as cleanup message but mark this handle as is_finished
		this->handle_cleanup(rc->get_handle_of_address(a), 1);
		this->pick_next(m);
		
	default:
		//not set
		break;
	}
	return 0;
}

/***************************************\
* short-term scheduling THREAD			*
\***************************************/
void* quick_dispatcher(void* ptr)
{	
	//detach so this needn't be joined anyhow
	pthread_detach(pthread_self());
	
	//get main classes
	uss_scheduler *sched = (uss_scheduler*) ptr;
	
	//make this a listener
	struct uss_address daemon_addr;
	memset(&daemon_addr, 0, sizeof(struct uss_address));
	
#if(USS_FIFO == 1)	
	daemon_addr.fifo = 1; /* this fill create the unique daemon fifo f1 */
#endif

	int fd_receiver = sched->cc->install_receiver(&daemon_addr);
	if(fd_receiver < 0) {dexit("could not establish the receiver in daemon");}
	
#if(USS_FIFO == 1)	
	//install dummy sender, to keep daemon stable
	int fd_dummy_sender = sched->cc->install_sender(&daemon_addr);
	if(fd_dummy_sender < 0) {dexit("could not establish the dummy sender in daemon");}
#endif	

	int ret = 0;
	struct uss_message m;
	struct uss_address a;
		
	//install successful now listen forever
	while(1)
	{
		memset(&m, 0, sizeof(struct uss_message));
		
		ret = sched->cc->blocking_read(fd_receiver, &a, &m);
		if(ret == -1) {dexit("quick_dispatcher: failed to blocking read message");}
		
		ret = sched->handle_message(a, m);
		if(ret == -1) {dexit("quick_dispatcher: failed to handle a message");}
	}
	return NULL;
}

