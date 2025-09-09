#ifndef __DSA_H__
#define __DSA_H__

#include "idxd.h"

static inline unsigned char enqcmd(void *dst, const void *src)
{
	unsigned char retry;

	asm volatile(".byte 0xf2, 0x0f, 0x38, 0xf8, 0x02\t\n"
			"setz %0\t\n"
			: "=r"(retry) : "a" (dst), "d" (src));
	return retry;
}

/*
static inline void movdir64b(void *dst, const void *src)
{
	asm volatile(".byte 0x66, 0x0f, 0x38, 0xf8, 0x02\t\n"
		: : "a" (dst), "d" (src));
}
*/

static __always_inline
void dsa_desc_submit(void *wq_portal, int dedicated,
		struct dsa_hw_desc *hw)
{
	if (dedicated)
		movdir64b(wq_portal, hw);
	else /* retry infinitely, a retry param is not needed at this time */
		while (enqcmd(wq_portal, hw))
			;
}

#endif
