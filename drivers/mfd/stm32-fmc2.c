// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2020
 */

#include <linux/mfd/stm32-fmc2.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pinctrl/consumer.h>
#include <linux/reset.h>

/* Regmap registers configuration */
#define FMC2_MAX_REGISTER		0x3fc

static const struct regmap_config stm32_fmc2_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = sizeof(u32),
	.max_register = FMC2_MAX_REGISTER,
};

static void stm32_fmc2_enable(struct stm32_fmc2 *fmc2)
{
	if (atomic_inc_return(&fmc2->nb_ctrl_used) == 1)
		regmap_update_bits(fmc2->regmap, FMC2_BCR1,
				   FMC2_BCR1_FMC2EN, FMC2_BCR1_FMC2EN);
}

static void stm32_fmc2_disable(struct stm32_fmc2 *fmc2)
{
	if (atomic_dec_and_test(&fmc2->nb_ctrl_used))
		regmap_update_bits(fmc2->regmap, FMC2_BCR1,
				   FMC2_BCR1_FMC2EN, 0);
}

static int stm32_fmc2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reset_control *rstc;
	struct stm32_fmc2 *fmc2;
	struct resource *res;
	void __iomem *mmio;
	int ret;

	fmc2 = devm_kzalloc(dev, sizeof(*fmc2), GFP_KERNEL);
	if (!fmc2)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(mmio))
		return PTR_ERR(mmio);

	fmc2->regmap = devm_regmap_init_mmio(dev, mmio,
					     &stm32_fmc2_regmap_cfg);
	if (IS_ERR(fmc2->regmap))
		return PTR_ERR(fmc2->regmap);

	fmc2->reg_phys_addr = res->start;

	fmc2->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(fmc2->clk))
		return PTR_ERR(fmc2->clk);

	rstc = devm_reset_control_get(dev, NULL);
	if (PTR_ERR(rstc) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	ret = clk_prepare_enable(fmc2->clk);
	if (ret)
		return ret;

	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		reset_control_deassert(rstc);
	}

	fmc2->enable = stm32_fmc2_enable;
	fmc2->disable = stm32_fmc2_disable;

	platform_set_drvdata(pdev, fmc2);

	clk_disable_unprepare(fmc2->clk);

	return devm_of_platform_populate(dev);
}

static int __maybe_unused stm32_fmc2_suspend(struct device *dev)
{
	return pinctrl_pm_select_sleep_state(dev);
}

static int __maybe_unused stm32_fmc2_resume(struct device *dev)
{
	return pinctrl_pm_select_default_state(dev);
}

static SIMPLE_DEV_PM_OPS(stm32_fmc2_pm_ops, stm32_fmc2_suspend,
			 stm32_fmc2_resume);

static const struct of_device_id stm32_fmc2_match[] = {
	{.compatible = "st,stm32mp15-fmc2"},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_fmc2_match);

static struct platform_driver stm32_fmc2_driver = {
	.probe	= stm32_fmc2_probe,
	.driver	= {
		.name = "stm32_fmc2",
		.of_match_table = stm32_fmc2_match,
		.pm = &stm32_fmc2_pm_ops,
	},
};
module_platform_driver(stm32_fmc2_driver);

MODULE_ALIAS("platform:stm32_fmc2");
MODULE_AUTHOR("Christophe Kerello <christophe.kerello@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 FMC2 driver");
MODULE_LICENSE("GPL v2");
