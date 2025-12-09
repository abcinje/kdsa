#include "driver.h"

#include <asm/processor.h>

#define WQ_DEDICATED	(0)
#define COMP_RETRIES	(200000)

static int __maybe_unused idxd_enqcmds(struct idxd_wq *wq, void __iomem *portal, const void *desc)
{
	unsigned int retries = wq->enqcmds_retries;
	int rc;

	do {
		rc = enqcmds(portal, desc);
		if (rc == 0)
			break;
		cpu_relax();
	} while (retries--);

	return rc;
}

static int submit_desc(struct idxd_wq *wq, struct dsa_hw_desc *desc)
{
	void __iomem *portal;

	portal = idxd_wq_portal_addr(wq);

#if (WQ_DEDICATED == 1)
	// Dedicated WQs
	movdir64b(portal, desc);
	return 0;
#elif (WQ_DEDICATED == 0)
	// Shared WQs
	return idxd_enqcmds(wq, portal, desc);
#else
#error Invalid WQ mode
#endif
}

void prep(struct dsa_hw_desc *desc, u32 pasid, u8 opcode, u64 addr_f1, u64 addr_f2, u64 len, u64 compl, u32 flags)
{
	memset(desc, 0, sizeof(struct dsa_hw_desc));

	desc->pasid = pasid;
	desc->flags = flags;
	desc->opcode = opcode;
	desc->src_addr = addr_f1;
	desc->dst_addr = addr_f2;
	desc->xfer_size = len;
	desc->priv = 0;
	desc->completion_addr = compl;
}

int submit(struct dma_chan *c, struct dsa_hw_desc *desc)
{
	struct idxd_wq *wq = to_idxd_wq(c);

	return submit_desc(wq, desc);
}

int poll(struct dsa_completion_record *comp)
{
	int retry = 0;
	volatile uint8_t *status = &comp->status;

	while (DSA_COMP_STATUS(*status) == 0 && retry++ < COMP_RETRIES)
		cpu_relax();

	return DSA_COMP_STATUS(*status);
}

void print_comp(const struct dsa_completion_record *comp)
{
	printk("kdsa: comp (status %u, fault_addr %#llx)\n", comp->status, comp->fault_addr);
}
