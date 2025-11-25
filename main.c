#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>

#include "driver.h"

#define NR_NUMA     (2)
#define NR_CHAN     (8)
#define NR_THREAD   (32)
#define BLK_SIZE    (512)
#define NR_DESC     (512)

#define A100_BAR1   (0x203000000000)

#if (NR_THREAD != 8) && (NR_THREAD != 16) && (NR_THREAD != 32)
#error Invalid number of threads
#endif

#if (NR_DESC % 64 != 0)
#error Invalid number of descriptors
#endif

#define BATCH

struct test_ctx {
	struct dsa_hw_desc desc[NR_DESC];
	struct dsa_completion_record *comp[NR_DESC];
	dma_addr_t comp_dma[NR_DESC];

	struct dsa_hw_desc batch_desc;
	struct dsa_completion_record *batch_comp;
	dma_addr_t desc_list_dma, batch_comp_dma;

	void *src, *dst;
	dma_addr_t src_dma, dst_dma, gpu_dma;
	struct dma_chan *chan;
	uint32_t pasid;

	uint64_t io_cnt;
} __attribute__((aligned(64)));
static_assert(sizeof(struct test_ctx) % 64 == 0);

static struct task_struct *threads[NR_THREAD];
static struct test_ctx ctxs[NR_THREAD];

static ktime_t begin_ktime[NR_THREAD];
static ktime_t end_ktime[NR_THREAD];

static wait_queue_head_t barrier_waitqueue;
static atomic_t barrier_cnt = ATOMIC_INIT(0);

static struct dma_chan *dma_chan[NR_NUMA][NR_CHAN];

static struct kmem_cache *comp_cache;

static int test_init(int tid)
{
	struct test_ctx *ctx;
	int i;
	int error;

	ctx = &ctxs[tid];

	ctx->io_cnt = 0;

	// Channel
	ctx->chan = dma_chan[0][tid / (NR_THREAD / NR_CHAN)];
	ctx->pasid = 1;

	// Buffer
	ctx->src = kmalloc(BLK_SIZE, GFP_KERNEL);
	ctx->dst = kmalloc(BLK_SIZE, GFP_KERNEL);
	if (!ctx->src || !ctx->dst)
		goto failure0;

	// IOVA
	ctx->src_dma = dma_map_single(ctx->chan->device->dev, ctx->src, BLK_SIZE, DMA_BIDIRECTIONAL);
	ctx->dst_dma = dma_map_single(ctx->chan->device->dev, ctx->dst, BLK_SIZE, DMA_BIDIRECTIONAL);
	ctx->gpu_dma = dma_map_resource(ctx->chan->device->dev, A100_BAR1 + tid * BLK_SIZE, BLK_SIZE, DMA_BIDIRECTIONAL, 0);

	// IOVA for Batch
	ctx->desc_list_dma = dma_map_single(ctx->chan->device->dev, ctx->desc, NR_DESC * sizeof(struct dsa_hw_desc), DMA_BIDIRECTIONAL);

	// Completion
	error = 0;
	for (i = 0; i < NR_DESC; i++) {
		ctx->comp[i] = kmem_cache_zalloc(comp_cache, GFP_KERNEL);
		if (!ctx->comp[i])
			error = 1;
	}
	ctx->batch_comp = kmem_cache_zalloc(comp_cache, GFP_KERNEL);
	if (!ctx->batch_comp)
		error = 1;
	if (error)
		goto failure1;

	for (i = 0; i < NR_DESC; i++)
		ctx->comp_dma[i] = dma_map_single(ctx->chan->device->dev, ctx->comp[i], sizeof(struct dsa_completion_record), DMA_BIDIRECTIONAL);
	ctx->batch_comp_dma = dma_map_single(ctx->chan->device->dev, ctx->batch_comp, sizeof(struct dsa_completion_record), DMA_BIDIRECTIONAL);

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
	int targetted, submitted;
	int rc;

	ctx = &ctxs[tid];

	while (!kthread_should_stop()) {
#ifndef BATCH
		targetted = NR_DESC;

		submitted = 0;

		for (i = 0; i < targetted; i++) {
#if 0
			// CPU -> CPU
			prep(&ctx->desc[i], DSA_OPCODE_MEMMOVE, ctx->src_dma, ctx->dst_dma, BLK_SIZE, ctx->comp_dma[i], IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV);
#else
			// CPU -> GPU
			prep(&ctx->desc[i], ctx->pasid, DSA_OPCODE_MEMMOVE, ctx->src_dma, ctx->gpu_dma, BLK_SIZE, ctx->comp_dma[i], IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV);
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
			if (unlikely(rc != DSA_COMP_SUCCESS))
				printk("kdsa: fatal: failed to poll (rc %d)\n", rc);
			else
				ctx->io_cnt++;
			ctx->comp[i]->status = 0;
		}
#else
		for (i = 0; i < NR_DESC; i++)
			prep(&ctx->desc[i], ctx->pasid, DSA_OPCODE_MEMMOVE, ctx->src_dma, ctx->gpu_dma, BLK_SIZE, ctx->comp_dma[i], IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV);
		prep(&ctx->batch_desc, ctx->pasid, DSA_OPCODE_BATCH, ctx->desc_list_dma, 0, NR_DESC, ctx->batch_comp_dma, IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CRAV);

		rc = submit(ctx->chan, &ctx->batch_desc);
		if (rc) {
			if (unlikely(rc != -11))
				printk("kdsa: fatal: failed to submit desc (rc %d)\n", rc);
		} else {
			rc = poll(ctx->batch_comp);
			if (unlikely(rc != DSA_COMP_SUCCESS))
				printk("kdsa: fatal: failed to poll (rc %d)\n", rc);
			else
				ctx->io_cnt += NR_DESC;

			for (i = 0; i < NR_DESC; i++)
				ctx->comp[i]->status = 0;
			ctx->batch_comp->status = 0;
		}

		return;
#endif
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
	dma_unmap_single(ctx->chan->device->dev, ctx->batch_comp_dma, sizeof(struct dsa_completion_record), DMA_BIDIRECTIONAL);
	kmem_cache_free(comp_cache, ctx->batch_comp);

	// IOVA for Batch
	dma_unmap_single(ctx->chan->device->dev, ctx->desc_list_dma, NR_DESC * sizeof(struct dsa_hw_desc), DMA_BIDIRECTIONAL);

	// IOVA
	dma_unmap_single(ctx->chan->device->dev, ctx->src_dma, BLK_SIZE, DMA_BIDIRECTIONAL);
	dma_unmap_single(ctx->chan->device->dev, ctx->dst_dma, BLK_SIZE, DMA_BIDIRECTIONAL);
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
	int nid, cid;
	int tid;
	int rc;
	long long int begin[NR_THREAD];
	long long int end[NR_THREAD];
	long long int b, e;
	long long int total_io_cnt;
	long long int elapsed_ns;

	// Channel
	for (nid = 0; nid < NR_NUMA; nid++)
		for (cid = 0; cid < NR_CHAN; cid++) {
			snprintf(chan_name, 16, "dma%dchan%d", nid, cid);
			dma_chan[nid][cid] = request_channel(chan_name);
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

	msleep(10000);

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
		for (tid = 0; tid < NR_THREAD; tid++) {
			begin[tid] = ktime_to_ns(begin_ktime[tid]);
			end[tid] = ktime_to_ns(end_ktime[tid]);
		}

		b = find_min_max(begin, NR_THREAD, 0);
		e = find_min_max(end, NR_THREAD, 1);

		total_io_cnt = 0;
		for (tid = 0; tid < NR_THREAD; tid++)
			total_io_cnt += ctxs[tid].io_cnt;
		elapsed_ns = e - b;

		printk("kdsa: ======== Result ========\n");
		printk("kdsa: io:         %lld\n", total_io_cnt);
		printk("kdsa: elapsed:    %lld μs\n", elapsed_ns / 1000);
		printk("kdsa: bandwidth:  %lld.%03lld MIOPS\n",
				(total_io_cnt * 1000) / elapsed_ns,
				((total_io_cnt * 1000000) / elapsed_ns) % 1000);
	} else {
		printk("kdsa: failed to test\n");
	}

	// Completion cache
	kmem_cache_destroy(comp_cache);

	// Channel
	for (nid = 0; nid < NR_NUMA; nid++)
		for (cid = 0; cid < NR_CHAN; cid++)
			if (dma_chan[nid][cid])
				dma_release_channel(dma_chan[nid][cid]);

	// rc == 0 means success; the return code is intentional to avoid rmmod
	return rc ? rc : -EPERM;
}
module_init(kdsa_init);

MODULE_LICENSE("GPL");
