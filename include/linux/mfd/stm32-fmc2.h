/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) STMicroelectronics 2020
 */

#ifndef _LINUX_STM32_FMC2_H_
#define _LINUX_STM32_FMC2_H_

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/regmap.h>

/* FMC2 Controller Registers */
#define FMC2_BCR1			0x0
#define FMC2_BTR1			0x4
#define FMC2_BCR(x)			((x) * 0x8 + FMC2_BCR1)
#define FMC2_BTR(x)			((x) * 0x8 + FMC2_BTR1)
#define FMC2_PCSCNTR			0x20
#define FMC2_PCR			0x80
#define FMC2_SR				0x84
#define FMC2_PMEM			0x88
#define FMC2_PATT			0x8c
#define FMC2_HECCR			0x94
#define FMC2_BWTR1			0x104
#define FMC2_BWTR(x)			((x) * 0x8 + FMC2_BWTR1)
#define FMC2_ISR			0x184
#define FMC2_ICR			0x188
#define FMC2_CSQCR			0x200
#define FMC2_CSQCFGR1			0x204
#define FMC2_CSQCFGR2			0x208
#define FMC2_CSQCFGR3			0x20c
#define FMC2_CSQAR1			0x210
#define FMC2_CSQAR2			0x214
#define FMC2_CSQIER			0x220
#define FMC2_CSQISR			0x224
#define FMC2_CSQICR			0x228
#define FMC2_CSQEMSR			0x230
#define FMC2_BCHIER			0x250
#define FMC2_BCHISR			0x254
#define FMC2_BCHICR			0x258
#define FMC2_BCHPBR1			0x260
#define FMC2_BCHPBR2			0x264
#define FMC2_BCHPBR3			0x268
#define FMC2_BCHPBR4			0x26c
#define FMC2_BCHDSR0			0x27c
#define FMC2_BCHDSR1			0x280
#define FMC2_BCHDSR2			0x284
#define FMC2_BCHDSR3			0x288
#define FMC2_BCHDSR4			0x28c

/* Register: FMC2_BCR1 */
#define FMC2_BCR1_CCLKEN		BIT(20)
#define FMC2_BCR1_FMC2EN		BIT(31)

/* Register: FMC2_BCRx */
#define FMC2_BCR_MBKEN			BIT(0)
#define FMC2_BCR_MUXEN			BIT(1)
#define FMC2_BCR_MTYP			GENMASK(3, 2)
#define FMC2_BCR_MWID			GENMASK(5, 4)
#define FMC2_BCR_FACCEN			BIT(6)
#define FMC2_BCR_BURSTEN		BIT(8)
#define FMC2_BCR_WAITPOL		BIT(9)
#define FMC2_BCR_WAITCFG		BIT(11)
#define FMC2_BCR_WREN			BIT(12)
#define FMC2_BCR_WAITEN			BIT(13)
#define FMC2_BCR_EXTMOD			BIT(14)
#define FMC2_BCR_ASYNCWAIT		BIT(15)
#define FMC2_BCR_CPSIZE			GENMASK(18, 16)
#define FMC2_BCR_CBURSTRW		BIT(19)
#define FMC2_BCR_NBLSET			GENMASK(23, 22)

/* Register: FMC2_BTRx/FMC2_BWTRx */
#define FMC2_BXTR_ADDSET		GENMASK(3, 0)
#define FMC2_BXTR_ADDHLD		GENMASK(7, 4)
#define FMC2_BXTR_DATAST		GENMASK(15, 8)
#define FMC2_BXTR_BUSTURN		GENMASK(19, 16)
#define FMC2_BTR_CLKDIV			GENMASK(23, 20)
#define FMC2_BTR_DATLAT			GENMASK(27, 24)
#define FMC2_BXTR_ACCMOD		GENMASK(29, 28)
#define FMC2_BXTR_DATAHLD		GENMASK(31, 30)

/* Register: FMC2_PCSCNTR */
#define FMC2_PCSCNTR_CSCOUNT		GENMASK(15, 0)
#define FMC2_PCSCNTR_CNTBEN(x)		BIT((x) + 16)

/* Register: FMC2_PCR */
#define FMC2_PCR_PWAITEN		BIT(1)
#define FMC2_PCR_PBKEN			BIT(2)
#define FMC2_PCR_PWID			GENMASK(5, 4)
#define FMC2_PCR_PWID_BUSWIDTH_8	0
#define FMC2_PCR_PWID_BUSWIDTH_16	1
#define FMC2_PCR_ECCEN			BIT(6)
#define FMC2_PCR_ECCALG			BIT(8)
#define FMC2_PCR_TCLR			GENMASK(12, 9)
#define FMC2_PCR_TCLR_DEFAULT		0xf
#define FMC2_PCR_TAR			GENMASK(16, 13)
#define FMC2_PCR_TAR_DEFAULT		0xf
#define FMC2_PCR_ECCSS			GENMASK(19, 17)
#define FMC2_PCR_ECCSS_512		1
#define FMC2_PCR_ECCSS_2048		3
#define FMC2_PCR_BCHECC			BIT(24)
#define FMC2_PCR_WEN			BIT(25)

/* Register: FMC2_SR */
#define FMC2_SR_NWRF			BIT(6)

/* Register: FMC2_PMEM */
#define FMC2_PMEM_MEMSET		GENMASK(7, 0)
#define FMC2_PMEM_MEMWAIT		GENMASK(15, 8)
#define FMC2_PMEM_MEMHOLD		GENMASK(23, 16)
#define FMC2_PMEM_MEMHIZ		GENMASK(31, 24)

/* Register: FMC2_PATT */
#define FMC2_PATT_ATTSET		GENMASK(7, 0)
#define FMC2_PATT_ATTWAIT		GENMASK(15, 8)
#define FMC2_PATT_ATTHOLD		GENMASK(23, 16)
#define FMC2_PATT_ATTHIZ		GENMASK(31, 24)

/* Register: FMC2_ISR */
#define FMC2_ISR_IHLF			BIT(1)

/* Register: FMC2_ICR */
#define FMC2_ICR_CIHLF			BIT(1)

/* Register: FMC2_CSQCR */
#define FMC2_CSQCR_CSQSTART		BIT(0)

/* Register: FMC2_CSQCFGR1 */
#define FMC2_CSQCFGR1_CMD2EN		BIT(1)
#define FMC2_CSQCFGR1_DMADEN		BIT(2)
#define FMC2_CSQCFGR1_ACYNBR		GENMASK(6, 4)
#define FMC2_CSQCFGR1_CMD1		GENMASK(15, 8)
#define FMC2_CSQCFGR1_CMD2		GENMASK(23, 16)
#define FMC2_CSQCFGR1_CMD1T		BIT(24)
#define FMC2_CSQCFGR1_CMD2T		BIT(25)

/* Register: FMC2_CSQCFGR2 */
#define FMC2_CSQCFGR2_SQSDTEN		BIT(0)
#define FMC2_CSQCFGR2_RCMD2EN		BIT(1)
#define FMC2_CSQCFGR2_DMASEN		BIT(2)
#define FMC2_CSQCFGR2_RCMD1		GENMASK(15, 8)
#define FMC2_CSQCFGR2_RCMD2		GENMASK(23, 16)
#define FMC2_CSQCFGR2_RCMD1T		BIT(24)
#define FMC2_CSQCFGR2_RCMD2T		BIT(25)

/* Register: FMC2_CSQCFGR3 */
#define FMC2_CSQCFGR3_SNBR		GENMASK(13, 8)
#define FMC2_CSQCFGR3_AC1T		BIT(16)
#define FMC2_CSQCFGR3_AC2T		BIT(17)
#define FMC2_CSQCFGR3_AC3T		BIT(18)
#define FMC2_CSQCFGR3_AC4T		BIT(19)
#define FMC2_CSQCFGR3_AC5T		BIT(20)
#define FMC2_CSQCFGR3_SDT		BIT(21)
#define FMC2_CSQCFGR3_RAC1T		BIT(22)
#define FMC2_CSQCFGR3_RAC2T		BIT(23)

/* Register: FMC2_CSQCAR1 */
#define FMC2_CSQCAR1_ADDC1		GENMASK(7, 0)
#define FMC2_CSQCAR1_ADDC2		GENMASK(15, 8)
#define FMC2_CSQCAR1_ADDC3		GENMASK(23, 16)
#define FMC2_CSQCAR1_ADDC4		GENMASK(31, 24)

/* Register: FMC2_CSQCAR2 */
#define FMC2_CSQCAR2_ADDC5		GENMASK(7, 0)
#define FMC2_CSQCAR2_NANDCEN		GENMASK(11, 10)
#define FMC2_CSQCAR2_SAO		GENMASK(31, 16)

/* Register: FMC2_CSQIER */
#define FMC2_CSQIER_TCIE		BIT(0)

/* Register: FMC2_CSQICR */
#define FMC2_CSQICR_CLEAR_IRQ		GENMASK(4, 0)

/* Register: FMC2_CSQEMSR */
#define FMC2_CSQEMSR_SEM		GENMASK(15, 0)

/* Register: FMC2_BCHIER */
#define FMC2_BCHIER_DERIE		BIT(1)
#define FMC2_BCHIER_EPBRIE		BIT(4)

/* Register: FMC2_BCHICR */
#define FMC2_BCHICR_CLEAR_IRQ		GENMASK(4, 0)

/* Register: FMC2_BCHDSR0 */
#define FMC2_BCHDSR0_DUE		BIT(0)
#define FMC2_BCHDSR0_DEF		BIT(1)
#define FMC2_BCHDSR0_DEN		GENMASK(7, 4)

/* Register: FMC2_BCHDSR1 */
#define FMC2_BCHDSR1_EBP1		GENMASK(12, 0)
#define FMC2_BCHDSR1_EBP2		GENMASK(28, 16)

/* Register: FMC2_BCHDSR2 */
#define FMC2_BCHDSR2_EBP3		GENMASK(12, 0)
#define FMC2_BCHDSR2_EBP4		GENMASK(28, 16)

/* Register: FMC2_BCHDSR3 */
#define FMC2_BCHDSR3_EBP5		GENMASK(12, 0)
#define FMC2_BCHDSR3_EBP6		GENMASK(28, 16)

/* Register: FMC2_BCHDSR4 */
#define FMC2_BCHDSR4_EBP7		GENMASK(12, 0)
#define FMC2_BCHDSR4_EBP8		GENMASK(28, 16)

/*
 * struct stm32_fmc2 - STM32 FMC2 data assigned by parent device
 * @clk: clock reference for this instance
 * @regmap: register map reference for this instance
 * @reg_phys_addr: physical address of the register map
 * @nb_ctrl_used: number of used controller
 * @nwait_is_used: NWAIT signal in used by a controller
 * @enable: enable the FMC2 IP
 * @disable: disable the FMC2 IP
 */
struct stm32_fmc2 {
	struct clk *clk;
	struct regmap *regmap;
	phys_addr_t reg_phys_addr;
	atomic_t nb_ctrl_used;
	atomic_t nwait_is_used;
	void (*enable)(struct stm32_fmc2 *fmc2);
	void (*disable)(struct stm32_fmc2 *fmc2);
};

#endif
