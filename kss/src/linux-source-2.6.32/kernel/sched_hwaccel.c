#ifdef CONFIG_SCHED_HWACCEL

#define HWACCEL_LOG_FACILITY KERN_DEBUG

/* cu-information needed by the scheduler
 */
struct computing_unit_info {
	unsigned long id;
	unsigned int type;

	struct list_head	queue_node;
	struct cfs_rq	cfs_rq;

	unsigned long api_device_number;
	/* flag for "lazy umount" of cus */
	int online;
	/* flag the task migration offers */
	int offers_outstanding;
	
	struct hardware_properties hp;
};

DEFINE_PER_CPU(struct computing_unit_info, cpu_cuis);
#define cpu_cui(cpu)		(&per_cpu(cpu_cuis, (cpu)))
struct cpumask cpu_active_cui_mask;

/* The main cu-bookkeeping structure
 */
struct computing_units {
	struct list_head list_of_cus[CU_NUMOF_TYPES];
	/*unsigned long current_ids[CU_NUMOF_TYPES];*/
	unsigned long current_id;
	unsigned long nr_devices;

	struct semaphore access_mutex;
};
struct computing_units computing_units;



/*
 * Base granularities for the task_granularity on the cu types
 * These values are in virtual runtime and should somehow reflect the
 * minimum cost of a task switch on the corresponding cu
 */
static u64 type_granularities_sec[CU_NUMOF_TYPES] = 
{
	(u64)0,  /* CU_TYPE_CPU */  
	(u64)4,  /* CU_TYPE_CUDA */ /* a guess */
	(u64)10  /* CU_TYPE_FPGA */ /* a guess */
};

#define MAX_NSEC_SECS (ULONG_MAX / NSEC_PER_SEC) 

/*
 * Short Helper functions
 */

#define for_each_cu_type(i) for (i=0; i<CU_NUMOF_TYPES; ++i)

/* Found this on crossreference, but his kernel does not seem to have it */
#ifndef task_is_dead
	#define task_is_dead(task)      ((task)->exit_state != 0)
	//#define task_is_dead(task)      ((task)->state == TASK_DEAD || (task)->exit_state == EXIT_ZOMBIE)
#endif

/* src as pointer, dst NOT as pointer! */
#define copy_from_user_or_die(src, dst) if (copy_from_user(&(dst), (src), sizeof(dst))) \
		return -EFAULT;


static inline int is_hardware_cfs_rq(struct cfs_rq *cfs_rq)
{
	/* maxcount is initialized with -1 and only greater than that for hw rqs */
	return (cfs_rq->maxcount >= 0);
}

static inline int number_of_tasks_on(struct computing_unit_info *cui)
{
	return (cui->cfs_rq.nr_running + cui->cfs_rq.count);
}

static inline int computing_unit_is_idle(struct computing_unit_info *cui)
{
	/* The device is idle if there are no tasks running or waiting */
	return (number_of_tasks_on(cui) == 0);
}

static inline int computing_unit_free_slots(struct computing_unit_info *cui)
{
	s64 temp;
	temp = (s64)cui->cfs_rq.maxcount - (s64)number_of_tasks_on(cui);
	if (likely(temp > (unsigned int)INT_MAX))
		return (int) INT_MAX;
	else if (unlikely(temp < INT_MIN))
		return (int) INT_MIN;
	else
		return cui->cfs_rq.maxcount - number_of_tasks_on(cui);
}

static inline int computing_unit_is_busy(struct computing_unit_info *cui)
{
	/* The device is busy if there are more tasks than computing slots */
	return (computing_unit_free_slots(cui) <= 0);
}

static inline int is_cpu_cui(struct computing_unit_info *cui)
{
	/* maxcount is initialized with -1 and only greater than that for hw rqs */
	return (cui->type == CU_TYPE_CPU);
}

static inline int is_queue_congested(struct computing_unit_info *cui)
{
#if (CU_HW_QUEUE_LIMIT == 0)
	/* If we do not allow any queue we have to make sure not to	
	 * inhibit the computation slots...
	 */
	return (!is_cpu_cui(cui) && computing_unit_is_busy(cui) && cui->cfs_rq.nr_running >= CU_HW_QUEUE_LIMIT);
#else
	/* A CPU queue is never congested */
	return (!is_cpu_cui(cui) && cui->cfs_rq.nr_running >= CU_HW_QUEUE_LIMIT);
#endif
}

static inline struct computing_unit_info *cui_of(struct cfs_rq *cfs_rq)
{
	return container_of(cfs_rq, struct computing_unit_info, cfs_rq);
}

static inline void copy_cui_to_cu(struct computing_unit_info *cui, struct computing_unit_shortinfo *cu) {
	cu->handle = cui->id;
	cu->type = cui->type;
	cu->api_device_number = cui->api_device_number;
	cu->online = cui->online;
	cu->count = cui->cfs_rq.count;
	cu->waiting = cui->cfs_rq.nr_running;
}

static inline struct load_weight* get_load_hw(struct computing_unit_info *cui)
{
	if (is_cpu_cui(cui))
		return &cpu_rq(cui->api_device_number)->cfs.load;
	else
		return &cui->cfs_rq.load;
}

#define down_mutex_or_fail(mutex) 	{int status; if ((status = down_killable((mutex))) < 0) return status;}




/*
 * Functions for the original scheduler
 */


/* Change a hardware task's cfs_rq if it moves across CPUs */
static inline void set_task_rq_hw(struct task_struct *p, unsigned int cpu)
{
	if (unlikely(p->hwse.is_hardware_task)) {
		//int this_cpu;
		struct cfs_rq *newrq, *oldrq = task_cfs_hw_rq(p);
		/* Bail out if this task is not currently executing on a CPU */
		if (!is_cpu_cui(cui_of(oldrq)))
			return;
		newrq = &cpu_cui(cpu)->cfs_rq;
		p->hwse.cfs_rq = newrq;
		
		
		if (oldrq != newrq) {
			unsigned long flags;
			
			/* adjust unfairness information */
			s64 vdiff = newrq->min_vruntime - oldrq->min_vruntime;
			p->hwse.vruntime += vdiff;
			
			//this_cpu = get_cpu();
			//printk(HWACCEL_LOG_FACILITY "CPU %u: Moving hardware task %d from %u to %u", this_cpu, p->pid, task_cpu(p), cpu);
		
			/* remove from old queue */
			spin_lock_irqsave(&oldrq->lock, flags);
			oldrq->count--;
			list_del_init(&p->hwse.group_node);
			spin_unlock_irqrestore(&oldrq->lock, flags);
			
			/* add to new queue */
			spin_lock_irqsave(&newrq->lock, flags);
			newrq->count++;
			list_add(&p->hwse.group_node, &newrq->tasks);
			spin_unlock_irqrestore(&newrq->lock, flags);
			
			//put_cpu();
		}
	}
}

/* Prevent a task on a cu from being migrated away. Is being called to a function
 * which relies on the task being in the initially read cfs_rq for the whole function
 */
static inline void pin_task_on_cpu(struct task_struct *p)
{
	unsigned int cpu;
	int status;
	
	cpu = task_cpu(p);
	if ((status = set_cpus_allowed_ptr(p, &cpumask_of_cpu(cpu))) < 0)
		printk(HWACCEL_LOG_FACILITY "error pinning cpu affinity of task %d: %d", p->pid, status);
	//else
		//printk(HWACCEL_LOG_FACILITY "Pinned task %d to cpu %u", p->pid, cpu);
}

/* Reallow task migration by the original CFS
 */
static inline void set_cpu_affinity_hw(struct task_struct *p, unsigned int type)
{
	if (type == CU_TYPE_CPU) {
		int status;
		if ((status = set_cpus_allowed_ptr(p, &cpu_active_cui_mask)) < 0)
			printk(HWACCEL_LOG_FACILITY "error setting cpu affinity of task %d: %d", p->pid, status);
	} else {
		set_cpus_allowed_ptr(p, cpu_present_mask);
	}
}







/*
 * Functions which manage the computing_unit_info structs
 */


static void init_cfs_rq(struct cfs_rq *cfs_rq, struct rq *rq);
static inline void __computing_unit_init(struct computing_unit_info *cui)
{
	init_cfs_rq(&cui->cfs_rq, NULL);
	init_MUTEX(&cui->cfs_rq.access_mutex);
	INIT_LIST_HEAD(&cui->queue_node);
	cui->cfs_rq.count = 0;
	cui->cfs_rq.maxcount = 1;
	cui->online = 1;
}

/*
 * Lock-holding cui iterator. Note: the current device might be
 * deleted so the iterator has to be dequeue-safe. Here we
 * achieve that by always pre-iterating before returning
 * the current device.
 * Assumes that computing_units is currently locked by caller
 * Note: This returns offline units, that is units which may not
 * be allocated currently
 */
static struct computing_unit_info *
__cui_iterator(struct list_head **iterator)
{
	int i;
	struct computing_unit_info *cui = NULL;
	struct list_head *next = *iterator;
	
	/* If we reached the end of another list we have to advance to the next one */
	for_each_cu_type(i) {
		/* If we reached the end of the last list we are done */
		if (next == &computing_units.list_of_cus[CU_NUMOF_TYPES - 1])
			return NULL;
		
		if (next == &computing_units.list_of_cus[i])
			/* This is safe since we filter the unsafe i+1 case for the next type */
			next = computing_units.list_of_cus[i+1].next;
	}
	
	cui = list_entry(next, struct computing_unit_info, queue_node);
	*iterator = next->next;

	return cui;
}

/* allocates enough kernel space for a new cui */
static inline struct computing_unit_info * __computing_unit_alloc_space(void)
{
	return (struct computing_unit_info *)kzalloc(sizeof(struct computing_unit_info), GFP_KERNEL);
}

/* Enables a cui - assumes that caller holds the access_mutex */
static inline void __computing_unit_enable(struct computing_unit_info *cui)
{
	/* This should not happen */
	WARN(cui->type >= CU_NUMOF_TYPES, "__computing_unit_enable: invalid type: %d", cui->type);
	
	/* CPU cus are not really being deleted and thus will not be really added */
	if (is_cpu_cui(cui)) {
		cpumask_set_cpu(cui->api_device_number, &cpu_active_cui_mask);
	}
	
	cui->online = 1;
}

/* Adds a cui to computing_units - assumes that caller holds the access_mutex */
static inline void __computing_unit_add(struct computing_unit_info *cui)
{
	/* This should not happen */
	WARN(cui->type >= CU_NUMOF_TYPES, "__computing_unit_add: invalid type: %d", cui->type);
	
	cui->id = ++computing_units.current_id;
	list_add_tail(&cui->queue_node, &computing_units.list_of_cus[cui->type]);
	computing_units.nr_devices++;
}

/* 
 * Removes a cui from computing_units - assumes that caller holds the access_mutex
 * Performs NO checks to see if the unit in a deleteable state
 */
static inline void __computing_unit_del(struct computing_unit_info *cui)
{
	list_del_init(&cui->queue_node);
	computing_units.nr_devices--;
}

/* 
 * Removes a cui from computing_units - assumes that caller holds the access_mutex
 * Only removes the unit if it is not busy, but then also frees the cui
 * Note: This method acquires the access_mutex of the cfs_rq of cui... Do not call
 * it while holding this mutex!
 */
static inline int __computing_unit_try_to_del(struct computing_unit_info *cui)
{
	/* Never really delete CPU cus */
	if (is_cpu_cui(cui)) {
		cpumask_clear_cpu(cui->api_device_number, &cpu_active_cui_mask);
		return false;
	}
	
	down_mutex_or_fail(&cui->cfs_rq.access_mutex);
	if (computing_unit_is_idle(cui)) {
		__computing_unit_del(cui);
		kfree(cui);
		return true;
	}
	
	up(&cui->cfs_rq.access_mutex);
	return false;
}

/* iterate over all devices to find the id - assumes that caller holds the access_mutex */
static inline struct computing_unit_info * __computing_unit_get_by_id(unsigned long id)
{
	static struct computing_unit_info *cui = NULL;
	struct list_head *iterator = computing_units.list_of_cus[0].next;
	
	do{
		cui = __cui_iterator(&iterator);
	} while (cui != NULL && cui->id != id);
	
	return cui;
}

/* iterate over all devices to find the type / api_device_number - assumes that caller holds the access_mutex */
static inline struct computing_unit_info * __computing_unit_get(unsigned int type, unsigned long api_device_number)
{
	static struct computing_unit_info *cui = NULL;
	struct list_head *iterator = computing_units.list_of_cus[type].next;
	
	do{
		cui = __cui_iterator(&iterator);
		if (cui == NULL || cui->type != type)
			return NULL;
	} while (cui->api_device_number != api_device_number);
	
	return cui;
}

/* Function to initialize the cpus as a computing unit - called from sched_init */
void __init computing_units_cpus_init(void)
{
	int i;
	
	/* init the cfs runqueues for the processors, with a
	 * maxcount as high as possible
	 */
	for_each_cpu(i, cpu_present_mask) {
		__computing_unit_init(cpu_cui(i));
		cpu_cui(i)->cfs_rq.maxcount = UINT_MAX;
		cpu_cui(i)->hp.concurrent_kernels = UINT_MAX;
		cpu_cui(i)->type = CU_TYPE_CPU;
		cpu_cui(i)->api_device_number = i;
		
		cpu_cui(i)->cfs_rq.rq = cpu_rq(i);
		
		__computing_unit_add(cpu_cui(i));
	}
	
	cpumask_copy(&cpu_active_cui_mask, cpu_present_mask);
}





/*
 * Semaphore locking mechanism functions
 *
 * Note: The maximum number of allowed kernels for a cu may change at runtime,
 * therefore the lock has to allow a dynamic count of tasks on the cu. To
 * achieve that, this locks count grows up instead of down. Hence count is
 * initialized with 0 and new tasks are allowed onto the cu if 
 * count < maxcount
 */

 
 


/* Initialize the lock */
static inline void cfs_sema_init(struct cfs_rq *cfs_rq, int val)
{
	static struct lock_class_key __key;
	cfs_rq->lock	= __SPIN_LOCK_UNLOCKED((*cfs_rq).lock);
	cfs_rq->count	= 0;
	cfs_rq->maxcount = val;
	lockdep_init_map(cfs_rq->lock.dep_map, "cfs_rq->lock", &__key, 0);
}

/*
 * Because this function is inlined, the 'state' parameter will be
 * constant, and thus optimised away by the compiler.  Likewise the
 * 'timeout' parameter for the cases without timeouts.
 *
 * This must be called while holding cfs_rq->lock AND cfs_rq->access_mutex
 */
static inline int __sched __cu_down_common(
		struct cfs_rq *cfs_rq, struct sched_entity *hwse,
		long state, long timeout)
{
	struct task_struct *task = current;
	hwse->semaphore_up = 0;
	
	for (;;) {
		if (signal_pending_state(state, task))
			goto interrupted;
		if (timeout <= 0)
			goto timed_out;
		__set_task_state(task, state);
		spin_unlock_irq(&cfs_rq->lock);
		up(&cfs_rq->access_mutex);
		timeout = schedule_timeout(timeout);
		down_mutex_or_fail(&cfs_rq->access_mutex);
		spin_lock_irq(&cfs_rq->lock);
		if (hwse->need_migrate)
			return 1;
		if (hwse->semaphore_up)
			return 0;
	}
	
 timed_out:
	return -ETIME;

 interrupted:
	return -EINTR;
}
static noinline int __sched __cu_down(struct cfs_rq *cfs_rq, struct sched_entity *hwse)
{
	return __cu_down_common(cfs_rq, hwse, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
}

/*
 * Mechanism to lock a cu by acquiring the underlying semaphore-like lock
 */
static int cu_down(struct cfs_rq *cfs_rq, struct sched_entity *hwse)
{
	unsigned long flags;
	int need_dequeue = 1;
	int retval = 0;
	struct task_struct *task = task_of_hw(hwse);
	
	/* if there is no room on the cui and the task is able to run elsewhere, fail to acquire
	 * !hwse->need_migrate ensures that we do not deadloop here...
	 */
	if (!hwse->need_migrate && 
	    hwse->current_affinity < SINGLE_IMPLEMENTATION_BONUS && 
			is_queue_congested(cui_of(cfs_rq)))
	{
		//printk(HWACCEL_LOG_FACILITY "Insta-Migrating task %d with affinity %d away from %s, because queue is congested with %d", task->pid, hwse->current_affinity, cu_type_to_const_char(cui_of(cfs_rq)->type), cfs_rq->nr_running);
		hwse->need_migrate = 1;
		return 1;
	}
	
	/* 
	 * enqueue before aquiring the spinlock because we
	 * do not know if enqueue may cause blocking
	 *
	 * If no enqueue was necessary (ie we get the semaphore
	 * without waiting) then we dequeue ourselves afterwards
	 */
	enqueue_task_fair_hw(cfs_rq, task);

	/*
	 * Obtain the semaphore-like lock
	 * As stated above this _increases_ the count
	 */
	spin_lock_irqsave(&cfs_rq->lock, flags);
	hwse->need_migrate = 0;
	if (likely(cfs_rq->count < cfs_rq->maxcount))
		cfs_rq->count++;
	else {
		/*printk(HWACCEL_LOG_FACILITY "Waiting for the lock... Now running %u tasks while %lu are waiting\n", cfs_rq->maxcount - cfs_rq->count, cfs_rq->nr_running);*/
		retval = __cu_down(cfs_rq, hwse);
		need_dequeue = (retval < 0);
	}
	spin_unlock_irqrestore(&cfs_rq->lock, flags);

	if (need_dequeue) {
		/* dequeue if an error occured (retval < 0)
		 * or if we got the semaphore without waiting...
		 * If we had to wait and have been woken up then "free"
		 * already dequeued us
		 *
		 * dequeue after releasing the spinlock because we
		 * do not know if dequeue may cause blocking
		 */
		dequeue_task_fair_hw(cfs_rq, task);
	}

	return retval;
}

/* cfs_rq->lock has to be locked for this */
static noinline void __sched __cu_up(struct task_struct *next)
{
	struct sched_entity *nextse;
	nextse = &(next->hwse);
	nextse->semaphore_up = 1;
	wake_up_process(next);
}

/* cfs_rq->lock has to be locked for this */
static noinline void __sched __migrate_task_hw(struct task_struct *next)
{
	struct sched_entity *nextse;
	nextse = &(next->hwse);
	nextse->need_migrate = 1;
	wake_up_process(next);
}

/*
 * Mechanism to release a cu by releasing the underlying semaphore-like lock
 */
static void cu_up(struct cfs_rq *cfs_rq)
{
	unsigned long flags;
	struct task_struct *next = NULL;

	/*
	 * Select next task to run 
	 * maxcount can get changed so we have to recheck it here...
	 */
	spin_lock_irqsave(&cfs_rq->lock, flags);
	if (cfs_rq->count > cfs_rq->maxcount)
		next = NULL;
	else {
		spin_unlock_irqrestore(&cfs_rq->lock, flags);
		/* Loop to get a valid one even if processes died while waiting for a cu */
		do {
			next = pick_next_task_fair_hw(cfs_rq);
		} while (next != NULL && task_is_dead(next));
		spin_lock_irqsave(&cfs_rq->lock, flags);
	}
	
	/* 
	 * Release the semaphore-like lock
	 * As stated above this _decreases_ the count
	 */

	/* pick_next_task_fair_hw returns NULL if the rbtree is empty
	 * If there are zero tasks waiting we can just decrease the count
	 */
	if (likely(next == NULL))
		cfs_rq->count--;
	else {
		__cu_up(next);
	}
	spin_unlock_irqrestore(&cfs_rq->lock, flags);
}




/*
 * Helper functions for the alloc, rerequest and free syscalls
 */


/*
 * Parse through the list of all computing unit lockholders and see if some of
 * them died. If so, release their lock.
 * Assumes that cfs_rq is locked
 */
static void __tidy_cu_locks(struct cfs_rq *cfs_rq)
{
	struct task_struct *p;
	unsigned long flags;
	unsigned int zombies = 0;
	
	struct sched_entity *hwse;
	struct list_head *next;
	/* a queue to gather dead tasks */
	struct list_head	del_node;
	INIT_LIST_HEAD(&del_node);

	spin_lock_irqsave(&cfs_rq->lock, flags);
	p = __locking_tasks_iterator_hw(cfs_rq, cfs_rq->tasks.next);
	
	while (p != NULL) {
		/* p still is a valid pointer since we used get_task_struct() */
		if (task_is_dead(p)){
			pin_task_on_cpu(p);
			/* delete it from this list - safe because the iterator is dequeue-safe */
			list_del_init(&p->hwse.group_node);
			
			/* mark it to be deleted */
			list_add(&p->hwse.group_node, &del_node);
			
			zombies++;
		}
		p = __locking_tasks_iterator_hw(cfs_rq, cfs_rq->balance_iterator);
	}
	spin_unlock_irqrestore(&cfs_rq->lock, flags);
	
	next = del_node.next;
	while (zombies) {
		if (next == &del_node)
			return;

		hwse = list_entry(next, struct sched_entity, group_node);
		p = task_of_hw(hwse);
		next = next->next;
		
		printk(KERN_INFO "Dead task %d held lock... Computing unit might be in a dirty state.", p->pid);
		/* release the task_struct so that the process may die */
		put_task_struct(p);
		/* up the cu lock, since a dead task occupied a slot */
		cu_up(cfs_rq);
		zombies--;
	}
}

/* 
 * calculates the affinity of a task (given its meta_info) towards a computing unit - assumes that computing_units is locked
 * Note: This is being called while holding a spinlock - do not sleep!
 *
 * @mi             meta_info from the application - must not be NULL!
 * @cui            pointer to the computing_unit_info struct to which the affinity shall be calculated
 */
static inline int __calculate_affinity(struct meta_info *mi, struct computing_unit_info *cui)
{
	int affinity = 0;
	unsigned int type, sum = 0;
	int constant_switch = CU_AFFINITY_WITH_PAREFFGAIN; /* will be optimized away */
	
	BUG_ON(mi == NULL);
	
	/* skip disabled units */
	if (!cui->online)
		return 0;
	
	/* Base affinity */
	/* TODO: Check bounds of affinity array */
	affinity = mi->type_affinity[cui->type];
	/* Never raise or assign a zero affinity because it means there is no implementation */
	if (!affinity)
		return 0;
	
	/* check if this is the only type of device that this task can run on */
	for_each_cu_type(type)
		sum += mi->type_affinity[type];
	/* if so, boost the affinity to ensure that the tasks makes it into the queue */
	if (sum == affinity)
		affinity += SINGLE_IMPLEMENTATION_BONUS - 5;
	
	/* Raise min affinity such that the maximum malus (-2) will not lower it to zero or beneath */
	affinity += 5;
	
	/* Penalize CPUs which are not the one we are currently executing on
	 * This is done to ensure that if we are scheduled on a cpu we will be
	 * stay on the one our ghost thread is currently on (and thus honoring the 
	 * original cfs scheduling decisions).
	 */
	if (is_cpu_cui(cui) && cui->api_device_number != task_cpu(current))
		affinity -= 1;

	switch (constant_switch) {
		case CU_AFFINITY_WITH_PAREFFGAIN:
			/* Raise and lower affinity based on parallel_efficiency_gain */
			switch (cui->type) {
				case CU_TYPE_CPU:  affinity -= (mi->parallel_efficiency_gain - 2); break;
				case CU_TYPE_CUDA: affinity += (mi->parallel_efficiency_gain - 2); break;
				case CU_TYPE_FPGA: affinity += (mi->parallel_efficiency_gain - 2); break;
			}
			break;
		
		default:
			BUG();
	}
	
	return affinity;
}


/* 
 * decision routine to match meta_info against a computing unit - assumes that computing_units is locked
 *
 * @mi             meta_info from the application - must not be NULL!
 * @best           pointer to a pointer to the computing_unit_info struct holding the best suited cu
 * @best_affinity  calculated level of affinity towards best
 * @cui            pointer to the computing_unit_info struct holding new contestant
 */
static inline void __assign_if_better(struct meta_info *mi, 
		struct computing_unit_info **best, int *best_affinity, 
		struct computing_unit_info *cui)
{
	int affinity = 0;
	BUG_ON(mi == NULL);
	
	/* Base affinity */
	affinity = __calculate_affinity(mi, cui);
	/* Never assign a zero affinity because it means there is no implementation
	 * or the device is offline...
	 */
	if (!affinity)
		return;
	
	/* Prefer a cu with highest affinity but search for one which can execute the task immediately */
	if (*best == NULL || 
#ifdef CU_HW_KEEP_QUEUE_FULL
		/* if the queues should be full just follow the affinity & queue congestion */
		((*best_affinity < affinity) && !is_queue_congested(cui)) ||
#endif
	  ((*best_affinity <= affinity) && (get_load_hw(cui)->weight <= get_load_hw(*best)->weight)) ||
	  (is_queue_congested(*best) && !is_queue_congested(cui))
		)
	{
		*best = cui;
		*best_affinity = affinity;
	}
}


/* Find the best runqueue with respect to meta_info - assumes that computing_units is locked */
static struct computing_unit_info * __find_best_cu(struct meta_info *mi, struct sched_entity *hwse, int *affinity_points)
{
	int type;
	struct computing_unit_info *best = NULL;
	struct computing_unit_info *cui = NULL;
	*affinity_points = 0;
	
	if (!mi) {
		struct list_head *iterator;
		/* Assume that there is no hardware implementation */
		for_each_cu_type(type)
			hwse->mi.type_affinity[type] = 0;
		hwse->mi.type_affinity[CU_TYPE_CPU] = 5;
		hwse->mi.parallel_efficiency_gain = 2; /* neutral gain */
		
		/* If no metadata is available, try to get any cu that is online */
		iterator = computing_units.list_of_cus[0].next;
		do{
			cui = __cui_iterator(&iterator);
			if (cui == NULL)
				break;
			else
				__assign_if_better(&hwse->mi, &best, affinity_points, cui);
		} while(1);
	
	} else {
		/* If metadata is available, try to get the most preferred and available cu */
		for_each_cu_type(type) {
			/* Skip types who currently have no units assigned. */
			if (list_empty(&computing_units.list_of_cus[type]))
				continue;
			
			/* Search through the units and record possible choices */
			list_for_each_entry(cui, &computing_units.list_of_cus[type], queue_node) {
				__assign_if_better(mi, &best, affinity_points, cui);
			}
		}
	}
	return best;
}


/* Returns the offering struct cui if the offer is valid and NULL otherwise
 * Assumes that computing_units is locked
 */
static struct computing_unit_info * __is_offer_valid(unsigned long id)
{
		struct computing_unit_info *cui = __computing_unit_get_by_id(id);
		if (cui != NULL && cui->offers_outstanding) {
			return cui;
		}
		return NULL;
}


/* 
 * Checks if an offer can be made to hwse to resume the calculation on this cu
 * Makes the offer if applicable. Assumes that computing_units and the 
 * runqueue of hwse is locked
 * Note: This is being called while holding a spinlock - do not sleep!
 *
 * Returns 1 if an offer has been made and 0 otherwise
 *
 * @hwse  the task to which the offer shall be made
 * @cui   pointer to the computing_unit_info struct which is offering
 */
static int __make_offer_to(struct sched_entity *hwse, struct computing_unit_info *cui)
{
	int affinity_points = __calculate_affinity(&hwse->mi, cui);
	/* only offer our service if we are an improvement */
	if (affinity_points > hwse->current_affinity) {
		if (affinity_points > hwse->offered_affinity || (__is_offer_valid(hwse->offerer) == NULL)) {
			hwse->offerer = cui->id;
			hwse->offered_affinity = affinity_points;
			return 1;
		}
	}
	return 0;
}


/* 
 * Tries to pull tasks from other cus to this one. Assumes that computing_units is locked
 *
 * @cui   pointer to the computing_unit_info struct of the invoking (aka idle) cu
 */
static void __load_balance_hw(struct computing_unit_info *cui)
{
	struct list_head *iterator;
	struct task_struct *candidate = NULL;
	struct computing_unit_info *othercui = NULL;
	struct computing_unit_info *candidatecui = NULL;
	unsigned long load_max = 0;
	int affinity_max = 1, free_slots = 0;
	
	/* if WE are a cpu we do not need to consider other cpus, as
	 * CPUs are balanced by the original CFS
	 */
	int isCPU = is_cpu_cui(cui);
	
	free_slots = computing_unit_free_slots(cui) + CU_HW_QUEUE_LIMIT;
	
	
	/* First pass: Try to find valid _waiting_ tasks from the busiest runqueue */
	iterator = computing_units.list_of_cus[0].next;
	//do{
		do{
			othercui = __cui_iterator(&iterator);
			if (othercui == NULL)
				break;
	#ifdef CU_HW_KEEP_QUEUE_FULL
			else if (isCPU)
				/* if we should leave the hw queues filled skip CPU balancing... */
				break;
	#endif
			else if (othercui == cui || (isCPU && is_cpu_cui(othercui)))
				/* skip us and other cpus if WE are a CPU */
				continue;
			else {
				struct rb_node *node;
				
				/* Iterate through all _waiting_ tasks on the other cus, try to find one we could execute */
				if (down_killable(&othercui->cfs_rq.access_mutex) == 0) {
					for (node = rb_first(&othercui->cfs_rq.tasks_timeline); node; node = rb_next(node)) {
						struct sched_entity *hwse = rb_entry(node, struct sched_entity, run_node);
						/* affinity is >0 if we may execute the task */
						int affinity_points = __calculate_affinity(&hwse->mi, cui);
						if (affinity_points > affinity_max || 
							 (affinity_points == affinity_max && get_load_hw(othercui)->weight > load_max)) {
							/* if we locked the rq of another candidate, release it */
							if (candidatecui && candidatecui != othercui)
								up(&candidatecui->cfs_rq.access_mutex);
							candidate = task_of_hw(hwse);
							candidatecui = othercui;
							affinity_max = affinity_points;
							load_max = get_load_hw(othercui)->weight;
						}
					}
					/* release access_mutex, but only if we are not planning to take our task from this rq */
					if (candidatecui != othercui)
						up(&othercui->cfs_rq.access_mutex);
				}
			}
		} while(1);
		
		/* If we found a task to pull here just proceed */
		if (candidate) {
			unsigned long flags;
			/*printk(HWACCEL_LOG_FACILITY "__load_balance_hw: Found a candidate (%p) and moving it (our count = %d).", candidate, cui->cfs_rq.count);*/
			dequeue_task_fair_hw(&candidatecui->cfs_rq, candidate);
			candidate->hwse.offerer = cui->id;
			candidate->hwse.offered_affinity = affinity_max;
			spin_lock_irqsave(&candidatecui->cfs_rq.lock, flags);
			__migrate_task_hw(candidate);
			spin_unlock_irqrestore(&candidatecui->cfs_rq.lock, flags);
			up(&candidatecui->cfs_rq.access_mutex);
			free_slots--;
		}// else
	//		break;
	//} while(free_slots);
	
	/* Second pass: Try to find valid _running_ tasks from the busiest runqueue */
	if (free_slots) {
		iterator = computing_units.list_of_cus[0].next;
		do{
			othercui = __cui_iterator(&iterator);
			if (othercui == NULL)
				break;
#ifdef CU_HW_KEEP_QUEUE_FULL
			else if (isCPU)
				/* if we should leave the hw queues filled skip CPU balancing... */
				break;
#endif
			else if (othercui == cui || (isCPU && is_cpu_cui(othercui)))
				continue;
			else {
				struct task_struct *p;
				unsigned long flags;
				unsigned int found_tasks = 0;
				
				/* Iterate through all _running_ tasks on the other cus */
				if (down_killable(&othercui->cfs_rq.access_mutex) == 0) {
					__tidy_cu_locks(&othercui->cfs_rq); /* do not fetch zombies */
					spin_lock_irqsave(&othercui->cfs_rq.lock, flags);
					p = __locking_tasks_iterator_hw(&othercui->cfs_rq, othercui->cfs_rq.tasks.next);
					while (p != NULL) {
						if (__make_offer_to(&p->hwse, cui))
							found_tasks++;
						p = __locking_tasks_iterator_hw(&othercui->cfs_rq, othercui->cfs_rq.balance_iterator);
					}
					spin_unlock_irqrestore(&othercui->cfs_rq.lock, flags);
					/* release access_mutex */
					up(&othercui->cfs_rq.access_mutex);
				}
				if (found_tasks) {
					if (is_cpu_cui(cui))
						cui->offers_outstanding += found_tasks;
					else
#ifdef CU_HW_KEEP_QUEUE_FULL
						cui->offers_outstanding = CU_HW_QUEUE_LIMIT - cui->cfs_rq.nr_running;
#else
						cui->offers_outstanding = 1;
#endif
				}
			}
		} while(1);
	}
}


/*
 * This locks two runqueues in ascending id direction, to avoid deadlocks
 * This locks alls given cfs_rqs which are not NULL and ignores duplicates
 */
static int lock_two_hw_runqueues(struct cfs_rq *one, struct cfs_rq *two)
{
	/* Nothing to do if both are null */
	if (one == two && one == NULL)
		return 0;

	if (one != NULL && (one == two || two == NULL))
		/* If both rqs are the same (and not NULL) only lock once */
		return down_killable(&one->access_mutex);
	
	else if (two != NULL && one == NULL)
		return down_killable(&two->access_mutex);
	
	else {
		/*
		 * Lock the queues always in ascending id direction, to avoid deadlocks
		 */
		struct computing_unit_info *cuione, *cuitwo;
		int retval;
		cuione = cui_of(one);
		cuitwo = cui_of(two);
		if (cuione->id < cuitwo->id) {
			retval = down_killable(&one->access_mutex);
			if (retval == 0) {
				retval = down_killable(&two->access_mutex);
				/* Special case: one suceeded, two did not -> release one */
				if (retval < 0)
					up(&one->access_mutex);
			}
		} else {
			retval = down_killable(&two->access_mutex);
			if (retval == 0) {
				retval = down_killable(&one->access_mutex);
				/* Special case: two suceeded, one did not -> release two */
				if (retval < 0)
					up(&two->access_mutex);
			}
		}
		return retval;
	}
}

/*
 * Calculates the task_granularity_nsec for the given task on the given cu
 * Assumes that cfs_rq is locked
 */
#ifdef APPLICATION_CONTROLLED_GRANULARITY
static inline void __set_task_granularity_nsec(struct sched_entity *hwse, struct cfs_rq *cfs_rq, struct meta_info *mi, int base_granularity)
#else
static inline void __set_task_granularity_nsec(struct sched_entity *hwse, struct cfs_rq *cfs_rq, struct meta_info *mi)
#endif
{
	struct computing_unit_info *cui = cui_of(cfs_rq);

	/*
	 * Initialize with the base value (in guessed seconds) for a context switch
	 * Conversion to seconds follows
	 */
#ifdef APPLICATION_CONTROLLED_GRANULARITY
	if (is_cpu_cui(cui))
		hwse->task_granularity_nsec = 0;
	else
		hwse->task_granularity_nsec = base_granularity;
#else
	hwse->task_granularity_nsec = type_granularities_sec[cui->type];
#endif

	if (!is_cpu_cui(cui) && mi != NULL) {
		/*
		 * A guess for the time in nanoseconds it would take to "preempt" the task, i.e.
		 * to copy its state to main memory and to copy it back if it is later resumed
		 */
		//hwse->task_granularity_nsec += 2 * (mi->memory_to_copy / cui->hp.bandwidth) * NSEC_PER_SEC;
		u64 tmp64 = mi->memory_to_copy * 2;
		do_div(tmp64, cui->hp.bandwidth);
		hwse->task_granularity_nsec += tmp64;
	}
	
	/* Convert to nanoseconds and prevent integer overflow */
	if (unlikely(hwse->task_granularity_nsec > MAX_NSEC_SECS))
		hwse->task_granularity_nsec = ULONG_MAX;
	else
		hwse->task_granularity_nsec *= NSEC_PER_SEC;
	
	//printk(HWACCEL_LOG_FACILITY "Calculated granularity of %lu", hwse->task_granularity_nsec);
}










/*
 * Syscalls to allocate, rerequest and free a computing unit from userspace
 */


/*
 * computing_unit_alloc:
 *
 * externally visible allocation routine for a supported and initialized
 * computing unit. Takes a meta information struct to assign a suitable
 * computing unit. Returns the API device number to a provided userland
 * pointer and on success a valid handle to a computing unit
 *
 * @mi:	 	meta_info struct filled by the application
 * @cu:		shortinfo struct to return infos about the cu back to userspace
 *
 * Returns
 *   0 after successfuly filling cu and copying it to userspace
 *   -EINVAL 
 *           if mi is not readable by the calling task
 *           if cu is not writeable by the calling task
 *   -EFAULT if one of the copies failed
 *   -ENODEV if the scheduler can find no suitable device for this task
 *           (eg only FPGA implementation and no FPGA present)
 *   -EINTR  if the task was interrupted by a signal while waiting for the lock
 *   -ETIME  if the task timed out while waiting for the lock
 */
#ifdef APPLICATION_CONTROLLED_GRANULARITY
SYSCALL_DEFINE3(computing_unit_alloc,
                struct meta_info *, mi, struct computing_unit_shortinfo *, cu,
								int, base_granularity)
#else
SYSCALL_DEFINE2(computing_unit_alloc,
                struct meta_info *, mi, struct computing_unit_shortinfo *, cu)
#endif
{
	int semaphore_state, affinity_points;
	struct computing_unit_shortinfo retcu;

  /* current task struct is in "current" */
	struct task_struct *task = current;
	struct sched_entity* hwse = &task->hwse;
	struct computing_unit_info *cui;
	struct cfs_rq *best_hw_rq = NULL;
	struct meta_info *provided_mi;
	
	/* fail if structs are not accessable */
	if (!access_ok(VERIFY_WRITE, cu, sizeof(struct computing_unit_shortinfo))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_alloc: Cannot write to cu\n");
		return -EINVAL;
	}
	if (mi != NULL && !access_ok(VERIFY_READ, mi, sizeof(struct meta_info))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_alloc: (mi != NULL) = %d\n", mi != NULL);
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_alloc: Cannot read from mi\n");
		return -EINVAL;
	}

	/* Retrieve the meta information from userspace */
	if (mi != NULL)
		copy_from_user_or_die(mi, hwse->mi);
	provided_mi = (mi != NULL ? &hwse->mi : NULL);
	
	/* We know at this point that this is a hardware task */
	hwse->is_hardware_task = 1;
	
	/* Find the best runqueue for this task... Lock computing_units for the search */
 find_best_unit:
	/* Prevent the task from switching cpus until allocated */
	//printk(HWACCEL_LOG_FACILITY "Pinning task %d in computing_unit_alloc", task->pid);
	pin_task_on_cpu(task);

	cui = NULL;
	down_mutex_or_fail(&computing_units.access_mutex);
	if (hwse->offerer != CU_INVALID_HANDLE) {
		/* Take offer - cui may be NULL afterwards if offerer died in the meantime */
		cui = __computing_unit_get_by_id(hwse->offerer);
		if (cui != NULL) {
			/* check if the offer is what it promises to be */
			affinity_points =  __calculate_affinity(provided_mi, cui);
			if (affinity_points == 0 || affinity_points < hwse->offered_affinity)
				cui = NULL;
		}
		hwse->offerer = CU_INVALID_HANDLE;
		hwse->offered_affinity = 0;
	}
	if (cui == NULL)
		cui = __find_best_cu(provided_mi, hwse, &affinity_points);
	up(&computing_units.access_mutex);
	/* Fail if no appropriate cu was found */
	if (cui == NULL) {
		set_cpu_affinity_hw(task, CU_TYPE_UNDEFINED);
		return -ENODEV;
	}
	hwse->current_affinity = affinity_points;
	best_hw_rq = &cui->cfs_rq;
	
	/* If this happened, the finding routine is messed up */
	WARN(unlikely(best_hw_rq->maxcount < 0), "sys_computing_unit_alloc: best_hw_rq seems to be no hardware cfs_rq!");

	/* Copy the prio information from the ghost thread */
	hwse->load.weight = task->se.load.weight;
	hwse->load.inv_weight = task->se.load.inv_weight;
	
	/* 
	 * lock access_mutex to get accurate min_vruntimes and to enable
	 * reentrant freeing. This is needed to make sure that alloc does not mess
	 * with the rb tree while free is working
	 */
	down_mutex_or_fail(&best_hw_rq->access_mutex);

	/* setup clock of hw rq, "attach" it to current cpu */
	/* only attach once per lifetime of hw cfs_rq - NOTE: not compatible with cpu hotplugging */
	if (unlikely(!rq_of_hw(best_hw_rq))) {
		best_hw_rq->rq = task_rq(task);
		best_hw_rq->min_vruntime = best_hw_rq->rq->clock;
		update_min_vruntime(best_hw_rq);
	}
	
	/* update stats of current task */
	if (likely(best_hw_rq->curr))
		entity_tick_hw(best_hw_rq, best_hw_rq->curr);
	
	/* Place the task near the beginning of the queue
	 * Honor saved unfairness information
	 */
	hwse->vruntime += best_hw_rq->min_vruntime + 1;
	//printk(HWACCEL_LOG_FACILITY "Task %d has vruntime %llu (new: %llu) for%s", current->pid, hwse->vruntime, best_hw_rq->min_vruntime, cu_type_to_const_char(cui->type));
	
	/* Save the rq */
	hwse->cfs_rq = best_hw_rq;
	
	/* tidy up lockholders - some may have died without freeing the cu */
	__tidy_cu_locks(best_hw_rq);

	/* acquire the underlying semaphore-like lock for this cu */
	semaphore_state = cu_down(best_hw_rq, hwse);
	/* relay the error condition to userspace */
	if (semaphore_state < 0) {
		up(&best_hw_rq->access_mutex);
		set_cpu_affinity_hw(task, CU_TYPE_UNDEFINED);
		return semaphore_state;
	}
	/* end of semaphore code */
	
	/* 
	 * Semaphore has been acquired
	 * Check if it is still valid, or if we have to migrate, or if our preferred
	 * cu is now offline and to be deleted
	 */
	if (hwse->need_migrate || !cui->online) {
		hwse->vruntime -= best_hw_rq->min_vruntime;
		hwse->cfs_rq = NULL;
		/* release the semaphore-like lock if it has been acquired */
		if (!hwse->need_migrate)
			cu_up(best_hw_rq);
		up(&best_hw_rq->access_mutex);
		/* Check if the device has to be deleted */
		if (!cui->online) {
      /* Try to delete the device */
			down_mutex_or_fail(&computing_units.access_mutex);
			__computing_unit_try_to_del(cui);
			up(&computing_units.access_mutex);
		} else {
			/*printk(HWACCEL_LOG_FACILITY "Got the signal to migrate %p", task);*/
    }
		
		/* Find another device... Better luck next time! */
		goto find_best_unit;
	}
	
	/*
	 * Mark this task as lock holder and bump the task_struct's
	 * usage count.
	 */
	{
		unsigned long flags;
		get_task_struct(task);
		spin_lock_irqsave(&best_hw_rq->lock, flags);
		list_add(&hwse->group_node, &best_hw_rq->tasks);
		best_hw_rq->curr = hwse;
		spin_unlock_irqrestore(&best_hw_rq->lock, flags);
	}
	
	/* Calculate the granularity with which this task will be rescheduled */
#ifdef APPLICATION_CONTROLLED_GRANULARITY
	__set_task_granularity_nsec(hwse, best_hw_rq, provided_mi, base_granularity);
#endif
	
	/* Copy the relevant information into the shortinfo struct */
	copy_cui_to_cu(cui, &retcu);
	
	/* release the runqueue of the cu */
	up(&best_hw_rq->access_mutex);
	
	/* Reallow the task on all (available) cpus */
	set_cpu_affinity_hw(task, cui->type);
	//printk(HWACCEL_LOG_FACILITY "Allowing task %d on cpu %2u (working on %s)", task->pid, task_cpu(task), cu_type_to_const_char(cui->type));
	
	/* copy the information about the cu into userspace */
	if (copy_to_user(cu, &retcu, sizeof(retcu)))
		goto release_lock_on_fault;
	
	/* Allocation of cu has been successful */
	return 0;
	
	/* If something goes wrong while the task already holds the cu lock, it has to be released */
 release_lock_on_fault:
	{
		unsigned long flags;
		//printk(HWACCEL_LOG_FACILITY "release_lock_on_fault task %d on cpu %2u", task->pid, task_cpu(task));
		while (down_killable(&best_hw_rq->access_mutex) < 0) {
			printk(HWACCEL_LOG_FACILITY "cu_alloc: release_lock_on_fault failed to acquire needed lock to tidy up, retrying");
		}
		spin_lock_irqsave(&best_hw_rq->lock, flags);
		/* Remove this task from the lock holder list and release its task_struct */
		list_del_init(&hwse->group_node);
		spin_unlock_irqrestore(&best_hw_rq->lock, flags);
		put_task_struct(task);
		/* release the semaphore-like lock */
		cu_up(best_hw_rq);
		/* release access_mutex */
		up(&best_hw_rq->access_mutex);
	}
	return -EFAULT;
}

/*
 * computing_unit_rerequest:
 *
 * externally visible call to probe unit availability for cooperative
 * multitasking.
 * Should only be called while holding a computing unit (allocated via
 * computing_unit_alloc).
 *
 * @id:	 	a handle to a cu which the calling process holds at the moment
 *
 * Returns
 *   0  if the unit may be occupied further and
 *   >0 if it should be freed by the caller.
 *   -EFAULT if the task is not associated with a hardware runqueue
 *   -ENODEV if no device with handle id exists
 *   -EPERM  if the caller tries to rerequest a different id than the previously allocated
 */
SYSCALL_DEFINE0(computing_unit_rerequest)
{
	struct task_struct *task = current;
	struct sched_entity* hwse = &task->hwse;
	struct sched_entity* next = NULL;
	struct cfs_rq *curr_hw_rq = cfs_rq_of_hw(hwse);
	struct computing_unit_info *cui = NULL;
	int may_occupy;
  
	if (unlikely(!is_hardware_cfs_rq(curr_hw_rq)))
		return -EFAULT;
	
	/* get the device */
	cui = cui_of(curr_hw_rq);
	
	//if (is_cpu_cui(cui))
	//	printk(HWACCEL_LOG_FACILITY "Task %d rerequests cpu %u", task->pid, cui->api_device_number);
	
	/* 
	 * lock access_mutex to enable reentrant freeing
	 * this is needed to make sure that rerequest does not mess
	 * with the rb tree while free is working
	 */
	down_mutex_or_fail(&curr_hw_rq->access_mutex);

	/* update stats of this task */
	entity_tick_hw(curr_hw_rq, hwse);

	/* tidy up lockholders - some may have died without freeing the cu */
	__tidy_cu_locks(curr_hw_rq);
	
	/* pick_next_entity_hw() returns NULL if the rbtree is empty
	 * 
	 * if no other threads are waiting (== NULL) the asking thread
	 * may occupy the resource further. Else (!= NULL) the task may
	 * accumulate at least an unfairness of its task_granularity_nsec as
	 * calculated by cu_alloc.
	 */
	next = pick_next_entity_hw(curr_hw_rq);
	may_occupy = (next == NULL);
	
	if (!may_occupy) {
		/* This means there IS a next node */
		s64 vdiff = hwse->vruntime - next->vruntime;
		/* Convert granularity to virtual time */
		u64 vgran = calc_delta_fair(hwse->task_granularity_nsec, hwse);
		
		may_occupy = ((vdiff < 0) || ((u64) vdiff <= vgran));
		
		/*if (!may_occupy)
			printk(HWACCEL_LOG_FACILITY "Granularity expired: %lld > %llu", (long long)vdiff, (unsigned long long)vgran);
		*/
	}
	
	/* release access_mutex */
	up(&curr_hw_rq->access_mutex);

	/* Check if we have an offer from another cu */
	if (hwse->offerer != CU_INVALID_HANDLE) {
		struct computing_unit_info *othercui = NULL;
		/* Check if the offering device still exists and is idle */
		down_mutex_or_fail(&computing_units.access_mutex);
		othercui = __is_offer_valid(hwse->offerer);
		/* POLICY: Take the offer if the offering cu's queue is not overly full */
		if (othercui != NULL && !is_queue_congested(othercui)) {
			//printk(HWACCEL_LOG_FACILITY "Task %d: Got an offer from %s (has %d offers outstanding)!", current->pid, cu_type_to_const_char(othercui->type), othercui->offers_outstanding);
			may_occupy = false;
			othercui->offers_outstanding -= 1;
		} else {
			/*
			if (othercui != NULL)
				printk(HWACCEL_LOG_FACILITY "Task %d: Rejecting offer from %s!", task->pid, cu_type_to_const_char(othercui->type));
			else
				printk(HWACCEL_LOG_FACILITY "Task %d: Rejecting offer from cu %d!", task->pid, hwse->offerer);
			*/
			hwse->offerer = CU_INVALID_HANDLE;
			hwse->offered_affinity = 0;
		}
		up(&computing_units.access_mutex);
	}
	
#ifdef FEAT_HWACCEL_DISABLE_CHECKPOINTING
	/* debug: enable eternal dibs */
	return 0;

#else

	/* If the unit is offline all rerequests fail
	 *
	 * If the unit works above its capacity due to a reduced maxcount, we also fail
	 *
	 * Note: The logic has to be reversed because the routine should return 0 (= false)
	 * on success (ie if the rerequest has been granted)
	 */
	return !(may_occupy && cui->online && cui->cfs_rq.count <= cui->cfs_rq.maxcount);
#endif /* FEAT_HWACCEL_DISABLE_CHECKPOINTING */
}

/*
 * computing_unit_free:
 *
 * externally visible freeing routine for a supported and initialized
 * computing unit which has been previously allocated via 
 * computing_unit_alloc. Checks if the caller indeed holds the unit
 * prior to freeing it. Returns zero on success
 * 
 * @id:	 	a handle to a cu which the calling process holds at the moment
 *
 * Returns
 *   0 on success
 *   -EFAULT if the task is not associated with a hardware runqueue
 *   -ENODEV if no device with handle id exists
 *   -EPERM  if the caller tries to free a different id than the previously allocated
 */
SYSCALL_DEFINE0(computing_unit_free)
{
	struct task_struct *curr = current;
	struct sched_entity *hwse = &curr->hwse;
	struct cfs_rq *curr_hw_rq = cfs_rq_of_hw(hwse);
	struct computing_unit_info *cui = NULL;
	
	WARN(curr_hw_rq == NULL, "computing_unit_free: current hardware runqueue is NULL");
	if (unlikely(!is_hardware_cfs_rq(curr_hw_rq)))
		return -EFAULT;
	
	/* Prevent the task from switching cus to ensure that the correct cu is being freed */
	//printk(HWACCEL_LOG_FACILITY "Pinning task %d in computing_unit_free", curr->pid);
	pin_task_on_cpu(curr);
	
	/* get the device */
	cui = cui_of(curr_hw_rq);
	
	/* 
	 * lock access_mutex to enable reentrant freeing
	 * this is needed to make sure that "next" and "curr" stay valid
	 */
	down_mutex_or_fail(&curr_hw_rq->access_mutex);
	
	
	/* just keep raw unfairness information */
	if (is_cpu_cui(cui)) {
		/* In the CPU case hwse has no data which makes sense */
		hwse->vruntime = curr->se.vruntime - (cfs_rq_of(&curr->se))->min_vruntime;
	} else {
		/* update stats of this task and kill its information from the rq */
		entity_tick_hw(curr_hw_rq, hwse);
		
		hwse->vruntime -= curr_hw_rq->min_vruntime;
	}
	hwse->cfs_rq = NULL;

	/* entity_tick_hw sets curr to hwse... reset that because the task is leaving */
	curr_hw_rq->curr = NULL;

	{
		unsigned long flags;
		spin_lock_irqsave(&curr_hw_rq->lock, flags);
		/* Remove this task from the lock holder list and release its task_struct */
		list_del_init(&hwse->group_node);
		spin_unlock_irqrestore(&curr_hw_rq->lock, flags);
		put_task_struct(curr);
	}
	
	/* release the semaphore-like lock */
	cu_up(curr_hw_rq);
	
	/* release access_mutex */
	up(&curr_hw_rq->access_mutex);
	
	/* Check if the device has to be deleted or balanced */
#ifdef CU_HW_KEEP_QUEUE_FULL
	//if (!is_queue_congested(cui))
	/* Prevent overscheduling... The currently freeing task might reapply for the queue directly */
	if (is_cpu_cui(cui) || cui->cfs_rq.nr_running < CU_HW_QUEUE_LIMIT - 1)
#else
	if (computing_unit_is_idle(cui))
#endif
	{
		if (down_killable(&computing_units.access_mutex) < 0) {
			printk(HWACCEL_LOG_FACILITY "cu_free: Failed to do load balancing due to unavailable lock");
			return 1;
		}
		if (computing_unit_is_idle(cui) && !cui->online)
			__computing_unit_try_to_del(cui);
		else
		/* Special case: Device is now idle */
			__load_balance_hw(cui);
		up(&computing_units.access_mutex);
	}
	
	return 0;
}



/*
 * Syscalls to add, remove, list and query a computing unit from userspace
 */


/*
 * computing_unit_add:
 *
 * Routine to add a computing unit to struct computing_units to enable its use
 * by the scheduling syscalls.
 *
 * @cu:		shortinfo struct to get infos about the cu from userspace and to
 *      	return its assigned handle back
 * @hp:		struct containing the hardware properties of the cu as reported by
 *      	the corresponding hardware API.
 *
 * Returns
 *   0 after successfuly filling cu and copying it to userspace
 *   -EINVAL if the provided pointers are invalid or if cu_us.type is invalid
 *   -EFAULT if the copy to or from userspace failed
 *   -ENOMEM if kzalloc failed to allocate a struct computing_unit_info
 
 */
SYSCALL_DEFINE2(computing_unit_add,
                struct computing_unit_shortinfo *, cu_us, struct hardware_properties *, hp_us)
{
	/* Some variables */
	struct computing_unit_shortinfo cu;
	struct computing_unit_info *cui = NULL;
	signed long error = 0;
	
	/* fail if the provided pointers are invalid */
	if (cu_us == NULL || !access_ok(VERIFY_READ, cu_us, sizeof(struct computing_unit_shortinfo))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_add: Cannot read from cu\n");
		return -EINVAL;
	}
	if (hp_us == NULL || !access_ok(VERIFY_READ, hp_us, sizeof(struct hardware_properties))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_add: Cannot read from hp\n");
		return -EINVAL;
	}

	/* retrieve and copy the information from userspace */
	if (copy_from_user(&cu, cu_us, sizeof(cu)))
		goto efault_and_fail;

	/* CPUs are a special case */
	if (cu.type == CU_TYPE_CPU) {
		/* Add a cpu: cpus are never deleted nor readded, just reenabled */
		/* search for device */
		down_mutex_or_fail(&computing_units.access_mutex);
		cui = __computing_unit_get(cu.type, cu.api_device_number);
		if (cui == NULL) {
			up(&computing_units.access_mutex);
			return -ENODEV;
		}
		
		__computing_unit_enable(cui);
		
		/* copy the information about the cu into userspace */
		copy_cui_to_cu(cui, &cu);
		if (copy_to_user(cu_us, &cu, sizeof(cu))) {
			up(&computing_units.access_mutex);
			goto efault_and_fail;
		}
		
		goto fill_new_device;
	}
	
	/* Allocate a new struct computing_unit_info */
	cui = __computing_unit_alloc_space();
	if (!cui)
		return -ENOMEM;

	/* init the computing_unit_info (cfs:rq, list, ...) */
	__computing_unit_init(cui);
	
	cui->type = cu.type;
	if (cui->type >= CU_NUMOF_TYPES) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_add: Invalid type given\n");
		error = -EINVAL;
		goto free_and_fail;
	}
	cui->api_device_number = cu.api_device_number;
	if (copy_from_user(&(cui->hp), hp_us, sizeof(cui->hp))) \
		goto efault_free_and_fail;
	cui->cfs_rq.maxcount = cui->hp.concurrent_kernels;
	
	/* Add to sruct computing_units and get assigned id */
	if (down_killable(&computing_units.access_mutex) < 0)
		goto efault_free_and_fail;
	__computing_unit_add(cui);
	cu.handle = cui->id;
	
	/* copy the information about the cu into userspace */
	if (copy_to_user(cu_us, &cu, sizeof(cu))) {
		error = -EFAULT;
		goto delete_and_fail;
	}
	
 fill_new_device:
	/* Get tasks for the new and idle device */
		__load_balance_hw(cui);
	up(&computing_units.access_mutex);
	return 0;
	
 efault_and_fail:
	error = -EFAULT;
	goto fail;

 efault_free_and_fail:
	error = -EFAULT;
	goto free_and_fail;

 delete_and_fail:
	__computing_unit_del(cui);
	up(&computing_units.access_mutex);
	
 free_and_fail:
	kfree(cui);
	
 fail:
	return error;
}

/*
 * computing_unit_del:
 *
 * Routine to remove a computing unit from struct computing_units to disable its
 * use by the scheduling syscalls.
 *
 * @id:	 	a handle to a cu
 *
 * Returns
 *   0 on success
 *   -ENODEV if no device with this id is found
 *   -EBUSY  if the device is currently in use and is therefore deleted with a delay
 */
SYSCALL_DEFINE1(computing_unit_del,
                 unsigned long, id)
{
	struct computing_unit_info *cui;
	down_mutex_or_fail(&computing_units.access_mutex);
	/* search for device */
	cui = __computing_unit_get_by_id(id);
	if (cui == NULL) {
		up(&computing_units.access_mutex);
		return -ENODEV;
	}
	
	/* mark as offline to prevent further occupation */
	cui->online = 0;
	
	/* try to tidy killed tasks from the cu */
	if (down_killable(&cui->cfs_rq.access_mutex) == 0) {
		__tidy_cu_locks(&cui->cfs_rq);
		up(&cui->cfs_rq.access_mutex);
	}
	
	/* Try to delete the unit directly */
	if (!__computing_unit_try_to_del(cui)) {
		up(&computing_units.access_mutex);
		/* Delete with a delay from syscalls alloc and free */
		return -EBUSY;
	}
	up(&computing_units.access_mutex);
	
	return 0;
}

/*
 * computing_unit_iterate:
 *
 * This implements an iterator over all computing units in struct computing_units.
 * A call with NULL as second argument (re-)initializes the iterator (to which
 * the first argument points) to the first handle of the first type currently
 * present in the struct. iterator is always preincremented to the next device,
 * to be safe against removal while iterating.
 * This function iterates over the devices sorted by type rather than id
 *
 * Example code:
	// Variables to iterate over all devices
	unsigned long iterator = CU_INVALID_HANDLE;
	unsigned long nr_devices = 0;
	unsigned long devices = 0;
	
	// Execute the kernel syscall to initialize iteration
	status = syscall(__NR_computing_unit_iterate, &iterator, NULL, &nr_devices);

 	// iterator is now set to the next valid id, or to CU_INVALID_HANDLE if there is none
	while (iterator != CU_INVALID_HANDLE) {
		struct computing_unit_shortinfo cu;
		// Execute the kernel syscall
		status = syscall(__NR_computing_unit_iterate, &iterator, &cu, NULL);
		// Check for errors
		if (status < 0)
			break;

		// This is needed to handle the case where the last device is removed during this iteration
		if (cu.handle == CU_INVALID_HANDLE)
			break;
		
		devices++;
		do_something_with(cu);
	}
	if (devices == nr_devices)
		// success
	else
		// failure
*
 * @iterator:	The iterator which is used by this syscall. Should not be modified
 *           	between the calls in userspace
 * @cu:     	shortinfo struct to write infos about the cu back to userspace
 * @nr_devices:	Will hold the number of devices known to the kernel upon return
 *             	Can be NULL if the number is of no interest
 *
 * Returns
 *   0 after successfuly filling cu and copying it to userspace
 *   -EINVAL if the provided pointers are invalid
 *   -ENODEV if the device identified by iterator is not found, which cannot happen if only
 *           devices returned by this iterator are deleted
 *           If two programs simultaneously iterate and delete devices this can and will happen
 *   -EFAULT if on of the copies failed
 */
SYSCALL_DEFINE3(computing_unit_iterate,
                unsigned long *, iterator, struct computing_unit_shortinfo *, cu, unsigned long *, nr_devices)
{
	struct computing_unit_info *cui = NULL;
	struct computing_unit_shortinfo retcu;
	struct list_head *it;
	unsigned long iter;
	
	/* fail if the provided pointers are invalid */
	if (iterator == NULL || !access_ok(VERIFY_WRITE, iterator, sizeof(unsigned long))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_iterate: Cannot write to iterator\n");
		return -EINVAL;
	}
	if (cu != NULL && !access_ok(VERIFY_WRITE, cu, sizeof(struct computing_unit_shortinfo))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_iterate: Cannot write to cu\n");
		return -EINVAL;
	}
	if (nr_devices != NULL && !access_ok(VERIFY_WRITE, nr_devices, sizeof(unsigned long))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_iterate: Cannot write to nr_devices\n");
		return -EINVAL;
	}

	copy_from_user_or_die(iterator, iter);
	
	if (nr_devices != NULL)
		if (copy_to_user(nr_devices, &computing_units.nr_devices, sizeof(computing_units.nr_devices)))
			return -EFAULT;
	
	/* First mode: Initialize iterator */
	if (cu == NULL) {
		down_mutex_or_fail(&computing_units.access_mutex);
		it = computing_units.list_of_cus[0].next;
		cui = __cui_iterator(&it);
		up(&computing_units.access_mutex);
		
		/* no devices present */
		if (cui == NULL)
			iter = CU_INVALID_HANDLE;
		else
			/* at least one device found... preiterate to it */
			iter = cui->id;
		
		goto copy_iterator;
	}
	
	/* Second mode: Iterate further */
	/* Lock to achieve a consistent pre-iteration */
	down_mutex_or_fail(&computing_units.access_mutex);
	
	/* Search for device which is identified by iterator */
	cui = __computing_unit_get_by_id(iter);
	if (cui == NULL) {
		up(&computing_units.access_mutex);
		/* The device we preiterated to was deleted... We do not know where we were */
		iter = CU_INVALID_HANDLE;
		retcu.handle = CU_INVALID_HANDLE;
		return -ENODEV;
	}
	
	/* Copy the data from the current device to the return struct */
	copy_cui_to_cu(cui, &retcu);

	/* Preiterate to the next device */
	it = cui->queue_node.next;
	cui = __cui_iterator(&it);
	up(&computing_units.access_mutex);
	
	if (cui == NULL)
		/* no devices present */
		iter = CU_INVALID_HANDLE;
	else
		/* at least one device found... preiterate to it */
		iter = cui->id;
	
	/* copy the new values to userspace */
	if (copy_to_user(cu, &retcu, sizeof(retcu)))
		return -EFAULT;
 copy_iterator:
	if (copy_to_user(iterator, &iter, sizeof(iter)))
		return -EFAULT;
	
	return 0;
}

/*
 * computing_unit_details:
 *
 * Routine to retrieve details about computing unit from userspace.
 *
 * @id:		A valid handle to a currently present computing unit
 * @cu:		shortinfo struct to hold the scheduler infos about the cu or NULL
 * @hp:		struct containing the hardware properties of the cu as reported by
 *      	the corresponding hardware API or NULL
 *
 * Returns
 *   0 after successfuly filling cu and copying it to userspace
 *   -EINVAL if the provided pointers are invalid
 *   -EFAULT if the copy failed
 *   -ENODEV if the device identified by id is not found
 */
SYSCALL_DEFINE3(computing_unit_details,
                unsigned long, id, struct computing_unit_shortinfo *, cu, struct hardware_properties *, hp)
{
	struct computing_unit_info *cui = NULL;
	struct computing_unit_shortinfo retcu;
	
	/* fail if the provided pointers are invalid */
	if (cu != NULL && !access_ok(VERIFY_WRITE, cu, sizeof(struct computing_unit_shortinfo))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_details: Cannot write to cu\n");
		return -EINVAL;
	}
	if (hp != NULL && !access_ok(VERIFY_WRITE, hp, sizeof(struct hardware_properties))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_details: Cannot write to hp\n");
		return -EINVAL;
	}

	/* Search for device which is identified by id */
	down_mutex_or_fail(&computing_units.access_mutex);
	cui = __computing_unit_get_by_id(id);
	up(&computing_units.access_mutex);
	if (cui == NULL)
		return -ENODEV;
	
	/* Copy the data about the device to the return struct */
	copy_cui_to_cu(cui, &retcu);

	/* copy the information to userspace if requested */
	if (hp != NULL)
		if (copy_to_user(hp, &cui->hp, sizeof(cui->hp)))
			return -EFAULT;
	if (cu != NULL)
		if (copy_to_user(cu, &retcu, sizeof(retcu)))
			return -EFAULT;
	
	return 0;
}

/*
 * computing_unit_set:
 *
 * Routine to reset the properties of a computing unit in struct computing_units
 *
 * @id:		A valid handle to a currently present computing unit
 * @cu:		shortinfo struct holding the new infos about the cu from userspace
 * @hp:		struct containing the hardware properties of the cu as reported by
 *      	the corresponding hardware API.
 *
 * Returns
 *   0 after successfuly copying the new properties from userspace
 *   -EINVAL if the provided pointers are invalid or if cu_us.type is invalid
 *   -EFAULT if the copy from userspace failed
 *   -ENODEV if the device identified by id is not found
 */
SYSCALL_DEFINE3(computing_unit_set,
                unsigned long, id, struct computing_unit_shortinfo *, cu_us, struct hardware_properties *, hp_us)
{
	/* Some variables */
	struct computing_unit_shortinfo cu;
	struct computing_unit_info *cui = NULL;
	int countdiff;
	
	/* fail if the provided pointers are invalid */
	if (cu_us == NULL || !access_ok(VERIFY_READ, cu_us, sizeof(struct computing_unit_shortinfo))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_add: Cannot read from cu\n");
		return -EINVAL;
	}
	if (hp_us == NULL || !access_ok(VERIFY_READ, hp_us, sizeof(struct hardware_properties))) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_add: Cannot read from hp\n");
		return -EINVAL;
	}

	/* Search for device which is identified by id */
	down_mutex_or_fail(&computing_units.access_mutex);
	cui = __computing_unit_get_by_id(id);
	up(&computing_units.access_mutex);
	if (cui == NULL)
		return -ENODEV;
	
	/* retrieve and copy the new information from userspace */
	copy_from_user_or_die(cu_us, cu);
	if (cu.type >= CU_NUMOF_TYPES) {
		printk(HWACCEL_LOG_FACILITY "sys_computing_unit_set: Invalid type given\n");
		return -EINVAL;
	}
	copy_from_user_or_die(hp_us, cui->hp);
	
	/* copy over the info, but cpu api_device_numbers are fixed */
	cui->type = cu.type;
	if (!is_cpu_cui(cui))
		cui->api_device_number = cu.api_device_number;
	
	countdiff = cui->hp.concurrent_kernels - cui->cfs_rq.maxcount;
	/*printk(HWACCEL_LOG_FACILITY "sys_computing_unit_add: countdiff = %d\n", countdiff);*/
	if (countdiff != 0) {
		unsigned long flags;
		struct task_struct *next;
		
		/* 
		 * lock the spinlock because we tamper with the maxcount
		 */
		spin_lock_irqsave(&cui->cfs_rq.lock, flags);

		cui->cfs_rq.maxcount = cui->hp.concurrent_kernels;
		/*
		 * If countdiff is > 0 then the cu can hold more tasks now and we can
		 * wake some of the sleepers to take those places
		 */
		 for (;countdiff > 0; countdiff--) {
			/*
			 * Loop to get a valid task even if processes died while waiting for a cu
			 */
			do {
				next = pick_next_task_fair_hw(&cui->cfs_rq);
			} while (next != NULL && task_is_dead(next));
			
			/* pick_next_task_fair_hw returns NULL if the rbtree is empty
			 * If there are zero tasks waiting we can just decrease the count
			 * maxcount can get changed so we have to recheck it here...
			 */
			if (likely(next == NULL || cui->cfs_rq.count > cui->cfs_rq.maxcount))
				/* No more tasks are waiting for the cu */
				break;
			else {
				/* Increase the count to reflect the newly woken task */
				cui->cfs_rq.count++;
				__cu_up(next);
			}
		}
		spin_unlock_irqrestore(&cui->cfs_rq.lock, flags);
	}
	
	return 0;
}

#undef HWACCEL_LOG_FACILITY
#endif /* CONFIG_SCHED_HWACCEL */
