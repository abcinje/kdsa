#ifndef _DRIVER_H_
#define _DRIVER_H_

#include <asm/page.h>
#include <linux/dmaengine.h>
#include "idxd.h"

static inline struct idxd_wq *to_idxd_wq(struct dma_chan *c)
{
	struct idxd_dma_chan *idxd_chan;

	idxd_chan = container_of(c, struct idxd_dma_chan, chan);
	return idxd_chan->wq;
}

void prep(struct dsa_hw_desc *desc, u8 opcode, u64 addr_f1, u64 addr_f2, u64 len, u64 compl, u32 flags);
int submit(struct dma_chan *c, struct dsa_hw_desc *desc);
int poll(struct dsa_completion_record *comp);
void print_comp(const struct dsa_completion_record *comp);

#endif
