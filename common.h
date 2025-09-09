#ifndef __COMMON_H__
#define __COMMON_H__

#include "idxd.h"

/* max number of operands e.g., dual cast - src1, dst1, dst2 */
#define NUM_ADDR_MAX	3

#define OP_NONE	1
enum {
	OP_FETCH = OP_NONE + 1,
	OP_DEMOTE,
	OP_FLUSH
};

enum {
	OP_READ = OP_NONE + 1,
	OP_WRITE
};

struct mmio_mem {
	char *bfile;
	uint64_t mmio_offset;
	void *base_addr;
	uint64_t sz;
	int fd;
};

/*
 • This structure contains test parameters and controls for a given
 • invocation of the tool, and common across all executing threads.
 * Thread-specific parameters should go into struct tcfg_cpu.
 */
struct tcfg {
	uint64_t blen;				/* buffer size (-s) */
	uint64_t bstride;			/* buffer stride (-t) */
	uint32_t nb_bufs;			/* buffer count (-n) */
	int qd;					/* queue depth (-q) */
	uint32_t nb_k;				/* cpu count for test - parsed from -k */
	uint32_t nb_K;				/* cpu count for test - parsed from -K */
	uint32_t nb_cpus;			/* cpu count for test - parsed from -k/-K */
	uint32_t pg_size;			/* 0 - 4K, 1 - 2Mb, 2 - 1G (-l) */
	uint32_t wq_type;			/* wq type (-w) */
	uint32_t batch_sz;			/* batch size (-b) */
	uint64_t iter;				/* iterations (-i) */
	uint32_t op;				/* opcode (-o) */
	bool verify;				/* verify data after generating descriptors (-v)  */
	bool dma;				/* use dma v/s memcpy (-m) */
	bool var_mmio;				/* portal mmio address is varied (-c) */
	uint8_t bl_idx;				/* selects one of the 4 block lengths (-e) */
	uint16_t bl_len;			/* block length for DIF ops, derived from bl_idx */
	int delta;				/* reciprocal of delta fraction (-D) */
	uint32_t delta_rec_size;		/* derived from buffer size (-S) and -D options */
	int tval_secs;				/* -T */

	int numa_node_default[NUM_ADDR_MAX];	/* default NUMA allocation (-1, -1) */
	int (*numa_node)[NUM_ADDR_MAX];		/* NUMA allocation (-S) */

	int place_op[NUM_ADDR_MAX];		/* -y */
	int access_op[NUM_ADDR_MAX];		/* -z */
	uint16_t buf_off[NUM_ADDR_MAX];		/* -O */
	uint32_t misc_flags;			/* -x */
	uint32_t ccmask;			/* cache control mask (-f) */
	uint32_t flags_cmask;			/* flags to clear in the descriptor (-F) */
	uint32_t flags_smask;			/* flags to set in the descriptor (-F) */
	uint32_t flags_nth_desc;		/* flags to set at every "flags_nth_desc"th descriptor in a batch (-Y) */
	int proc;				/* uses processes not threads (-P) */
	int driver;				/* user driver(uio/vfio_pci) (-u) */
	int nb_user_eng;			/* number of engines to use with -u */
	uint32_t transl_fetch;			/* interval at which the translation fetch descriptor to add (-X) */
	int drain_desc;				/* drain desc (-Y) */
	bool shuffle_descs;			/* shuffle descriptors */

	uint64_t blen_arr[NUM_ADDR_MAX];
	uint64_t bstride_arr[NUM_ADDR_MAX];

	union {					/* fill/pattern value used for o4/o6 */
		uint64_t fill;
		uint64_t pat;
	};

	uint32_t nb_desc;			/* num descriptors not including batch */

	uint64_t bw_cycles;
	uint64_t cycles;			/* avg of execution cycles across CPUs */
	uint64_t retry;				/* completion polling retries */
	uint64_t mwait_cycles;			/* cycles spent in mwait */
	float cpu_util;				/* cpu utilization needed to prep/submit descriptors */
	int kops_rate;				/* operation rate - kilo operations/sec */
	float latency;				/* latency for n descriptors */
	float bw;				/* operation BW */
	uint64_t retries_per_sec;		/* pollling retries the CPU can do per sec  */
	uint64_t cycles_per_sec;		/* rdtsc cycles per sec */
	uint64_t drain_lat;			/* calculated drain latency per descriptor */

	struct thread_data *td;			/* barrier */
	struct tcfg_cpu *tcpu;			/* per worker data */

	struct numa_mem *numa_mem;		/* per memory node info */
	int nb_numa_node;			/* size of each of the numa_xyz arrays
						 * nb_numa_node is the index of the
						 * highest numa node id on the system
						 */
	int *numa_nb_cpu;			/* numa_nb_cpu[i] is the cpu count in node i */

	int vfio_fd;				/* VFIO filehandle (-u with vfio_pci) */

	struct op_info *op_info;
	uint32_t id_nb_owners;			/* number of inter domain owner processes */
	bool id_owners_given;			/* true if owners are specified by -R option */
	struct cpu_wq_info *id_owners;		/* array of inter domain owner processes */
	struct id_owner_info *id_owner_info;	/* array of inter domain owner processes */
	int id_oper;				/* inter domain operand type:
						 * 1-addr1, 2-addr2 3-both
						 */
	int id_idpte_type;			/* IDPTE type 0-SASS, 1-SAMS */
	uint32_t id_updt_win_interval; 		/* IDPT update window interval in terms of
						 * desc counts
						 */
	int id_window_mode;			/* 0 - Address mode; 1 - Offset mode */
	int id_window_enable;			/* 0 - Full Address Range; 1 - Window Range */
	int id_window_cnt;			/* Number of IDPT windows to be created */

	void * (*malloc)(size_t size, unsigned int align, int numa_node);


	bool cpu_desc_work;

	int mmio_fd_idx[NUM_ADDR_MAX];		/* address index (0 - 2) into mmio_mem */
	struct mmio_mem mmio_mem[NUM_ADDR_MAX];	/* per mmio file info - mmio files maybe duplicated
						 * in that case, mmio_idx points to a single mmio_mem
						 * struct
						 */

	bool large_stride;
	bool stop;
};

#endif
