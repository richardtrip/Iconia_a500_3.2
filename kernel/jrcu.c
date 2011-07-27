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
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/stddef.h>
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


#define RCU_HZ 20 /* max rate at which batches are retired */

struct rcu_data {
 u8 wait; /* goes false when this cpu consents to
 * the retirement of the current batch */
 u8 which; /* selects the current callback list */
 struct rcu_list cblist[2]; /* current & previous callback lists */
} ____cacheline_aligned_in_smp;

static struct rcu_data rcu_data[NR_CPUS];

/* debug & statistics stuff */
static struct rcu_stats {
 unsigned nbatches; /* #end-of-batches (eobs) seen */
 atomic_t nbarriers; /* #rcu barriers processed */
 u64 ninvoked; /* #invoked (ie, finished) callbacks */
 atomic_t nleft; /* #callbacks left (ie, not yet invoked) */
 unsigned nforced; /* #forced eobs (should be zero) */
} rcu_stats;

int rcu_scheduler_active __read_mostly;
int rcu_nmi_seen __read_mostly;
static u64 rcu_timestamp;

/*
 * Return our CPU id or zero if we are too early in the boot process to
 * know what that is. For RCU to work correctly, a cpu named '0' must
 * eventually be present (but need not ever be online).
 */
static inline int rcu_cpu(void)
{
 return current_thread_info()->cpu;
}

/*
 * Invoke whenever the calling CPU consents to end-of-batch. All CPUs
 * must so consent before the batch is truly ended.
 */
static inline void rcu_eob(int cpu)
{
 struct rcu_data *rd = &rcu_data[cpu];
 if (unlikely(rd->wait)) {
 rd->wait = 0;
#ifdef CONFIG_RCU_PARANOID
 /* not needed, we can tolerate some fuzziness on exactly
 * when other CPUs see the above write insn. */
 smp_wmb();
#endif
 }
}

void rcu_note_context_switch(int cpu)
{
 rcu_eob(cpu);
}

void rcu_barrier(void)
{
 struct rcu_synchronize rcu;

 if (!rcu_scheduler_active)
 return;

 init_completion(&rcu.completion);
 call_rcu(&rcu.head, wakeme_after_rcu);
 wait_for_completion(&rcu.completion);
 atomic_inc(&rcu_stats.nbarriers);

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
 which = ACCESS_ONCE(rd->which) & 1;
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
 * For a given cpu, push the previous batch of callbacks onto a (global)
 * pending list, then make the current batch the previous. A new, empty
 * current batch exists after this operation.
 *
 * Locklessly tolerates changes being made by call_rcu() to the current
 * batch, locklessly tolerates the current batch becoming the previous
 * batch, and locklessly tolerates a new, empty current batch becoming
 * available. Requires that the previous batch be quiescent by the time
 * rcu_end_batch is invoked.
 */
static void rcu_end_batch(struct rcu_data *rd, struct rcu_list *pending)
{
 int prev;
 struct rcu_list *plist; /* some cpus' previous list */

 prev = (ACCESS_ONCE(rd->which) & 1) ^ 1;
 plist = &rd->cblist[prev];

 /* Chain previous batch of callbacks, if any, to the pending list */
 if (plist->head) {
 rcu_list_join(pending, plist);
 rcu_list_init(plist);
 smp_wmb();
 }
 /*
 * Swap current and previous lists. Other cpus must not see this
 * out-of-order w.r.t. the just-completed plist init, hence the above
 * smp_wmb().
 */
 rd->which++;
}

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
 int cpu, eob;
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
 */
 for_each_present_cpu(cpu) {
 rd = &rcu_data[cpu];
 rcu_end_batch(rd, pending);
 if (cpu_online(cpu)) /* wins race with offlining every time */
 rd->wait = preempt_count_cpu(cpu) > idle_cpu(cpu);
 else
 rd->wait = 0;
 }
 rcu_stats.nbatches++;
}

static void rcu_delimit_batches(void)
{
 unsigned long flags;
 struct rcu_list pending;

 rcu_list_init(&pending);

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

#define RCU_PERIOD_NS (NSEC_PER_SEC / RCU_HZ)
#define RCU_PERIOD_DELTA_NS (((NSEC_PER_SEC / HZ) * 3) / 2)

#define RCU_PERIOD_MIN_NS RCU_PERIOD_NS
#define RCU_PERIOD_MAX_NS (RCU_PERIOD_NS + RCU_PERIOD_DELTA_NS)

static struct hrtimer rcu_timer;

static void rcu_softirq_func(struct softirq_action *h)
{
 rcu_delimit_batches();
}

static enum hrtimer_restart rcu_timer_func(struct hrtimer *t)
{
 ktime_t next;

 raise_softirq(RCU_SOFTIRQ);

 next = ktime_add_ns(ktime_get(), RCU_PERIOD_NS);
 hrtimer_set_expires_range_ns(&rcu_timer, next, RCU_PERIOD_DELTA_NS);
 return HRTIMER_RESTART;
}

static void rcu_timer_restart(void)
{
 pr_info("JRCU: starting timer. rate is %d Hz\n", RCU_HZ);
 hrtimer_forward_now(&rcu_timer, ns_to_ktime(RCU_PERIOD_NS));
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

#define RCU_PERIOD_MIN_US (RCU_PERIOD_MIN_NS / NSEC_PER_USEC)
#define RCU_PERIOD_MAX_US (RCU_PERIOD_MAX_NS / NSEC_PER_USEC)

/*
 * Once the system is fully up, we will drive the periodic-polling part
 * of JRCU from a kernel daemon, jrcud. Until then it is driven by
 * an interrupt.
 */
#include <linux/err.h>
#include <linux/param.h>
#include <linux/kthread.h>

static int jrcud_func(void *arg)
{
 set_user_nice(current, -19);
 current->flags |= PF_NOFREEZE;

 pr_info("JRCU: daemon started. Will operate at ~%d Hz.\n", RCU_HZ);
 rcu_timer_stop();

 while (!kthread_should_stop()) {
 usleep_range(RCU_PERIOD_MIN_US, RCU_PERIOD_MAX_US);
 rcu_delimit_batches();
 }

 pr_info("JRCU: daemon exiting\n");
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
 int cpu, q, s[2], msecs;

 raw_local_irq_disable();
 msecs = div_s64(sched_clock() - rcu_timestamp, NSEC_PER_MSEC);
 raw_local_irq_enable();

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

 s[1] = s[0] = 0;
 for_each_online_cpu(cpu) {
 struct rcu_data *rd = &rcu_data[cpu];
 int w = ACCESS_ONCE(rd->which) & 1;
 seq_printf(m, "%c%c%c%d ",
 '-',
 idle_cpu(cpu) ? 'I' : '-',
 rd->wait ? 'W' : '-',
 w);
 s[w]++;
 }
 seq_printf(m, " FLAGS\n");

 for (q = 0; q < 2; q++) {
 for_each_online_cpu(cpu) {
 struct rcu_data *rd = &rcu_data[cpu];
 struct rcu_list *l = &rd->cblist[q];
 seq_printf(m, "%4d ", l->count);
 }
 seq_printf(m, " Q%d%c\n", q, " *"[s[q] > s[q^1]]);
 }
 seq_printf(m, "\nFLAGS:\n");
 seq_printf(m, " I - cpu idle, 0|1 - Q0 or Q1 is current Q, other is previous Q,\n");
 seq_printf(m, " W - cpu does not permit current batch to end (waiting),\n");
 seq_printf(m, " * - marks the Q that is current for most CPUs.\n");

 return 0;
}

static int rcu_debugfs_open(struct inode *inode, struct file *file)
{
 return single_open(file, rcu_debugfs_show, NULL);
}

static const struct file_operations rcu_debugfs_fops = {
 .owner = THIS_MODULE,
 .open = rcu_debugfs_open,
 .read = seq_read,
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

 retval = debugfs_create_file("rcudata", 0444, rcudir,
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
