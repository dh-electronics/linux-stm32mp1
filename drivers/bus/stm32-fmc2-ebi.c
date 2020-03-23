// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2020
 */

#include <linux/mfd/stm32-fmc2.h>
#include <linux/module.h>
#include <linux/of_device.h>

#define FMC2_MAX_EBI_CE			4

#define FMC2_BCR_CPSIZE_0		0x0
#define FMC2_BCR_CPSIZE_128		0x1
#define FMC2_BCR_CPSIZE_256		0x2
#define FMC2_BCR_CPSIZE_512		0x3
#define FMC2_BCR_CPSIZE_1024		0x4

#define FMC2_BCR_MWID_8			0x0
#define FMC2_BCR_MWID_16		0x1

#define FMC2_BCR_MTYP_SRAM		0x0
#define FMC2_BCR_MTYP_PSRAM		0x1
#define FMC2_BCR_MTYP_NOR		0x2

#define FMC2_BXTR_EXTMOD_A		0x0
#define FMC2_BXTR_EXTMOD_B		0x1
#define FMC2_BXTR_EXTMOD_C		0x2
#define FMC2_BXTR_EXTMOD_D		0x3

#define FMC2_BCR_NBLSET_MAX		0x3
#define FMC2_BXTR_ADDSET_MAX		0xf
#define FMC2_BXTR_ADDHLD_MAX		0xf
#define FMC2_BXTR_DATAST_MAX		0xff
#define FMC2_BXTR_BUSTURN_MAX		0xf
#define FMC2_BXTR_DATAHLD_MAX		0x3
#define FMC2_BTR_CLKDIV_MAX		0xf
#define FMC2_BTR_DATLAT_MAX		0xf
#define FMC2_PCSCNTR_CSCOUNT_MAX	0xff

enum stm32_fmc2_ebi_register_type {
	FMC2_REG_BCR = 1,
	FMC2_REG_BTR,
	FMC2_REG_BWTR,
	FMC2_REG_PCSCNTR
};

enum stm32_fmc2_ebi_transaction_type {
	FMC2_ASYNC_MODE_1_SRAM = 0,
	FMC2_ASYNC_MODE_1_PSRAM,
	FMC2_ASYNC_MODE_A_SRAM,
	FMC2_ASYNC_MODE_A_PSRAM,
	FMC2_ASYNC_MODE_2_NOR,
	FMC2_ASYNC_MODE_B_NOR,
	FMC2_ASYNC_MODE_C_NOR,
	FMC2_ASYNC_MODE_D_NOR,
	FMC2_SYNC_READ_SYNC_WRITE_PSRAM,
	FMC2_SYNC_READ_ASYNC_WRITE_PSRAM,
	FMC2_SYNC_READ_SYNC_WRITE_NOR,
	FMC2_SYNC_READ_ASYNC_WRITE_NOR
};

enum stm32_fmc2_ebi_buswidth {
	FMC2_BUSWIDTH_8 = 8,
	FMC2_BUSWIDTH_16 = 16
};

enum stm32_fmc2_ebi_cpsize {
	FMC2_CPSIZE_0 = 0,
	FMC2_CPSIZE_128 = 128,
	FMC2_CPSIZE_256 = 256,
	FMC2_CPSIZE_512 = 512,
	FMC2_CPSIZE_1024 = 1024
};

struct stm32_fmc2_ebi {
	u32 bcr[FMC2_MAX_EBI_CE];
	u32 btr[FMC2_MAX_EBI_CE];
	u32 bwtr[FMC2_MAX_EBI_CE];
	u32 pcscntr;
	u8 cs_assigned;
};

/*
 * struct stm32_fmc2_prop - STM32 FMC2 EBI property
 * @name: the device tree binding name of the property
 * @bprop: indicate that it is a boolean property
 * @mprop: indicate that it is a mandatory property
 * @reg_type: the register that have to be modified
 * @reg_mask: the bit that have to be modified in the selected register
 *            in case of it is a boolean property
 * @reset_val: the default value that have to be set in case the property
 *             has not been defined in the device tree
 * @check: this callback ckecks that the property is compliant with the
 *         transaction type selected
 * @calculate: this callback is called to calculate for exemple a timing
 *             set in ns in the device tree in clock cycles
 * @set: this callback applies the values in the registers
 */
struct stm32_fmc2_prop {
	const char *name;
	bool bprop;
	bool mprop;
	int reg_type;
	u32 reg_mask;
	u32 reset_val;
	int (*check)(struct stm32_fmc2 *fmc2,
		     const struct stm32_fmc2_prop *prop, int cs);
	u32 (*calculate)(struct stm32_fmc2 *fmc2, u32 setup);
	int (*set)(struct stm32_fmc2 *fmc2, const struct stm32_fmc2_prop *prop,
		   int cs, u32 setup);
};

static int stm32_fmc2_ebi_check_mux(struct stm32_fmc2 *fmc2,
				    const struct stm32_fmc2_prop *prop,
				    int cs)
{
	u32 bcr;

	regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);

	if (bcr & FMC2_BCR_MTYP)
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_waitcfg(struct stm32_fmc2 *fmc2,
					const struct stm32_fmc2_prop *prop,
					int cs)
{
	u32 bcr, val = FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);

	regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);

	if ((bcr & FMC2_BCR_MTYP) == val && bcr & FMC2_BCR_BURSTEN)
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_sync_trans(struct stm32_fmc2 *fmc2,
					   const struct stm32_fmc2_prop *prop,
					   int cs)
{
	u32 bcr;

	regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);

	if (bcr & FMC2_BCR_BURSTEN)
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_async_trans(struct stm32_fmc2 *fmc2,
					    const struct stm32_fmc2_prop *prop,
					    int cs)
{
	u32 bcr;

	regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);

	if (!(bcr & FMC2_BCR_BURSTEN) || !(bcr & FMC2_BCR_CBURSTRW))
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_cpsize(struct stm32_fmc2 *fmc2,
				       const struct stm32_fmc2_prop *prop,
				       int cs)
{
	u32 bcr, val = FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);

	regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);

	if ((bcr & FMC2_BCR_MTYP) == val && bcr & FMC2_BCR_BURSTEN)
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_address_hold(struct stm32_fmc2 *fmc2,
					     const struct stm32_fmc2_prop *prop,
					     int cs)
{
	u32 bcr, bxtr, val = FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_D);

	regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);
	if (prop->reg_type == FMC2_REG_BWTR)
		regmap_read(fmc2->regmap, FMC2_BWTR(cs), &bxtr);
	else
		regmap_read(fmc2->regmap, FMC2_BTR(cs), &bxtr);

	if ((!(bcr & FMC2_BCR_BURSTEN) || !(bcr & FMC2_BCR_CBURSTRW)) &&
	    ((bxtr & FMC2_BXTR_ACCMOD) == val || bcr & FMC2_BCR_MUXEN))
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_clk_period(struct stm32_fmc2 *fmc2,
					   const struct stm32_fmc2_prop *prop,
					   int cs)
{
	u32 bcr, bcr1;

	regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);
	if (cs)
		regmap_read(fmc2->regmap, FMC2_BCR(0), &bcr1);
	else
		bcr1 = bcr;

	if (bcr & FMC2_BCR_BURSTEN && (!cs || !(bcr1 & FMC2_BCR1_CCLKEN)))
		return 0;

	return -EINVAL;
}

static int stm32_fmc2_ebi_check_cclk(struct stm32_fmc2 *fmc2,
				     const struct stm32_fmc2_prop *prop,
				     int cs)
{
	if (cs)
		return -EINVAL;

	return stm32_fmc2_ebi_check_sync_trans(fmc2, prop, cs);
}

static u32 stm32_fmc2_ebi_timing_ns_to_clock_cycles(struct stm32_fmc2 *fmc2,
						    u32 setup)
{
	unsigned long hclk = clk_get_rate(fmc2->clk);
	unsigned long hclkp = NSEC_PER_SEC / (hclk / 1000);

	return DIV_ROUND_UP(setup * 1000, hclkp);
}

static int stm32_fmc2_ebi_get_reg(int reg_type, int cs, u32 *reg)
{
	switch (reg_type) {
	case FMC2_REG_BCR:
		*reg = FMC2_BCR(cs);
		break;
	case FMC2_REG_BTR:
		*reg = FMC2_BTR(cs);
		break;
	case FMC2_REG_BWTR:
		*reg = FMC2_BWTR(cs);
		break;
	case FMC2_REG_PCSCNTR:
		*reg = FMC2_PCSCNTR;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int stm32_fmc2_ebi_set_bit_field(struct stm32_fmc2 *fmc2,
					const struct stm32_fmc2_prop *prop,
					int cs, u32 setup)
{
	u32 reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	regmap_update_bits(fmc2->regmap, reg, prop->reg_mask,
			   setup ? prop->reg_mask : 0);

	return 0;
}

static int stm32_fmc2_ebi_set_trans_type(struct stm32_fmc2 *fmc2,
					 const struct stm32_fmc2_prop *prop,
					 int cs, u32 setup)
{
	u32 bcr_mask, bcr = FMC2_BCR_WREN;
	u32 btr_mask, btr = 0;
	u32 bwtr_mask, bwtr = 0;

	bwtr_mask = FMC2_BXTR_ACCMOD;
	btr_mask = FMC2_BXTR_ACCMOD;
	bcr_mask = FMC2_BCR_MUXEN | FMC2_BCR_MTYP | FMC2_BCR_FACCEN |
		   FMC2_BCR_WREN | FMC2_BCR_WAITEN | FMC2_BCR_BURSTEN |
		   FMC2_BCR_EXTMOD | FMC2_BCR_CBURSTRW;

	switch (setup) {
	case FMC2_ASYNC_MODE_1_SRAM:
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_SRAM);
		/*
		 * MUXEN = 0, MTYP = 0, FACCEN = 0, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		break;
	case FMC2_ASYNC_MODE_1_PSRAM:
		/*
		 * MUXEN = 0, MTYP = 1, FACCEN = 0, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
		break;
	case FMC2_ASYNC_MODE_A_SRAM:
		/*
		 * MUXEN = 0, MTYP = 0, FACCEN = 0, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_SRAM);
		bcr |= FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_A);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_A);
		break;
	case FMC2_ASYNC_MODE_A_PSRAM:
		/*
		 * MUXEN = 0, MTYP = 1, FACCEN = 0, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
		bcr |= FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_A);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_A);
		break;
	case FMC2_ASYNC_MODE_2_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN;
		break;
	case FMC2_ASYNC_MODE_B_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 1
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_B);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_B);
		break;
	case FMC2_ASYNC_MODE_C_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 2
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_C);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_C);
		break;
	case FMC2_ASYNC_MODE_D_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 0, WAITEN = 0,
		 * WREN = 1, EXTMOD = 1, CBURSTRW = 0, ACCMOD = 3
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_EXTMOD;
		btr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_D);
		bwtr |= FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_D);
		break;
	case FMC2_SYNC_READ_SYNC_WRITE_PSRAM:
		/*
		 * MUXEN = 0, MTYP = 1, FACCEN = 0, BURSTEN = 1, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 1, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
		bcr |= FMC2_BCR_BURSTEN | FMC2_BCR_CBURSTRW;
		break;
	case FMC2_SYNC_READ_ASYNC_WRITE_PSRAM:
		/*
		 * MUXEN = 0, MTYP = 1, FACCEN = 0, BURSTEN = 1, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_PSRAM);
		bcr |= FMC2_BCR_BURSTEN;
		break;
	case FMC2_SYNC_READ_SYNC_WRITE_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 1, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 1, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_BURSTEN | FMC2_BCR_CBURSTRW;
		break;
	case FMC2_SYNC_READ_ASYNC_WRITE_NOR:
		/*
		 * MUXEN = 0, MTYP = 2, FACCEN = 1, BURSTEN = 1, WAITEN = 0,
		 * WREN = 1, EXTMOD = 0, CBURSTRW = 0, ACCMOD = 0
		 */
		bcr |= FIELD_PREP(FMC2_BCR_MTYP, FMC2_BCR_MTYP_NOR);
		bcr |= FMC2_BCR_FACCEN | FMC2_BCR_BURSTEN;
		break;
	default:
		/* Type of transaction not supported */
		return -EINVAL;
	}

	if (bcr & FMC2_BCR_EXTMOD)
		regmap_update_bits(fmc2->regmap, FMC2_BWTR(cs),
				   bwtr_mask, bwtr);
	regmap_update_bits(fmc2->regmap, FMC2_BTR(cs), btr_mask, btr);
	regmap_update_bits(fmc2->regmap, FMC2_BCR(cs), bcr_mask, bcr);

	return 0;
}

static int stm32_fmc2_ebi_set_buswidth(struct stm32_fmc2 *fmc2,
				       const struct stm32_fmc2_prop *prop,
				       int cs, u32 setup)
{
	u32 val;

	switch (setup) {
	case FMC2_BUSWIDTH_8:
		val = FIELD_PREP(FMC2_BCR_MWID, FMC2_BCR_MWID_8);
		break;
	case FMC2_BUSWIDTH_16:
		val = FIELD_PREP(FMC2_BCR_MWID, FMC2_BCR_MWID_16);
		break;
	default:
		/* Buswidth not supported */
		return -EINVAL;
	}

	regmap_update_bits(fmc2->regmap, FMC2_BCR(cs), FMC2_BCR_MWID, val);

	return 0;
}

static int stm32_fmc2_ebi_set_cpsize(struct stm32_fmc2 *fmc2,
				     const struct stm32_fmc2_prop *prop,
				     int cs, u32 setup)
{
	u32 val;

	switch (setup) {
	case FMC2_CPSIZE_0:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_0);
		break;
	case FMC2_CPSIZE_128:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_128);
		break;
	case FMC2_CPSIZE_256:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_256);
		break;
	case FMC2_CPSIZE_512:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_512);
		break;
	case FMC2_CPSIZE_1024:
		val = FIELD_PREP(FMC2_BCR_CPSIZE, FMC2_BCR_CPSIZE_1024);
		break;
	default:
		/* Cpsize not supported */
		return -EINVAL;
	}

	regmap_update_bits(fmc2->regmap, FMC2_BCR(cs), FMC2_BCR_CPSIZE, val);

	return 0;
}

static int stm32_fmc2_ebi_set_bl_setup(struct stm32_fmc2 *fmc2,
				       const struct stm32_fmc2_prop *prop,
				       int cs, u32 setup)
{
	u32 val;

	val = min_t(u32, setup, FMC2_BCR_NBLSET_MAX);
	val = FIELD_PREP(FMC2_BCR_NBLSET, val);
	regmap_update_bits(fmc2->regmap, FMC2_BCR(cs), FMC2_BCR_NBLSET, val);

	return 0;
}

static int stm32_fmc2_ebi_set_address_setup(struct stm32_fmc2 *fmc2,
					    const struct stm32_fmc2_prop *prop,
					    int cs, u32 setup)
{
	u32 bcr, bxtr, reg;
	u32 val = FIELD_PREP(FMC2_BXTR_ACCMOD, FMC2_BXTR_EXTMOD_D);
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);
	if (prop->reg_type == FMC2_REG_BWTR)
		regmap_read(fmc2->regmap, FMC2_BWTR(cs), &bxtr);
	else
		regmap_read(fmc2->regmap, FMC2_BTR(cs), &bxtr);

	if ((bxtr & FMC2_BXTR_ACCMOD) == val || bcr & FMC2_BCR_MUXEN)
		val = clamp_val(setup, 1, FMC2_BXTR_ADDSET_MAX);
	else
		val = min_t(u32, setup, FMC2_BXTR_ADDSET_MAX);
	val = FIELD_PREP(FMC2_BXTR_ADDSET, val);
	regmap_update_bits(fmc2->regmap, reg, FMC2_BXTR_ADDSET, val);

	return 0;
}

static int stm32_fmc2_ebi_set_address_hold(struct stm32_fmc2 *fmc2,
					   const struct stm32_fmc2_prop *prop,
					   int cs, u32 setup)
{
	u32 val, reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	val = clamp_val(setup, 1, FMC2_BXTR_ADDHLD_MAX);
	val = FIELD_PREP(FMC2_BXTR_ADDHLD, val);
	regmap_update_bits(fmc2->regmap, reg, FMC2_BXTR_ADDHLD, val);

	return 0;
}

static int stm32_fmc2_ebi_set_data_setup(struct stm32_fmc2 *fmc2,
					 const struct stm32_fmc2_prop *prop,
					 int cs, u32 setup)
{
	u32 val, reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	val = clamp_val(setup, 1, FMC2_BXTR_DATAST_MAX);
	val = FIELD_PREP(FMC2_BXTR_DATAST, val);
	regmap_update_bits(fmc2->regmap, reg, FMC2_BXTR_DATAST, val);

	return 0;
}

static int stm32_fmc2_ebi_set_bus_turnaround(struct stm32_fmc2 *fmc2,
					     const struct stm32_fmc2_prop *prop,
					     int cs, u32 setup)
{
	u32 val, reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	val = setup ? min_t(u32, setup - 1, FMC2_BXTR_BUSTURN_MAX) : 0;
	val = FIELD_PREP(FMC2_BXTR_BUSTURN, val);
	regmap_update_bits(fmc2->regmap, reg, FMC2_BXTR_BUSTURN, val);

	return 0;
}

static int stm32_fmc2_ebi_set_data_hold(struct stm32_fmc2 *fmc2,
					const struct stm32_fmc2_prop *prop,
					int cs, u32 setup)
{
	u32 val, reg;
	int ret;

	ret = stm32_fmc2_ebi_get_reg(prop->reg_type, cs, &reg);
	if (ret)
		return ret;

	if (prop->reg_type == FMC2_REG_BWTR)
		val = setup ? min_t(u32, setup - 1, FMC2_BXTR_DATAHLD_MAX) : 0;
	else
		val = min_t(u32, setup, FMC2_BXTR_DATAHLD_MAX);
	val = FIELD_PREP(FMC2_BXTR_DATAHLD, val);
	regmap_update_bits(fmc2->regmap, reg, FMC2_BXTR_DATAHLD, val);

	return 0;
}

static int stm32_fmc2_ebi_set_clk_period(struct stm32_fmc2 *fmc2,
					 const struct stm32_fmc2_prop *prop,
					 int cs, u32 setup)
{
	u32 val;

	val = setup ? clamp_val(setup - 1, 1, FMC2_BTR_CLKDIV_MAX) : 1;
	val = FIELD_PREP(FMC2_BTR_CLKDIV, val);
	regmap_update_bits(fmc2->regmap, FMC2_BTR(cs), FMC2_BTR_CLKDIV, val);

	return 0;
}

static int stm32_fmc2_ebi_set_data_latency(struct stm32_fmc2 *fmc2,
					   const struct stm32_fmc2_prop *prop,
					   int cs, u32 setup)
{
	u32 val;

	val = min_t(u32, setup, FMC2_BTR_DATLAT_MAX);
	val = FIELD_PREP(FMC2_BTR_DATLAT, val);
	regmap_update_bits(fmc2->regmap, FMC2_BTR(cs), FMC2_BTR_DATLAT, val);

	return 0;
}

static int stm32_fmc2_ebi_set_max_low_pulse(struct stm32_fmc2 *fmc2,
					    const struct stm32_fmc2_prop *prop,
					    int cs, u32 setup)
{
	u32 old_val, new_val, pcscntr;

	if (setup < 1)
		return 0;

	regmap_read(fmc2->regmap, FMC2_PCSCNTR, &pcscntr);

	/* Enable counter for the bank */
	regmap_update_bits(fmc2->regmap, FMC2_PCSCNTR,
			   FMC2_PCSCNTR_CNTBEN(cs),
			   FMC2_PCSCNTR_CNTBEN(cs));

	new_val = min_t(u32, setup - 1, FMC2_PCSCNTR_CSCOUNT_MAX);
	old_val = FIELD_GET(FMC2_PCSCNTR_CSCOUNT, pcscntr);
	if (old_val && new_val > old_val)
		/* Keep current counter value */
		return 0;

	new_val = FIELD_PREP(FMC2_PCSCNTR_CSCOUNT, new_val);
	regmap_update_bits(fmc2->regmap, FMC2_PCSCNTR,
			   FMC2_PCSCNTR_CSCOUNT, new_val);

	return 0;
}

static const struct stm32_fmc2_prop stm32_fmc2_child_props[] = {
	/* st,fmc2_ebi_cs_trans_type must be the first property */
	{
		.name = "st,fmc2_ebi_cs_transaction_type",
		.mprop = true,
		.set = stm32_fmc2_ebi_set_trans_type,
	},
	{
		.name = "st,fmc2_ebi_cs_cclk_enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR1_CCLKEN,
		.check = stm32_fmc2_ebi_check_cclk,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2_ebi_cs_mux_enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_MUXEN,
		.check = stm32_fmc2_ebi_check_mux,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2_ebi_cs_buswidth",
		.reset_val = FMC2_BUSWIDTH_16,
		.set = stm32_fmc2_ebi_set_buswidth,
	},
	{
		.name = "st,fmc2_ebi_cs_waitpol_high",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITPOL,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2_ebi_cs_waitcfg_enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITCFG,
		.check = stm32_fmc2_ebi_check_waitcfg,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2_ebi_cs_wait_enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_WAITEN,
		.check = stm32_fmc2_ebi_check_sync_trans,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2_ebi_cs_asyncwait_enable",
		.bprop = true,
		.reg_type = FMC2_REG_BCR,
		.reg_mask = FMC2_BCR_ASYNCWAIT,
		.check = stm32_fmc2_ebi_check_async_trans,
		.set = stm32_fmc2_ebi_set_bit_field,
	},
	{
		.name = "st,fmc2_ebi_cs_cpsize",
		.check = stm32_fmc2_ebi_check_cpsize,
		.set = stm32_fmc2_ebi_set_cpsize,
	},
	{
		.name = "st,fmc2_ebi_cs_byte_lane_setup",
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bl_setup,
	},
	{
		.name = "st,fmc2_ebi_cs_address_setup",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_ADDSET_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_setup,
	},
	{
		.name = "st,fmc2_ebi_cs_address_hold",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_ADDHLD_MAX,
		.check = stm32_fmc2_ebi_check_address_hold,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_hold,
	},
	{
		.name = "st,fmc2_ebi_cs_data_setup",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_DATAST_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_setup,
	},
	{
		.name = "st,fmc2_ebi_cs_bus_turnaround",
		.reg_type = FMC2_REG_BTR,
		.reset_val = FMC2_BXTR_BUSTURN_MAX + 1,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bus_turnaround,
	},
	{
		.name = "st,fmc2_ebi_cs_data_hold",
		.reg_type = FMC2_REG_BTR,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_hold,
	},
	{
		.name = "st,fmc2_ebi_cs_clk_period",
		.reset_val = FMC2_BTR_CLKDIV_MAX + 1,
		.check = stm32_fmc2_ebi_check_clk_period,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_clk_period,
	},
	{
		.name = "st,fmc2_ebi_cs_data_latency",
		.check = stm32_fmc2_ebi_check_sync_trans,
		.set = stm32_fmc2_ebi_set_data_latency,
	},
	{
		.name = "st,fmc2_ebi_cs_write_address_setup",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_ADDSET_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_setup,
	},
	{
		.name = "st,fmc2_ebi_cs_write_address_hold",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_ADDHLD_MAX,
		.check = stm32_fmc2_ebi_check_address_hold,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_address_hold,
	},
	{
		.name = "st,fmc2_ebi_cs_write_data_setup",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_DATAST_MAX,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_setup,
	},
	{
		.name = "st,fmc2_ebi_cs_write_bus_turnaround",
		.reg_type = FMC2_REG_BWTR,
		.reset_val = FMC2_BXTR_BUSTURN_MAX + 1,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_bus_turnaround,
	},
	{
		.name = "st,fmc2_ebi_cs_write_data_hold",
		.reg_type = FMC2_REG_BWTR,
		.check = stm32_fmc2_ebi_check_async_trans,
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_data_hold,
	},
	{
		.name = "st,fmc2_ebi_cs_max_low_pulse",
		.calculate = stm32_fmc2_ebi_timing_ns_to_clock_cycles,
		.set = stm32_fmc2_ebi_set_max_low_pulse,
	},
};

static void stm32_fmc2_ebi_set_bank(struct stm32_fmc2 *fmc2,
				    int cs, bool enable)
{
	regmap_update_bits(fmc2->regmap, FMC2_BCR(cs), FMC2_BCR_MBKEN,
			   enable ? FMC2_BCR_MBKEN : 0);
}

static int stm32_fmc2_ebi_parse_prop(struct device *dev,
				     struct device_node *dev_node,
				     const struct stm32_fmc2_prop *prop,
				     int cs)
{
	struct stm32_fmc2 *fmc2 = dev_get_drvdata(dev->parent);
	u32 setup = 0;

	if (!prop->set) {
		dev_err(dev, "property %s is not well defined\n", prop->name);
		return -EINVAL;
	}

	if (prop->check && prop->check(fmc2, prop, cs))
		/* Skeep this property */
		return 0;

	if (prop->bprop) {
		const __be32 *bprop = of_get_property(dev_node,
						      prop->name, NULL);

		if (prop->mprop && !bprop) {
			dev_err(dev, "mandatory property %s not defined in the device tree\n",
				prop->name);
			return -EINVAL;
		}

		if (bprop)
			setup = 1;
	} else {
		u32 val;
		int ret;

		ret = of_property_read_u32(dev_node, prop->name, &val);
		if (prop->mprop && ret) {
			dev_err(dev, "mandatory property %s not defined in the device tree\n",
				prop->name);
			return ret;
		}

		if (ret)
			setup = prop->reset_val;
		else if (prop->calculate)
			setup = prop->calculate(fmc2, val);
		else
			setup = val;
	}

	return prop->set(fmc2, prop, cs, setup);
}

static int stm32_fmc2_ebi_setup_cs(struct device *dev,
				   struct device_node *dev_node,
				   u32 cs)
{
	struct stm32_fmc2 *fmc2 = dev_get_drvdata(dev->parent);
	int i, ret;

	/* Disable the bank */
	stm32_fmc2_ebi_set_bank(fmc2, cs, false);

	for (i = 0; i < ARRAY_SIZE(stm32_fmc2_child_props); i++) {
		const struct stm32_fmc2_prop *p = &stm32_fmc2_child_props[i];

		ret = stm32_fmc2_ebi_parse_prop(dev, dev_node, p, cs);
		if (ret) {
			dev_err(dev, "property %s could not be set: %d\n",
				p->name, ret);
			return ret;
		}
	}

	/* Enable the bank */
	stm32_fmc2_ebi_set_bank(fmc2, cs, true);

	return 0;
}

static int stm32_fmc2_ebi_parse_dt(struct device *dev,
				   struct stm32_fmc2_ebi *ebi)
{
	struct device_node *child;
	bool child_found = false;
	u32 cs;
	int ret;

	for_each_available_child_of_node(dev->of_node, child) {
		ret = of_property_read_u32(child, "reg", &cs);
		if (ret) {
			dev_err(dev, "could not retrieve reg property: %d\n",
				ret);
			return ret;
		}

		if (cs > FMC2_MAX_EBI_CE) {
			dev_err(dev, "invalid reg value: %d\n", cs);
			return -EINVAL;
		}

		if (ebi->cs_assigned & BIT(cs)) {
			dev_err(dev, "cs already assigned: %d\n", cs);
			return -EINVAL;
		}

		ret = stm32_fmc2_ebi_setup_cs(dev, child, cs);
		if (ret) {
			dev_err(dev, "setup chip select %d failed: %d\n",
				cs, ret);
			return ret;
		}

		ebi->cs_assigned |= BIT(cs);
		child_found = true;
	}

	if (!child_found) {
		dev_warn(dev, "no subnodes found, disable the driver.\n");
		return -ENODEV;
	}

	return of_platform_default_populate(dev->of_node, NULL, dev);
}

static void stm32_fmc2_ebi_save_setup(struct stm32_fmc2 *fmc2,
				      struct stm32_fmc2_ebi *ebi)
{
	unsigned int cs;

	for (cs = 0; cs < FMC2_MAX_EBI_CE; cs++) {
		regmap_read(fmc2->regmap, FMC2_BCR(cs), &ebi->bcr[cs]);
		regmap_read(fmc2->regmap, FMC2_BTR(cs), &ebi->btr[cs]);
		regmap_read(fmc2->regmap, FMC2_BWTR(cs), &ebi->bwtr[cs]);
	}

	regmap_read(fmc2->regmap, FMC2_PCSCNTR, &ebi->pcscntr);
}

static void stm32_fmc2_ebi_set_setup(struct stm32_fmc2 *fmc2,
				     struct stm32_fmc2_ebi *ebi)
{
	unsigned int cs;

	for (cs = 0; cs < FMC2_MAX_EBI_CE; cs++) {
		regmap_write(fmc2->regmap, FMC2_BCR(cs), ebi->bcr[cs]);
		regmap_write(fmc2->regmap, FMC2_BTR(cs), ebi->btr[cs]);
		regmap_write(fmc2->regmap, FMC2_BWTR(cs), ebi->bwtr[cs]);
	}

	regmap_write(fmc2->regmap, FMC2_PCSCNTR, ebi->pcscntr);
}

static bool stm32_fmc2_ebi_check_nwait_is_used(struct stm32_fmc2 *fmc2,
					       struct stm32_fmc2_ebi *ebi)
{
	unsigned int cs;
	u32 bcr;

	for (cs = 0; cs < FMC2_MAX_EBI_CE; cs++) {
		if (!(ebi->cs_assigned & BIT(cs)))
			continue;

		regmap_read(fmc2->regmap, FMC2_BCR(cs), &bcr);
		if (bcr & FMC2_BCR_WAITEN || bcr & FMC2_BCR_ASYNCWAIT)
			return true;
	}

	return false;
}

static void stm32_fmc2_ebi_disable_banks(struct stm32_fmc2 *fmc2,
					 struct stm32_fmc2_ebi *ebi)
{
	unsigned int cs;

	for (cs = 0; cs < FMC2_MAX_EBI_CE; cs++) {
		if (!(ebi->cs_assigned & BIT(cs)))
			continue;

		stm32_fmc2_ebi_set_bank(fmc2, cs, false);
	}
}

static int stm32_fmc2_ebi_probe(struct platform_device *pdev)
{
	struct stm32_fmc2 *fmc2 = dev_get_drvdata(pdev->dev.parent);
	struct stm32_fmc2_ebi *ebi;
	int ret;

	ebi = devm_kzalloc(&pdev->dev, sizeof(*ebi), GFP_KERNEL);
	if (!ebi)
		return -ENOMEM;

	ret = clk_prepare_enable(fmc2->clk);
	if (ret)
		return ret;

	ret = stm32_fmc2_ebi_parse_dt(&pdev->dev, ebi);
	if (ret)
		goto out;

	if (stm32_fmc2_ebi_check_nwait_is_used(fmc2, ebi) &&
	    atomic_inc_return(&fmc2->nwait_is_used) > 1) {
		dev_err(&pdev->dev,
			"NWAIT signal in used by a FMC controller\n");
		atomic_dec(&fmc2->nwait_is_used);
		ret = -EINVAL;
		goto out;
	}

	stm32_fmc2_ebi_save_setup(fmc2, ebi);
	fmc2->enable(fmc2);
	platform_set_drvdata(pdev, ebi);

	return 0;

out:
	stm32_fmc2_ebi_disable_banks(fmc2, ebi);
	clk_disable_unprepare(fmc2->clk);

	return ret;
}

static int stm32_fmc2_ebi_remove(struct platform_device *pdev)
{
	struct stm32_fmc2 *fmc2 = dev_get_drvdata(pdev->dev.parent);
	struct stm32_fmc2_ebi *ebi = platform_get_drvdata(pdev);

	if (stm32_fmc2_ebi_check_nwait_is_used(fmc2, ebi))
		atomic_dec(&fmc2->nwait_is_used);
	stm32_fmc2_ebi_disable_banks(fmc2, ebi);
	fmc2->disable(fmc2);
	clk_disable_unprepare(fmc2->clk);

	return 0;
}

static int __maybe_unused stm32_fmc2_ebi_suspend(struct device *dev)
{
	struct stm32_fmc2 *fmc2 = dev_get_drvdata(dev->parent);

	fmc2->disable(fmc2);
	clk_disable_unprepare(fmc2->clk);

	return 0;
}

static int __maybe_unused stm32_fmc2_ebi_resume(struct device *dev)
{
	struct stm32_fmc2 *fmc2 = dev_get_drvdata(dev->parent);
	struct stm32_fmc2_ebi *ebi = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(fmc2->clk);
	if (ret)
		return ret;

	stm32_fmc2_ebi_set_setup(fmc2, ebi);
	fmc2->enable(fmc2);

	return 0;
}

static SIMPLE_DEV_PM_OPS(stm32_fmc2_ebi_pm_ops, stm32_fmc2_ebi_suspend,
			 stm32_fmc2_ebi_resume);

static const struct of_device_id stm32_fmc2_ebi_match[] = {
	{.compatible = "st,stm32mp1-fmc2-ebi"},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_fmc2_ebi_match);

static struct platform_driver stm32_fmc2_ebi_driver = {
	.probe	= stm32_fmc2_ebi_probe,
	.remove	= stm32_fmc2_ebi_remove,
	.driver	= {
		.name = "stm32_fmc2_ebi",
		.of_match_table = stm32_fmc2_ebi_match,
		.pm = &stm32_fmc2_ebi_pm_ops,
	},
};
module_platform_driver(stm32_fmc2_ebi_driver);

MODULE_ALIAS("platform:stm32_fmc2_ebi");
MODULE_AUTHOR("Christophe Kerello <christophe.kerello@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 FMC2 ebi driver");
MODULE_LICENSE("GPL v2");
