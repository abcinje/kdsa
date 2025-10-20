#include <stdio.h>
#include <stdint.h>

typedef uint64_t u64;

struct sw_err_reg {
    union {
        struct {
            u64 valid:1;
            u64 overflow:1;
            u64 desc_valid:1;
            u64 wq_idx_valid:1;
            u64 batch:1;
            u64 fault_rw:1;
            u64 priv:1;
            u64 rsvd:1;
            u64 error:8;
            u64 wq_idx:8;
            u64 rsvd2:8;
            u64 operation:8;
            u64 pasid:20;
            u64 rsvd3:4;

            u64 batch_idx:16;
            u64 rsvd4:16;
            u64 invalid_flags:32;

            u64 fault_addr;

            u64 rsvd5;
        };
        u64 bits[4];
    };
} __attribute__((packed));

void print_sw_err_reg(struct sw_err_reg *reg) {
    printf("bits[0] = 0x%016llx\n", reg->bits[0]);
    printf("  valid        = %llu\n", reg->valid);
    printf("  overflow     = %llu\n", reg->overflow);
    printf("  desc_valid   = %llu\n", reg->desc_valid);
    printf("  wq_idx_valid = %llu\n", reg->wq_idx_valid);
    printf("  batch        = %llu\n", reg->batch);
    printf("  fault_rw     = %llu\n", reg->fault_rw);
    printf("  priv         = %llu\n", reg->priv);
    printf("  rsvd         = %llu\n", reg->rsvd);
    printf("  error        = 0x%02llx\n", reg->error);
    printf("  wq_idx       = 0x%02llx\n", reg->wq_idx);
    printf("  rsvd2        = 0x%02llx\n", reg->rsvd2);
    printf("  operation    = 0x%02llx\n", reg->operation);
    printf("  pasid        = 0x%05llx\n", reg->pasid);
    printf("  rsvd3        = 0x%01llx\n", reg->rsvd3);

    printf("bits[1] = 0x%016llx\n", reg->bits[1]);
    printf("  batch_idx    = 0x%04llx\n", reg->batch_idx);
    printf("  rsvd4        = 0x%04llx\n", reg->rsvd4);
    printf("  invalid_flags= 0x%08llx\n", reg->invalid_flags);

    printf("bits[2] = 0x%016llx\n", reg->bits[2]);
    printf("  fault_addr   = 0x%016llx\n", reg->fault_addr);

    printf("bits[3] = 0x%016llx\n", reg->bits[3]);
    printf("  rsvd5        = 0x%016llx\n", reg->rsvd5);
}

int main() {
    struct sw_err_reg reg = {0};
    reg.bits[0] = 0x0000010300011a2fULL;
    reg.bits[1] = 0x0000000000000000ULL;
    reg.bits[2] = 0x00000000ffea2000ULL;
    reg.bits[3] = 0x0000000000000000ULL;

    print_sw_err_reg(&reg);
    return 0;
}
