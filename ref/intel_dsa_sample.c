#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/idxd.h>
#include <xmmintrin.h>
#include <accel-config/libaccel_config.h>

#define NOP_RETRY 10000
#define BLEN 4096

struct wq_info {
	bool wq_mapped;
	void *wq_portal;
	int wq_fd;
};

static inline int enqcmd(volatile void *reg, struct dsa_hw_desc *desc)
{
	uint8_t retry;

	asm volatile (".byte 0xf2, 0x0f, 0x38, 0xf8, 0x02\t\n"
			"setz %0\t\n":"=r" (retry):"a"(reg), "d"(desc));
	return (int)retry;
}
static inline void submit_desc(void *wq_portal, struct dsa_hw_desc *hw)
{
	while (enqcmd(wq_portal, hw)) _mm_pause();
}

static uint8_t op_status(uint8_t status)
{
	return status & DSA_COMP_STATUS_MASK;
}

static bool is_write_syscall_success(int fd)
{
	struct dsa_hw_desc desc = {0};
	struct dsa_completion_record comp __attribute__((aligned(32)));
	int retry = 0;
	int rc;

	desc.opcode = DSA_OPCODE_NOOP;
	desc.flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR;
	comp.status = 0;

	desc.completion_addr = (unsigned long)&comp;

	rc = write(fd, &desc, sizeof(desc));

	if (rc == sizeof(desc)) {
		while (comp.status == 0 && retry++ < NOP_RETRY)
			_mm_pause();

		if (comp.status == DSA_COMP_SUCCESS)
			return true;
	}

	return false;
}

static int map_wq(struct wq_info *wq_info)
{
	void *wq_portal;
	struct accfg_ctx *ctx;
	struct accfg_wq *wq;
	struct accfg_device *device;
	char path[PATH_MAX];
	int fd;
	int wq_found;

	wq_portal = MAP_FAILED;

	accfg_new(&ctx);

	accfg_device_foreach(ctx, device) {
		/*
		 * Use accfg_device_(*) functions to select enabled device
		 * based on name, numa node
		 */
		accfg_wq_foreach(device, wq) {
			if (accfg_wq_get_user_dev_path(wq, path, sizeof(path)))
				continue;
			/*
			 * Use accfg_wq_(*) functions select WQ of type
			 * ACCFG_WQT_USER and desired mode
			 */
			wq_found = accfg_wq_get_type(wq) == ACCFG_WQT_USER &&
					accfg_wq_get_mode(wq) == ACCFG_WQ_SHARED;

			if (wq_found)
				break;
		}

		if (wq_found)
			break;
	}

	accfg_unref(ctx);

	if (!wq_found)
		return -ENODEV;

	printf("wq found: %s\n", path);

	fd = open(path, O_RDWR);
	if (fd >= 0) {
		wq_portal = mmap(NULL, 0x1000, PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
	}

	if (wq_portal == MAP_FAILED) {
		/*
		 * EPERM means the driver doesn't support mmap but
		 * can support write syscall. So fallback to write syscall
		 */
		if (errno == EPERM && is_write_syscall_success(fd)) {
			wq_info->wq_mapped = false;
			wq_info->wq_fd = fd;
			return 0;
		}

		return -errno;
	}

	wq_info->wq_portal = wq_portal;
	wq_info->wq_mapped = true;
	wq_info->wq_fd = -1;

	return 0;
}

int main(int argc, char *argv[])
{
	int fd;
	struct dsa_hw_desc desc = { };
	char src[BLEN];
	char dst[BLEN];
	struct dsa_completion_record comp __attribute__((aligned(32)));
	uint32_t tlen;
	int rc;
	struct wq_info wq_info;

	rc = map_wq(&wq_info);
	if (rc)
		return EXIT_FAILURE;

	memset(src, 0xaa, BLEN);

	desc.opcode = DSA_OPCODE_MEMMOVE;

	/*
	 * Request a completion – since we poll on status, this flag
	 * needs to be 1 for status to be updated on successful
	 * completion
	 */
	desc.flags |= IDXD_OP_FLAG_RCR;

	/* CRAV should be 1 since RCR = 1 */
	desc.flags |= IDXD_OP_FLAG_CRAV;

	/* Hint to direct data writes to CPU cache */
	desc.flags |= IDXD_OP_FLAG_CC;
	desc.xfer_size = BLEN;
	desc.src_addr = (uintptr_t) src;
	desc.dst_addr = (uintptr_t) dst;
	desc.completion_addr = (uintptr_t)&comp;

retry:
	if (wq_info.wq_mapped) {
		submit_desc(wq_info.wq_portal, &desc);
	} else {
		int rc = write(wq_info.wq_fd, &desc, sizeof(desc));
		if (rc != sizeof(desc))
			return EXIT_FAILURE;
	}

	while (comp.status == 0) _mm_pause();

	if (comp.status != DSA_COMP_SUCCESS) {
		if (op_status(comp.status) == DSA_COMP_PAGE_FAULT_NOBOF) {
			int wr = comp.status & DSA_COMP_STATUS_WRITE;
			volatile char *t;
			t = (char *)comp.fault_addr;
			wr ? *t = *t : *t;
			desc.src_addr += comp.bytes_completed;
			desc.dst_addr += comp.bytes_completed;
			desc.xfer_size -= comp.bytes_completed;
			goto retry;
		} else {
			printf("desc failed status %u\n", comp.status);
			rc = EXIT_FAILURE;
		}
	} else {
		printf("desc successful\n");

		rc = memcmp(src, dst, BLEN);
		rc ? printf("memmove failed\n") : printf("memmove successful\n");
		rc = rc ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	return EXIT_SUCCESS;
}
