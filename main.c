#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/wait.h>

#include "common.h"

#define NB_CPUS (16)

struct task_struct *threads[NB_CPUS];

wait_queue_head_t barrier_wq;
atomic_t barrier_cnt = ATOMIC_INIT(0);

static void test_barrier(void)
{
	if (atomic_inc_return(&barrier_cnt) == NB_CPUS)
		wake_up_all(&barrier_wq);
	else
		wait_event(barrier_wq, atomic_read(&barrier_cnt) == NB_CPUS);
}

static int test_fn(void *data)
{
	test_barrier();

	printk("%ld\n", (long)data);

	while (!kthread_should_stop())
		cond_resched();

	return 0;
}

static int __init kdsa_init(void)
{
	int i;

	init_waitqueue_head(&barrier_wq);

	for (i = 0; i < NB_CPUS; i++) {
		threads[i] = kthread_run(test_fn, (void *)(long)i, "kdsa_thread%d", i);
		if (IS_ERR(threads[i])) {
			printk("kdsa: failed to create thread %d\n", i);
			threads[i] = NULL;
		}
	}

	printk("kdsa: initialized\n");
	return 0;
}

static void __exit kdsa_exit(void)
{
	int i;
	for (i = 0; i < NB_CPUS; i++) {
		if (!IS_ERR_OR_NULL(threads[i]))
			kthread_stop(threads[i]);
	}

	printk("kdsa: exited\n");
}

module_init(kdsa_init);
module_exit(kdsa_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Injae Kang");
