/*
 * Completely Fair Scheduling (CFS) Class (SCHED_NORMAL/SCHED_BATCH)
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra <pzijlstr@redhat.com>
 */

/**************************************************************
 * CFS operations on hardware schedulable entities:
 */

/* cpu runqueue to which this cfs_rq is attached */
static inline struct rq *rq_of_hw(struct cfs_rq *cfs_rq)
{
	return cfs_rq->rq;
}

/* runqueue on which this entity is (to be) queued */
static inline struct cfs_rq *cfs_rq_of_hw(struct sched_entity *se)
{
	return se->cfs_rq;
}

static inline struct task_struct *task_of_hw(struct sched_entity *se)
{
	return container_of(se, struct task_struct, hwse);
}

static inline struct cfs_rq *task_cfs_hw_rq(struct task_struct *p)
{
	return p->hwse.cfs_rq;
}

/**************************************************************
 * Scheduling class tree data structure manipulation methods:
 */

/* see sched_fair.c */

/**************************************************************
 * Scheduling class statistics methods:
 */

/*
 * Update the current hwardware task's runtime statistics. Skip current tasks that
 * are not in our scheduling class.
 */
static void update_hw(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	/* The ->rq pointer is being set by alloc */
	u64 now = rq_of_hw(cfs_rq)->clock;
	unsigned long delta_exec;

	if (unlikely(!curr))
		return;

	/*
	 * Idly try to set curr to the given se for the following commands
	 * If curr is being reset during the following command chain that is
	 * ignored up to now
	 */
	cfs_rq->curr = curr;
	
	/*
	 * Get the amount of time the current task was running
	 * since the last time we changed load (this cannot
	 * overflow on 32 bits):
	 */
	delta_exec = (unsigned long)(now - curr->exec_start);
	if (!delta_exec)
		return;

	__update_curr(cfs_rq, curr, delta_exec);
	curr->exec_start = now;
}

static inline void
update_stats_wait_start_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->wait_start, rq_of_hw(cfs_rq)->clock);
}

/*
 * Task is being enqueued - update stats:
 */
static void update_stats_enqueue_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Are we enqueueing a waiting task? (for current tasks
	 * a dequeue/enqueue event is a NOP)
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_start_hw(cfs_rq, se);
}

static void
update_stats_wait_end_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	schedstat_set(se->wait_max, max(se->wait_max,
			rq_of_hw(cfs_rq)->clock - se->wait_start));
	schedstat_set(se->wait_count, se->wait_count + 1);
	schedstat_set(se->wait_sum, se->wait_sum +
			rq_of_hw(cfs_rq)->clock - se->wait_start);
#ifdef CONFIG_SCHEDSTATS
	if (entity_is_task(se)) {
		trace_sched_stat_wait(task_of_hw(se),
			rq_of_hw(cfs_rq)->clock - se->wait_start);
	}
#endif
	schedstat_set(se->wait_start, 0);
}

static inline void
update_stats_dequeue_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Mark the end of the wait period if dequeueing a
	 * waiting task:
	 */
	if (se != cfs_rq->curr)
		update_stats_wait_end_hw(cfs_rq, se);
}

/*
 * We are picking a new current task - update its stats:
 */
static inline void
update_stats_curr_start_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * We are starting a new run period:
	 */
	se->exec_start = rq_of_hw(cfs_rq)->clock;
}

/**************************************************
 * Scheduling class queueing methods:
 */

static void
account_entity_enqueue_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_add(&cfs_rq->load, se->load.weight);
	add_cfs_task_weight(cfs_rq, se->load.weight);
	cfs_rq->nr_running++;
	se->on_rq = 1;
}

static void
account_entity_dequeue_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	update_load_sub(&cfs_rq->load, se->load.weight);
	add_cfs_task_weight(cfs_rq, -se->load.weight);
	cfs_rq->nr_running--;
	se->on_rq = 0;
}

static void
enqueue_entity_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_hw(cfs_rq, cfs_rq->curr);
	account_entity_enqueue_hw(cfs_rq, se);

	update_stats_enqueue_hw(cfs_rq, se);
	check_spread(cfs_rq, se);
	if (se != cfs_rq->curr)
		__enqueue_entity(cfs_rq, se);
}

static void
dequeue_entity_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_hw(cfs_rq, cfs_rq->curr);

	update_stats_dequeue_hw(cfs_rq, se);

	if (se != cfs_rq->curr)
		__dequeue_entity(cfs_rq, se);
	account_entity_dequeue_hw(cfs_rq, se);
	update_min_vruntime(cfs_rq);
}

static void
set_next_entity_hw(struct cfs_rq *cfs_rq, struct sched_entity *se)
{
	/* 'current' is not kept within the tree. */
	if (se->on_rq) {
		cfs_rq->curr = NULL;
		/*
		 * We have to do a full dequeue here since hardware tasks
		 * do not get automatically re-enqueued into the tree. They
		 * have to enqueue themselves by calling cu_alloc again.
		 */
		dequeue_entity_hw(cfs_rq, se);
	}

	update_stats_curr_start_hw(cfs_rq, se);
	cfs_rq->curr = se;
#ifdef CONFIG_SCHEDSTATS
	/*
	 * Track our maximum slice length, if the CPU's load is at
	 * least twice that of our own weight (i.e. dont track it
	 * when there are only lesser-weight tasks around):
	 */
	if (rq_of_hw(cfs_rq)->load.weight >= 2*se->load.weight) {
		se->slice_max = max(se->slice_max,
			se->sum_exec_runtime - se->prev_sum_exec_runtime);
	}
#endif
	se->prev_sum_exec_runtime = se->sum_exec_runtime;
}

/*
static int
wakeup_preempt_entity_hw(struct sched_entity *curr, struct sched_entity *se);
*/

static struct sched_entity *pick_next_entity_hw(struct cfs_rq *cfs_rq)
{
	struct sched_entity *se = __pick_next_entity(cfs_rq);
	/*struct sched_entity *left = se;

	
	if (cfs_rq->next && wakeup_preempt_entity_hw(cfs_rq->next, left) < 1)
		se = cfs_rq->next;
	*/

	return se;
}

static void
entity_tick_hw(struct cfs_rq *cfs_rq, struct sched_entity *curr)
{
	if (unlikely(!curr))
		return;
	/*
	 * Update run-time statistics of the 'current'.
	 */
	update_hw(cfs_rq, curr);
}

/**************************************************
 * CFS operations on tasks:
 */

/*
 * The enqueue_task method is called before nr_running is
 * increased. Here we update the fair scheduling stats and
 * then put the task into the rbtree:
 */
static void enqueue_task_fair_hw(struct cfs_rq *cfs_rq, struct task_struct *p)
{
	struct sched_entity *se = &p->hwse;

	if (se->on_rq)
		return;
	enqueue_entity_hw(cfs_rq, se);
}

/*
 * The dequeue_task method is called before nr_running is
 * decreased. We remove the task from the rbtree and
 * update the fair scheduling stats:
 */
static void dequeue_task_fair_hw(struct cfs_rq *cfs_rq, struct task_struct *p)
{
	struct sched_entity *se = &p->hwse;

	dequeue_entity_hw(cfs_rq, se);
}

/*
 * Should 'se' preempt 'curr'.
 *
 *             |s1
 *        |s2
 *   |s3
 *         g
 *      |<--->|c
 *
 *  w(c, s1) = -1
 *  w(c, s2) =  0
 *  w(c, s3) =  1
 *
 */

// static int
//wakeup_preempt_entity_hw(struct sched_entity *curr, struct sched_entity *se)
//{
//	/* s64 gran, vdiff = curr->vruntime - se->vruntime; */
//	s64 vdiff = curr->vruntime - se->vruntime;
//
//	if (vdiff <= 0)
//		return -1;
//
//	/* TODO: Implement penalty for parameter copying */
//	/*
//	gran = wakeup_gran(curr, se);
//	if (vdiff > gran)
//		return 1;
//	*/
//
//	return 0;
//}

static struct task_struct *pick_next_task_fair_hw(struct cfs_rq *cfs_rq)
{
	struct task_struct *p = NULL;
	struct sched_entity *se = NULL;

	if (unlikely(!cfs_rq->nr_running))
		return NULL;

	se = pick_next_entity_hw(cfs_rq);
	if(se == NULL) {
		WARN(se == NULL, "rbtree was empty when it should not be");
	} else {
		set_next_entity_hw(cfs_rq, se);

		p = task_of_hw(se);
	}

	return p;
}

/*
 * Lock-holding task iterator. Note: the current task might be
 * dequeued so the iterator has to be dequeue-safe. Here we
 * achieve that by always pre-iterating before returning
 * the current task:
 */
static struct task_struct *
__locking_tasks_iterator_hw(struct cfs_rq *cfs_rq, struct list_head *next)
{
	struct task_struct *p = NULL;
	struct sched_entity *hwse;

	if (next == &cfs_rq->tasks)
		return NULL;

	hwse = list_entry(next, struct sched_entity, group_node);
	p = task_of_hw(hwse);
	cfs_rq->balance_iterator = next->next;

	return p;
}

