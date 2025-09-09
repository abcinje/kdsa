#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <asm/page.h>
#include <linux/dmaengine.h>
#include <linux/idxd.h>

struct idxd_dma_chan {
	struct dma_chan chan;
	struct idxd_wq *wq;
};

struct idxd_wq {
	void __iomem *portal;
	u32 portal_offset;
	unsigned int enqcmds_retries;
};

#define IDXD_PORTAL_MASK	(PAGE_SIZE - 1)

static inline void __iomem *idxd_wq_portal_addr(struct idxd_wq *wq)
{
	int ofs = wq->portal_offset;

	wq->portal_offset = (ofs + sizeof(struct dsa_raw_desc)) & IDXD_PORTAL_MASK;
	return wq->portal + ofs;
}

static inline struct idxd_wq *to_idxd_wq(struct dma_chan *c)
{
	struct idxd_dma_chan *idxd_chan;

	idxd_chan = container_of(c, struct idxd_dma_chan, chan);
	return idxd_chan->wq;
}

void prep(struct dsa_hw_desc *desc, char opcode, u64 addr_f1, u64 addr_f2, u64 len, u64 compl, u32 flags);
int submit(struct dma_chan *c, struct dsa_hw_desc *desc);
int poll(struct dsa_completion_record *comp);

#endif
