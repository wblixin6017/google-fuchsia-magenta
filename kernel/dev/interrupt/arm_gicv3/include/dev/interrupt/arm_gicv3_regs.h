#pragma once

#include <platform/gic.h>
#include <reg.h>

#define GICREG(gic, reg)   (*REG32(GICBASE(gic) + (reg)))
#define GICREG64(gic, reg) (*REG64(GICBASE(gic) + (reg)))

#define DEFINE_ICC_SYS_REG(name, reg)                   \
static inline uint32_t gic_read_##name(void) {          \
    uint64_t temp;                                      \
   __asm__ volatile("mrs %0, " reg : "=r"(temp));       \
    return temp;                                        \
}                                                       \
static inline void gic_write_##name(uint32_t value) {   \
   __asm__ volatile("msr " reg ", %0" :: "r"((uint64_t)value));   \
}

DEFINE_ICC_SYS_REG(ctlr_el1, "S3_0_C12_C12_4")
DEFINE_ICC_SYS_REG(pmr_el1, "S3_0_C4_C6_0")
DEFINE_ICC_SYS_REG(iar1_el1, "S3_0_C12_C12_0")
DEFINE_ICC_SYS_REG(sre_el1, "S3_0_C12_C12_5")
DEFINE_ICC_SYS_REG(bpr1_el1, "S3_0_C12_C12_3")
DEFINE_ICC_SYS_REG(igrpen1_el1, "S3_0_C12_C12_7")
DEFINE_ICC_SYS_REG(eoir1_el1, "S3_0_C12_C12_1")

/* distributor registers */

#define GICD_CTLR               (GICD_OFFSET + 0x0000)
#define GICD_TYPER              (GICD_OFFSET + 0x0004)
#define GICD_IIDR               (GICD_OFFSET + 0x0008)
#define GICD_IGROUPR(n)         (GICD_OFFSET + 0x0080 + (n) * 4)
#define GICD_ISENABLER(n)       (GICD_OFFSET + 0x0100 + (n) * 4)
#define GICD_ICENABLER(n)       (GICD_OFFSET + 0x0180 + (n) * 4)
#define GICD_ISPENDR(n)         (GICD_OFFSET + 0x0200 + (n) * 4)
#define GICD_ICPENDR(n)         (GICD_OFFSET + 0x0280 + (n) * 4)
#define GICD_ISACTIVER(n)       (GICD_OFFSET + 0x0300 + (n) * 4)
#define GICD_ICACTIVER(n)       (GICD_OFFSET + 0x0380 + (n) * 4)
#define GICD_IPRIORITYR(n)      (GICD_OFFSET + 0x0400 + (n) * 4)
#define GICD_ITARGETSR(n)       (GICD_OFFSET + 0x0800 + (n) * 4)
#define GICD_ICFGR(n)           (GICD_OFFSET + 0x0c00 + (n) * 4)
#define GICD_NSACR(n)           (GICD_OFFSET + 0x0e00 + (n) * 4)
#define GICD_SGIR               (GICD_OFFSET + 0x0f00)
#define GICD_CPENDSGIR(n)       (GICD_OFFSET + 0x0f10 + (n) * 4)
#define GICD_SPENDSGIR(n)       (GICD_OFFSET + 0x0f20 + (n) * 4)
#define GICD_IROUTER(n)         (GICD_OFFSET + 0x6000 + (n) * 8)

/* redistributor registers */

#define GICR_SGI_OFFSET         (GICR_OFFSET + 0x10000)

#define GICR_IGROUPR(n)         (GICR_SGI_OFFSET + 0x0080 + (n) * 4)
#define GICR_IGRPMOD(n)         (GICR_SGI_OFFSET + 0x0d00 + (n) * 4)
#define GICR_ISENABLER(n)       (GICR_SGI_OFFSET + 0x0100 + (n) * 4)
#define GICR_ICENABLER(n)       (GICR_SGI_OFFSET + 0x0180 + (n) * 4)
#define GICR_ISPENDR(n)         (GICR_SGI_OFFSET + 0x0200 + (n) * 4)
#define GICR_ICPENDR(n)         (GICR_SGI_OFFSET + 0x0280 + (n) * 4)
#define GICR_ISACTIVER(n)       (GICR_SGI_OFFSET + 0x0300 + (n) * 4)
#define GICR_ICACTIVER(n)       (GICR_SGI_OFFSET + 0x0380 + (n) * 4)
#define GICR_IPRIORITYR(n)      (GICR_SGI_OFFSET + 0x0400 + (n) * 4)
#define GICR_ICFGR0             (GICR_SGI_OFFSET + 0x0c00)
#define GICR_ICFGR1             (GICR_SGI_OFFSET + 0x0c04)
#define GICR_NSACR              (GICR_SGI_OFFSET + 0x0e00)

/* peripheral identification registers */

#define GICD_CIDR0              (GICD_OFFSET + 0xfff0)
#define GICD_CIDR1              (GICD_OFFSET + 0xfff4)
#define GICD_CIDR2              (GICD_OFFSET + 0xfff8)
#define GICD_CIDR3              (GICD_OFFSET + 0xfffc)
#define GICD_PIDR0              (GICD_OFFSET + 0xffe0)
#define GICD_PIDR1              (GICD_OFFSET + 0xffe4)
#define GICD_PIDR2              (GICD_OFFSET + 0xffe8)
#define GICD_PIDR3              (GICD_OFFSET + 0xffec)
