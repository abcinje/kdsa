#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>

#include "driver.h"

#define NR_CHAN     (8)
#define NR_THREAD   (32)
#define BLK_SIZE    (512)
#define NR_SUBMIT   (262144)
#define NR_DESC     (512)

#define A100_BAR1   (0x203000000000)

#if (NR_THREAD != 8) && (NR_THREAD != 16) && (NR_THREAD != 32)
#error Invalid number of threads
#endif

#if (NR_DESC % 64 != 0)
#error Invalid number of descriptors
#endif

struct test_ctx {
	struct dsa_hw_desc desc[NR_DESC];
	struct dsa_completion_record *comp[NR_DESC];
	dma_addr_t comp_dma[NR_DESC];

	void *src, *dst;
	dma_addr_t src_dma, dst_dma, gpu_dma;
	struct dma_chan *chan;

	uint8_t padding[16];
} __attribute__((aligned(64)));
static_assert(sizeof(struct test_ctx) % 64 == 0);

static struct task_struct *threads[NR_THREAD];
static struct test_ctx ctxs[NR_THREAD];

static ktime_t begin_ktime[NR_THREAD];
static ktime_t end_ktime[NR_THREAD];

static wait_queue_head_t barrier_waitqueue;
static atomic_t barrier_cnt = ATOMIC_INIT(0);

static struct dma_chan *chan[NR_CHAN];

static struct kmem_cache *comp_cache;

static int test_init(int tid)
{
	struct test_ctx *ctx;
	int i;
	int error;

	ctx = &ctxs[tid];

	// Channel
	ctx->chan = chan[tid / (NR_THREAD / NR_CHAN)];

	// Buffer
	ctx->src = kmalloc(BLK_SIZE, GFP_KERNEL);
	ctx->dst = kmalloc(BLK_SIZE, GFP_KERNEL);
	if (!ctx->src || !ctx->dst)
		goto failure0;

	// IOVA
	ctx->src_dma = dma_map_single(ctx->chan->device->dev, ctx->src, BLK_SIZE, DMA_TO_DEVICE);
	ctx->dst_dma = dma_map_single(ctx->chan->device->dev, ctx->dst, BLK_SIZE, DMA_FROM_DEVICE);
	ctx->gpu_dma = dma_map_resource(ctx->chan->device->dev, A100_BAR1 + tid * BLK_SIZE, BLK_SIZE, DMA_BIDIRECTIONAL, 0);

	// Completion
	error = 0;
	for (i = 0; i < NR_DESC; i++) {
		ctx->comp[i] = kmem_cache_alloc(comp_cache, GFP_KERNEL);
		if (!ctx->comp[i])
			error = 1;
	}
	if (error)
		goto failure1;
	for (i = 0; i < NR_DESC; i++)
		ctx->comp_dma[i] = dma_map_single(ctx->chan->device->dev, ctx->comp[i], sizeof(struct dsa_completion_record), DMA_BIDIRECTIONAL);

	return 0;

failure1:
	for (i = 0; i < NR_DESC; i++)
		if (ctx->comp[i])
			kmem_cache_free(comp_cache, ctx->comp[i]);

failure0:
	if (ctx->src)
		kfree(ctx->src);
	if (ctx->dst)
		kfree(ctx->dst);

	return 1;
}

static void test_barrier(void)
{
	if (atomic_inc_return(&barrier_cnt) == NR_THREAD)
		wake_up_all(&barrier_waitqueue);
	else
		wait_event(barrier_waitqueue, atomic_read(&barrier_cnt) == NR_THREAD);
}

static void test_run(int tid)
{
	struct test_ctx *ctx;
	int i;
	int remained, targetted, submitted, failed;
	int rc;

	ctx = &ctxs[tid];

	remained = NR_SUBMIT;
	while (remained) {
		targetted = min(remained, NR_DESC);

		submitted = 0;
		failed = 0;

		for (i = 0; i < targetted; i++) {
			memset(ctx->comp[i], 0, sizeof(struct dsa_completion_record));

#if 0
			// CPU -> CPU
			prep(&ctx->desc[i], DSA_OPCODE_MEMMOVE, ctx->src_dma, ctx->dst_dma, BLK_SIZE, ctx->comp_dma[i], IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV);
#else
			// CPU -> GPU
			prep(&ctx->desc[i], DSA_OPCODE_MEMMOVE, ctx->src_dma, ctx->gpu_dma, BLK_SIZE, ctx->comp_dma[i], IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV);
#endif

			rc = submit(ctx->chan, &ctx->desc[i]);
			if (rc) {
				if (unlikely(rc != -11))
					printk("kdsa: fatal: failed to submit desc (rc %d)\n", rc);
				break;
			}
			submitted++;
		}

		/*
		if (submitted == NR_DESC)
			printk("kdsa: increase the number of descriptors\n");
		*/

		for (i = 0; i < submitted; i++) {
			rc = poll(ctx->comp[i]);
			if (rc != DSA_COMP_SUCCESS) {
				printk("kdsa: fatal: failed to poll (rc %d)\n", rc);
				failed++;
			}
		}

		remained -= (submitted - failed);
	}
}

static void test_exit(int tid)
{
	struct test_ctx *ctx;
	int i;

	ctx = &ctxs[tid];

	// Completion
	for (i = 0; i < NR_DESC; i++) {
		dma_unmap_single(ctx->chan->device->dev, ctx->comp_dma[i], sizeof(struct dsa_completion_record), DMA_BIDIRECTIONAL);
		kmem_cache_free(comp_cache, ctx->comp[i]);
	}

	// IOVA
	dma_unmap_single(ctx->chan->device->dev, ctx->src_dma, BLK_SIZE, DMA_TO_DEVICE);
	dma_unmap_single(ctx->chan->device->dev, ctx->dst_dma, BLK_SIZE, DMA_FROM_DEVICE);
	dma_unmap_resource(ctx->chan->device->dev, ctx->gpu_dma, BLK_SIZE, DMA_BIDIRECTIONAL, 0);

	// Buffer
	kfree(ctx->src);
	kfree(ctx->dst);
}

static int test(void *data)
{
	int tid;
	int rc;

	tid = (int)(long)data;
	rc = 0;

	if (test_init(tid)) {
		rc = 1;
		goto failure;
	}

	test_barrier();

	begin_ktime[tid] = ktime_get();
	test_run(tid);
	end_ktime[tid] = ktime_get();

	test_exit(tid);

failure:
	while (!kthread_should_stop())
		cond_resched();

	return rc;
}

static bool filter(struct dma_chan *chan, void *param)
{
	const char *wanted = param;
	const char *name = dma_chan_name(chan);
	return strcmp(name, wanted) == 0;
}

static struct dma_chan *request_channel(const char *chan_name)
{
	dma_cap_mask_t mask;
	struct dma_chan *chan;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	chan = dma_request_channel(mask, filter, (void *)chan_name);
	if (!chan) {
		printk("kdsa: failed to request channel %s\n", chan_name);
		return NULL;
	}

	return chan;
}

static long long int find_min_max(long long int *arr, int len, int is_max)
{
	int i;
	long long int tmp = arr[0];

	for (i = 1; i < len; i++) {
		if (is_max)
			tmp = max(tmp, arr[i]);
		else
			tmp = min(tmp, arr[i]);
	}

	return tmp;
}

static int __init kdsa_init(void)
{
	char chan_name[16];
	int cid;
	int tid;
	int rc;
	long long int begin[NR_THREAD];
	long long int end[NR_THREAD];
	long long int b, e;

	// Channel
	for (cid = 0; cid < NR_CHAN; cid++) {
		snprintf(chan_name, 16, "dma0chan%d", cid);
		chan[cid] = request_channel(chan_name);
	}

	// Barrier
	init_waitqueue_head(&barrier_waitqueue);

	// Completion cache
	comp_cache = kmem_cache_create("kdsa_comp", sizeof(struct dsa_completion_record), 0, SLAB_HWCACHE_ALIGN, NULL);

	// Create threads
	for (tid = 0; tid < NR_THREAD; tid++) {
		threads[tid] = kthread_create(test, (void *)(long)tid, "kdsa_thread%d", tid);
		if (IS_ERR(threads[tid])) {
			printk("kdsa: failed to create thread %d\n", tid);
			threads[tid] = NULL;
			continue;
		}

		kthread_bind(threads[tid], tid < 16 ? tid : tid + 16);
		wake_up_process(threads[tid]);
	}

	// Stop threads
	rc = 0;
	for (tid = 0; tid < NR_THREAD; tid++) {
		if (IS_ERR_OR_NULL(threads[tid]))
			rc = -EINVAL;
		else if (kthread_stop(threads[tid]))
			rc = -ENOMEM;
	}

	// Result
	if (!rc) {
		for (tid = 0; tid < NR_THREAD; tid++)
			begin[tid] = ktime_to_ns(begin_ktime[tid]);
		for (tid = 0; tid < NR_THREAD; tid++)
			end[tid] = ktime_to_ns(end_ktime[tid]);

		b = find_min_max(begin, NR_THREAD, 0);
		e = find_min_max(end, NR_THREAD, 1);
		printk("kdsa: elapsed_ns: %lld\n", e - b);

		printk("kdsa: io: %ld\n", (long)NR_THREAD * NR_SUBMIT);
	} else {
		printk("kdsa: failed to test\n");
	}

	// Completion cache
	kmem_cache_destroy(comp_cache);

	// Channel
	for (cid = 0; cid < NR_CHAN; cid++)
		if (chan[cid])
			dma_release_channel(chan[cid]);

	// rc == 0 means success; the return code is intentional to avoid rmmod
	return rc ? rc : -EPERM;
}
module_init(kdsa_init);

MODULE_LICENSE("GPL");
