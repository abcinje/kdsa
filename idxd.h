#ifndef _USR_IDXD_H_
#define _USR_IDXD_H_

/* Opcode */
enum dsa_opcode {
	DSA_OPCODE_NOOP = 0,
	DSA_OPCODE_BATCH,
	DSA_OPCODE_DRAIN,
	DSA_OPCODE_MEMMOVE,
	DSA_OPCODE_MEMFILL,
	DSA_OPCODE_COMPARE,
	DSA_OPCODE_COMPVAL,
	DSA_OPCODE_CR_DELTA,
	DSA_OPCODE_AP_DELTA,
	DSA_OPCODE_DUALCAST,
	DSA_OPCODE_TRANSL_FETCH,
	DSA_OPCODE_CRCGEN = 0x10,
	DSA_OPCODE_COPY_CRC,
	DSA_OPCODE_DIF_CHECK,
	DSA_OPCODE_DIF_INS,
	DSA_OPCODE_DIF_STRP,
	DSA_OPCODE_DIF_UPDT,
	DSA_OPCODE_DIX_GEN = 0x17,
	DSA_OPCODE_CFLUSH = 0x20,
	DSA_OPCODE_UPDATE_WIN,
	DSA_OPCODE_RS_IPASID_MEMCOPY = 0x23,
	DSA_OPCODE_RS_IPASID_FILL,
	DSA_OPCODE_RS_IPASID_COMPARE,
	DSA_OPCODE_RS_IPASID_COMPVAL,
	DSA_OPCODE_RS_IPASID_CFLUSH,
	DSA_OPCODE_URS_IPASID_MEMCOPY = 0x33,
	DSA_OPCODE_URS_IPASID_FILL,
	DSA_OPCODE_URS_IPASID_COMPARE,
	DSA_OPCODE_URS_IPASID_COMPVAL,
	DSA_OPCODE_URS_IPASID_CFLUSH,
};

struct dsa_hw_desc {
	uint32_t	pasid:20;
	uint32_t	rsvd:11;
	uint32_t	priv:1;
	uint32_t	flags:24;
	uint32_t	opcode:8;
	uint64_t	completion_addr;
	union {
		uint64_t	src_addr;
		uint64_t	rdback_addr;
		uint64_t	pattern;
		uint64_t	desc_list_addr;
		uint64_t	win_base_addr;
		uint64_t	pattern_lower;
		uint64_t	transl_fetch_addr;
	};
	union {
		uint64_t	dst_addr;
		uint64_t	rdback_addr2;
		uint64_t	src2_addr;
		uint64_t	comp_pattern;
		uint64_t	win_size;
	};
	union {
		uint32_t	xfer_size;
		uint32_t	desc_count;
		uint32_t	region_size;
	};
	uint16_t	int_handle;
	uint16_t	rsvd1;
	union {
		uint8_t		expected_res;
		/* create delta record */
		struct {
			uint64_t	delta_addr;
			uint32_t	max_delta_size;
			uint32_t 	delt_rsvd;
			uint8_t 	expected_res_mask;
		};
		uint32_t	delta_rec_size;
		uint64_t	dest2;
		/* CRC */
		struct {
			uint32_t	crc_seed;
			uint32_t	crc_rsvd;
			uint64_t	seed_addr;
		};
		/* DIF check or strip */
		struct {
			uint8_t		src_dif_flags;
			uint8_t		dif_chk_res;
			uint8_t		dif_chk_flags;
			uint8_t		dif_chk_res2[5];
			uint32_t	chk_ref_tag_seed;
			uint16_t	chk_app_tag_mask;
			uint16_t	chk_app_tag_seed;
		};
		/* DIF insert */
		struct {
			uint8_t		dif_ins_res;
			uint8_t		dest_dif_flag;
			uint8_t		dif_ins_flags;
			uint8_t		dif_ins_res2[13];
			uint32_t	ins_ref_tag_seed;
			uint16_t	ins_app_tag_mask;
			uint16_t	ins_app_tag_seed;
		};
		/* DIF update */
		struct {
			uint8_t		src_upd_flags;
			uint8_t		upd_dest_flags;
			uint8_t		dif_upd_flags;
			uint8_t		dif_upd_res[5];
			uint32_t	src_ref_tag_seed;
			uint16_t	src_app_tag_mask;
			uint16_t	src_app_tag_seed;
			uint32_t	dest_ref_tag_seed;
			uint16_t	dest_app_tag_mask;
			uint16_t	dest_app_tag_seed;
		};

		/* restricted ops with interpasid */
		struct {
			uint8_t rest_ip_res1[20];
			union {
				uint16_t src_pasid_hndl;
				uint8_t rest_ip_res2[2];
				uint16_t src1_pasid_hndl;
			};
			union {
				uint16_t dest_pasid_hndl;
				uint16_t src2_pasid_hndl;
				uint8_t rest_ip_res3[2];
			};
		};

		/* unrestricted ops with interpasid */
		struct {
			uint8_t unrest_ip_res1[8];
			union {
				/* Generic */
				struct {
					uint32_t addr1_pasid:20;
					uint32_t unrest_ip_res2:11;
					uint32_t addr1_priv:1;
				};
				/* Memcopy, Compare Pattern */
				struct {
					uint32_t src_pasid:20;
					uint32_t unrest_ip_res3:11;
					uint32_t unrest_src_priv:1;
				};
				/* Fill, Cache Flush */
				struct {
					uint32_t unrest_ip_res4: 32;
				};
				/* Compare */
				struct {
					uint32_t src1_pasid:20;
					uint32_t unrest_ip_res5:11;
					uint32_t unrest_src1_priv:1;
				};
			};
			union {
				/* Generic */
				struct {
					uint32_t addr2_pasid:20;
					uint32_t unrest_ip_res6:11;
					uint32_t addr2_priv:1;
				};
				/* Memcopy, Fill */
				struct {
					uint32_t dest_pasid:20;
					uint32_t unrest_ip_res7:11;
					uint32_t unrest_dest_priv:1;
				};
				/* Compare */
				struct {
					uint32_t src2_pasid:20;
					uint32_t unrest_ip_res8:11;
					uint32_t unrest_src2_priv:1;
				};
				/* Compare Pattern */
				struct {
					uint32_t unrest_ip_res9: 32;
				};
			};
			uint64_t unrest_ip_res10:48;
			uint16_t ipt_handle;
		};

		/* Update window */
		struct {
			uint8_t update_win_resv2[21];
			uint8_t idpt_win_flags;
			uint16_t idpt_win_handle;
		};

		/* Fill */
		struct {
			uint64_t	pattern_upper;
		};

		/* Translation fetch */
		struct {
			uint64_t	transl_fetch_res;
			uint32_t	region_stride;
		};

		/* DIX generate */
		struct {
			uint8_t		dix_gen_res;
			uint8_t		dest_dif_flags;
			uint8_t		dif_flags;
			uint8_t		dix_gen_res2[13];
			uint32_t	ref_tag_seed;
			uint16_t	app_tag_mask;
			uint16_t	app_tag_seed;
		};

		uint8_t		op_specific[24];
	};
} __attribute__((packed));

enum idxd_win_type {
	IDXD_WIN_TYPE_SA_SS = 0,
	IDXD_WIN_TYPE_SA_MS,
};

#endif
