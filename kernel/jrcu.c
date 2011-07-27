/*
 * Joe's tiny single-cpu RCU, for small SMP systems.
 *
 * Running RCU end-of-batch operations from a single cpu relieves the
 * other CPUs from this periodic responsibility. This will eventually
 * be important for those realtime applications requiring full use of
 * dedicated cpus. JRCU is also a lockless implementation, currently,
 * although some anticipated features will eventually require a per
 * cpu rcu_lock along some minimal-contention paths.
 *
 * Author: Joe Korty <joe.korty@ccur.com>
 *
 * Acknowledgements: Paul E. McKenney's 'TinyRCU for uniprocessors' inspired
 * the thought that there could could be something similiarly simple for SMP.
 * The rcu_list chain operators are from Jim Houston's Alternative RCU.
 *
 * Copyright Concurrent Computer Corporation, 2011
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
 * This RCU maintains three callback lists: the current batch (per cpu),
 * the previous batch (also per cpu), and the pending list (global).
 */

#include <linux/bug.h>
#include <linux/smp.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/preempt.h>
#include <linux/compiler.h>
#include <linux/irqflags.h>
#include <linux/rcupdate.h>

#include <asm/system.h>

/*
 * Define an rcu list type and operators. An rcu list has only ->next
 * pointers for the chain nodes; the list head however is special and
 * has pointers to both the first and last nodes of the chain. Tweaked
 * so that null head, tail pointers can be used to signify an empty list.
 */
struct rcu_list {
 struct rcu_head *head;
 struct rcu_head **tail;
 int count; /* stats-n-debug */
};

static inline void rcu_list_init(struct rcu_list *l)
{
 l->head = NULL;
 l->tail = NULL;
 l->count = 0;
}

/*
 * Add an element to the tail of an rcu list
 */
static inline void rcu_list_add(struct rcu_list *l, struct rcu_head *h)
{
 if (unlikely(l->tail == NULL))
 l->tail = &l->head;
 *l->tail = h;
 l->tail = &h->next;
 l->count++;
 h->next = NULL;
}

/*
 * Append the contents of one rcu list to another. The 'from' list is left
 * corrupted on exit; the caller must re-initialize it before it can be used
 * again.
 */
static inline void rcu_list_join(struct rcu_list *to, struct rcu_list *from)
{
 if (from->head) {
 if (unlikely(to->tail == NULL)) {
 to->tail = &to->head;
 to->count = 0;
 }
 *to->tail = from->head;
 to->tail = from->tail;
 to->count += from->count;
 }
}
/*
 * selects, in ->cblist[] below, which is the current callback list and which
 * is the previous.
 */
static u8 rcu_which ____cacheline_aligned_in_smp;

struct rcu_data {
 u8 wait; /* goes false when this cpu consents to
 * the retirement of the current batch */
 struct rcu_list cblist[2]; /* current & previous callback lists */
} ____cacheline_aligned_in_smp;

static struct rcu_data rcu_data[NR_CPUS];

/* debug & statistics stuff */
static struct rcu_stats {
 unsigned npasses;	/* #passes made */
 unsigned nbatches; /* #end-of-batches (eobs) seen */
 atomic_t nbarriers; /* #rcu barriers processed */
 u64 ninvoked; /* #invoked (ie, finished) callbacks */
 atomic_t nleft; /* #callbacks left (ie, not yet invoked) */
 unsigned nforced; /* #forced eobs (should be zero) */
} rcu_stats;
#define RCU_HZ			(20)
#define RCU_HZ_PERIOD_US	(USEC_PER_SEC / RCU_HZ)
#define RCU_HZ_DELTA_US		(USEC_PER_SEC / (HZ * 3 / 2))

int rcu_hz = RCU_HZ;
int rcu_hz_period_us = RCU_HZ_PERIOD_US;
int rcu_hz_delta_us = RCU_HZ_DELTA_US;

int rcu_scheduler_active __read_mostly;
int rcu_nmi_seen __read_mostly;
static u64 rcu_timestamp;

/*
 * Return our CPU id or zero if we are too early in the boot process to
 * know what that is. For RCU to work correctly, a cpu named '0' must
 * eventually be present (but need not ever be online).
 */
#ifdef HAVE_THREAD_INFO_CPU
static inline int rcu_cpu(void)
{
 return current_thread_info()->cpu;
}
#else

static unsigned rcu_cpu_early_flag __read_mostly = 1;

static inline int rcu_cpu(void)
{
	if (unlikely(rcu_cpu_early_flag)) {
		if (!(rcu_scheduler_active && nr_cpu_ids > 1))
			return 0;
		rcu_cpu_early_flag = 0;
	}
	return raw_smp_processor_id();
}
#endif /* HAVE_THREAD_INFO_CPU */

/*
 * Invoke whenever the calling CPU consents to end-of-batch. All CPUs
 * must so consent before the batch is truly ended.
 */
static inline void rcu_eob(int cpu)
{
 struct rcu_data *rd = &rcu_data[cpu];
 if (unlikely(rd->wait)) {
 rd->wait = 0;
#ifndef CONFIG_JRCU_LAZY
 	smp_wmb();
#endif
 }
}
void rcu_read_unlock_jrcu(void)
{
	if (preempt_count() == 1)
		rcu_eob(rcu_cpu());
	preempt_enable();
}
EXPORT_SYMBOL_GPL(rcu_read_unlock_jrcu);

void rcu_note_context_switch(int cpu)
{
 rcu_eob(cpu);
}
void rcu_note_might_resched(void)
{
	unsigned long flags;

	raw_local_irq_save(flags);
	rcu_eob(rcu_cpu());
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL(rcu_note_might_resched);

void synchronize_sched(void)
{
 struct rcu_synchronize rcu;

 if (!rcu_scheduler_active)
 return;

 init_completion(&rcu.completion);
 call_rcu(&rcu.head, wakeme_after_rcu);
 wait_for_completion(&rcu.completion);
 atomic_inc(&rcu_stats.nbarriers);

}
EXPORT_SYMBOL_GPL(synchronize_sched);

void rcu_barrier(void)
{
	synchronize_sched();
	synchronize_sched();
}
EXPORT_SYMBOL_GPL(rcu_barrier);

void rcu_force_quiescent_state(void)
{
}
EXPORT_SYMBOL_GPL(rcu_force_quiescent_state);


/*
 * Insert an RCU callback onto the calling CPUs list of 'current batch'
 * callbacks. Lockless version, can be invoked anywhere except under NMI.
 */
void call_rcu(struct rcu_head *cb, void (*func)(struct rcu_head *rcu))
{
 unsigned long flags;
 struct rcu_data *rd;
 struct rcu_list *cblist;
 int which;

 cb->func = func;
 cb->next = NULL;

 raw_local_irq_save(flags);
 smp_rmb();

 rd = &rcu_data[rcu_cpu()];
 which = ACCESS_ONCE(rcu_which);
 cblist = &rd->cblist[which];

 /* The following is not NMI-safe, therefore call_rcu()
 * cannot be invoked under NMI. */
 rcu_list_add(cblist, cb);
 smp_wmb();
 raw_local_irq_restore(flags);
 atomic_inc(&rcu_stats.nleft);
}
EXPORT_SYMBOL_GPL(call_rcu);

/*
 * Invoke all callbacks on the passed-in list.
 */
static void rcu_invoke_callbacks(struct rcu_list *pending)
{
 struct rcu_head *curr, *next;

 for (curr = pending->head; curr;) {
 next = curr->next;
 curr->func(curr);
 curr = next;
 rcu_stats.ninvoked++;
 atomic_dec(&rcu_stats.nleft);
 }
}

/*
 * Check if the conditions for ending the current batch are true. If
 * so then end it.
 *
 * Must be invoked periodically, and the periodic invocations must be
 * far enough apart in time for the previous batch to become quiescent.
 * This is a few tens of microseconds unless NMIs are involved; an NMI
 * stretches out the requirement by the duration of the NMI.
 *
 * "Quiescent" means the owning cpu is no longer appending callbacks
 * and has completed execution of a trailing write-memory-barrier insn.
 */
static void __rcu_delimit_batches(struct rcu_list *pending)
{
 struct rcu_data *rd;
 struct rcu_list *plist;
 int cpu, eob, prev;
 u64 rcu_now;

 /* If an NMI occured then the previous batch may not yet be
 * quiescent. Let's wait till it is.
 */
 if (rcu_nmi_seen) {
 rcu_nmi_seen = 0;
 return;
 }

 if (!rcu_scheduler_active)
 return;

 /*
 * Find out if the current batch has ended
 * (end-of-batch).
 */
 eob = 1;
 for_each_online_cpu(cpu) {
 rd = &rcu_data[cpu];
 if (rd->wait) {
 eob = 0;
 break;
 }
 }

 /*
 * Force end-of-batch if too much time (n seconds) has
 * gone by. The forcing method is slightly questionable,
 * hence the WARN_ON.
 */
 rcu_now = sched_clock();
 if (!eob && !rcu_timestamp
 && ((rcu_now - rcu_timestamp) > 3LL * NSEC_PER_SEC)) {
 rcu_stats.nforced++;
 WARN_ON_ONCE(1);
 eob = 1;
 }

 /*
 * Just return if the current batch has not yet
 * ended. Also, keep track of just how long it
 * has been since we've actually seen end-of-batch.
 */

 if (!eob)
 return;

 rcu_timestamp = rcu_now;

 /*
 * End the current RCU batch and start a new one.
 *
 * This is a two-step operation: move every cpu's previous list
 * to the global pending list, then tell every cpu to swap its
 * current and pending lists (ie, toggle rcu_which).
 *
 * We tolerate the cpus taking a bit of time noticing this swap;
 * we expect them to continue to put callbacks on the old current
 * list (which is now the previous list) for a while.  That time,
 * however, cannot exceed one RCU_HZ period.
 */
prev = ACCESS_ONCE(rcu_which) ^ 1;

 for_each_present_cpu(cpu) {
 rd = &rcu_data[cpu];
 plist = &rd->cblist[prev];
 /* Chain previous batch of callbacks, if any, to the pending list */
 if (plist->head) {
 	rcu_list_join(pending, plist);
	rcu_list_init(plist);
 }
 if (cpu_online(cpu)) /* wins race with offlining every time */
 rd->wait = preempt_count_cpu(cpu) > idle_cpu(cpu);
 else
 rd->wait = 0;
 }
 smp_wmb(); /* just paranoia, the below xchg should do this on all archs */

 /*
  * Swap current and previous lists.  The other cpus must not
  * see this out-of-order w.r.t. the above emptying of each cpu's
  * previous list.  The xchg accomplishes that and, as a side (but
  * seemingly unneeded) bonus, keeps this cpu from advancing its insn
  * counter until the results of that xchg are visible on other cpus.
  */
 xchg(&rcu_which, prev); /* only place where rcu_which is written to */

 rcu_stats.nbatches++;
}

static void rcu_delimit_batches(void)
{
 unsigned long flags;
 struct rcu_list pending;

 rcu_list_init(&pending);
 rcu_stats.npasses++;

 raw_local_irq_save(flags);
 smp_rmb();
 __rcu_delimit_batches(&pending);
 smp_wmb();
 raw_local_irq_restore(flags);

 if (pending.head)
 rcu_invoke_callbacks(&pending);
}

/* ------------------ interrupt driver section ------------------ */

/*
 * We drive RCU from a periodic interrupt during most of boot. Once boot
 * is complete we (optionally) transition to a daemon.
 */

#include <linux/time.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>

#define rcu_hz_period_ns	(rcu_hz_period_us * NSEC_PER_USEC)
#define rcu_hz_delta_ns		(rcu_hz_delta_us * NSEC_PER_USEC)

static struct hrtimer rcu_timer;

static void rcu_softirq_func(struct softirq_action *h)
{
 rcu_delimit_batches();
}

static enum hrtimer_restart rcu_timer_func(struct hrtimer *t)
{
 ktime_t next;

 raise_softirq(RCU_SOFTIRQ);

 next = ktime_add_ns(ktime_get(), rcu_hz_period_ns);
 hrtimer_set_expires_range_ns(&rcu_timer, next, rcu_hz_delta_ns);
 return HRTIMER_RESTART;
}

static void rcu_timer_restart(void)
{
 pr_info("JRCU: starting timer. rate is %d Hz\n", RCU_HZ);
 hrtimer_forward_now(&rcu_timer, ns_to_ktime(rcu_hz_period_ns));
 hrtimer_start_expires(&rcu_timer, HRTIMER_MODE_ABS);
}

static __init int rcu_timer_start(void)
{
 open_softirq(RCU_SOFTIRQ, rcu_softirq_func);

 hrtimer_init(&rcu_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
 rcu_timer.function = rcu_timer_func;
 rcu_timer_restart();

 return 0;
}

#ifdef CONFIG_JRCU_DAEMON
static void rcu_timer_stop(void)
{
 int stat;

 stat = hrtimer_cancel(&rcu_timer);
 if (stat)
 pr_info("JRCU: timer canceled.\n");
}
#endif

/*
 * Transition from a simple to a full featured, interrupt driven RCU.
 *
 * This is to protect us against RCU being used very very early in the boot
 * process, where ideas like 'tasks' and 'cpus' and 'timers' and such are
 * not yet fully formed. During this very early time, we use a simple,
 * not-fully-functional braindead version of RCU.
 *
 * Invoked from main() at the earliest point where scheduling and timers
 * are functional.
 */
void __init rcu_scheduler_starting(void)
{
 int stat;

 stat = rcu_timer_start();
 if (stat) {
 pr_err("JRCU: failed to start. This is fatal.\n");
 return;
 }

 rcu_scheduler_active = 1;
 smp_wmb();

 pr_info("JRCU: started\n");
}

#ifdef CONFIG_JRCU_DAEMON

/* ------------------ daemon driver section --------------------- */

/*
 * Once the system is fully up, we will drive the periodic-polling part
 * of JRCU from a kernel daemon, jrcud. Until then it is driven by
 * an interrupt.
 */
#include <linux/err.h>
#include <linux/param.h>
#include <linux/kthread.h>
static int rcu_priority;
static struct task_struct *rcu_daemon;

static int jrcu_set_priority(int priority)
{
	struct sched_param param;

	if (priority == 0) {
		set_user_nice(current, -19);
		return 0;
	}

	if (priority < 0)
		param.sched_priority = MAX_USER_RT_PRIO + priority;
	else
		param.sched_priority = priority;

	sched_setscheduler_nocheck(current, SCHED_RR, &param);
	return param.sched_priority;
}

static int jrcud_func(void *arg)
{
 current->flags |= PF_NOFREEZE;

 rcu_priority = jrcu_set_priority(CONFIG_JRCU_DAEMON_PRIO);
 rcu_timer_stop();
 pr_info("JRCU: daemon started. Will operate at ~%d Hz.\n", rcu_hz);

 while (!kthread_should_stop()) {
 usleep_range(rcu_hz_period_us, rcu_hz_period_us + rcu_hz_delta_us);
 rcu_delimit_batches();
 }

 pr_info("JRCU: daemon exiting\n");
 rcu_daemon = NULL;
 rcu_timer_restart();
 return 0;
}

static __init int jrcud_start(void)
{
 struct task_struct *p;

 p = kthread_run(jrcud_func, NULL, "jrcud");
 if (IS_ERR(p)) {
 pr_warn("JRCU: daemon not started\n");
 return -ENODEV;
 }
 rcu_daemon = p;
 return 0;
}
late_initcall(jrcud_start);

#endif /* CONFIG_JRCU_DAEMON */

/* ------------------ debug and statistics section -------------- */

#ifdef CONFIG_RCU_TRACE

#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int rcu_debugfs_show(struct seq_file *m, void *unused)
{
 int cpu, q, msecs;

 raw_local_irq_disable();
 msecs = div_s64(sched_clock() - rcu_timestamp, NSEC_PER_MSEC);
 raw_local_irq_enable();
 seq_printf(m, "%14u: hz\n", rcu_hz);
#ifdef CONFIG_JRCU_DAEMON
	if (rcu_daemon)
		seq_printf(m, "%14u: daemon priority\n", rcu_priority);
	else
		seq_printf(m, "%14s: daemon priority\n", "none, no daemon");
#endif
 seq_printf(m, "%14u: #passes seen\n",
	rcu_stats.npasses);

 seq_printf(m, "%14u: #batches seen\n",
 rcu_stats.nbatches);
 seq_printf(m, "%14u: #barriers seen\n",
 atomic_read(&rcu_stats.nbarriers));
 seq_printf(m, "%14llu: #callbacks invoked\n",
 rcu_stats.ninvoked);
 seq_printf(m, "%14u: #callbacks left to invoke\n",
 atomic_read(&rcu_stats.nleft));
 seq_printf(m, "%14u: #msecs since last end-of-batch\n",
 msecs);
 seq_printf(m, "%14u: #passes forced (0 is best)\n",
 rcu_stats.nforced);
 seq_printf(m, "\n");

 for_each_online_cpu(cpu)
 seq_printf(m, "%4d ", cpu);
 seq_printf(m, " CPU\n");

 for_each_online_cpu(cpu) {
 struct rcu_data *rd = &rcu_data[cpu];
 seq_printf(m, "--%c%c ",
 rd->wait ? 'W' : '-');
 }
 seq_printf(m, " FLAGS\n");

 for (q = 0; q < 2; q++) {
 int w = ACCESS_ONCE(rcu_which);
 for_each_online_cpu(cpu) {
 struct rcu_data *rd = &rcu_data[cpu];
 struct rcu_list *l = &rd->cblist[q];
 seq_printf(m, "%4d ", l->count);
 }
 seq_printf(m, "  Q%d%c\n", q, " *"[q == w]);
 }
 seq_printf(m, "\nFLAGS:\n");
 seq_printf(m, "  I - cpu idle, W - cpu waiting for end-of-batch,\n");
 seq_printf(m, "  * - the current Q, other is the previous Q.\n");

 return 0;
}
static ssize_t rcu_debugfs_write(struct file *file,
 const char __user *buffer, size_t count, loff_t *ppos)
{
 int i, j, c;
 char token[32];

 if (!capable(CAP_SYS_ADMIN))
 return -EPERM;

 if (count <= 0)
 return count;

if (!access_ok(VERIFY_READ, buffer, count))
 return -EFAULT;

i = 0;
 if (__get_user(c, &buffer[i++]))
 return -EFAULT;

 /* Token extractor -- first, skip leading whitepace */
 while (c && isspace(c) && i < count) {
 if (__get_user(c, &buffer[i++]))
 return -EFAULT;
 }

 if (i >= count || c == 0)
 return count; /* all done, no more tokens */

 j = 0;
do {
 if (j == (sizeof(token) - 1))
 return -EINVAL;
 token[j++] = c;
if (__get_user(c, &buffer[i++]))
 return -EFAULT;
 } while (c && !isspace(c) && i < count); /* extract next token */
 token[j++] = 0;

 if (!strncmp(token, "hz=", 3)) {
 int rcu_hz_wanted = -1;
 sscanf(&token[3], "%d", &rcu_hz_wanted);
 if (rcu_hz_wanted < 2 || rcu_hz_wanted > 1000)
 return -EINVAL;
 rcu_hz = rcu_hz_wanted;
 rcu_hz_period_us = USEC_PER_SEC / rcu_hz;
 } else
 return -EINVAL;

 return count;
}

static int rcu_debugfs_open(struct inode *inode, struct file *file)
{
 return single_open(file, rcu_debugfs_show, NULL);
}

static const struct file_operations rcu_debugfs_fops = {
 .owner = THIS_MODULE,
 .open = rcu_debugfs_open,
 .read = seq_read,
 .write = rcu_debugfs_write,
 .llseek = seq_lseek,
 .release = single_release,
};

static struct dentry *rcudir;

static int __init rcu_debugfs_init(void)
{
 struct dentry *retval;

 rcudir = debugfs_create_dir("rcu", NULL);
 if (!rcudir)
 goto error;

 retval = debugfs_create_file("rcudata", 0644, rcudir,
		NULL, &rcu_debugfs_fops);
 if (!retval)
 goto error;

 pr_info("JRCU: Created debugfs files\n");
 return 0;

error:
 debugfs_remove_recursive(rcudir);
 pr_warn("JRCU: Could not create debugfs files\n");
 return -ENOSYS;
}
late_initcall(rcu_debugfs_init);
#endif /* CONFIG_RCU_TRACE */
