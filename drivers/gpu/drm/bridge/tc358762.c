// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Marek Vasut <marex@denx.de>
 *
 * Based on tc358764.c by
 *  Andrzej Hajda <a.hajda@samsung.com>
 *  Maciej Purski <m.purski@samsung.com>
 *
 * Based on rpi_touchscreen.c by
 *  Eric Anholt <eric@anholt.net>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

/* PPI layer registers */
#define PPI_STARTPPI		0x0104 /* START control bit */
#define PPI_LPTXTIMECNT		0x0114 /* LPTX timing signal */
#define PPI_D0S_ATMR		0x0144
#define PPI_D1S_ATMR		0x0148
#define PPI_D0S_CLRSIPOCOUNT	0x0164 /* Assertion timer for Lane 0 */
#define PPI_D1S_CLRSIPOCOUNT	0x0168 /* Assertion timer for Lane 1 */
#define PPI_START_FUNCTION	1

/* DSI layer registers */
#define DSI_STARTDSI		0x0204 /* START control bit of DSI-TX */
#define DSI_LANEENABLE		0x0210 /* Enables each lane */
#define DSI_RX_START		1

/* LCDC/DPI Host Registers */
#define LCDCTRL			0x0420

/* SPI Master Registers */
#define SPICMR			0x0450
#define SPITCR			0x0454

/* System Controller Registers */
#define SYSCTRL			0x0464

/* System registers */
#define LPX_PERIOD		3

/* Lane enable PPI and DSI register bits */
#define LANEENABLE_CLEN		BIT(0)
#define LANEENABLE_L0EN		BIT(1)
#define LANEENABLE_L1EN		BIT(2)

struct tc358762 {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct regulator *regulator;
	struct drm_panel *panel;
	int error;
};

static int tc358762_clear_error(struct tc358762 *ctx)
{
	int ret = ctx->error;

	ctx->error = 0;
	return ret;
}

static void tc358762_write(struct tc358762 *ctx, u16 addr, u32 val)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	u8 data[6];

	if (ctx->error)
		return;

	data[0] = addr;
	data[1] = addr >> 8;
	data[2] = val;
	data[3] = val >> 8;
	data[4] = val >> 16;
	data[5] = val >> 24;

	ret = mipi_dsi_generic_write(dsi, data, sizeof(data));
	if (ret < 0)
		ctx->error = ret;
}

static inline struct tc358762 *bridge_to_tc358762(struct drm_bridge *bridge)
{
	return container_of(bridge, struct tc358762, bridge);
}

static inline
struct tc358762 *connector_to_tc358762(struct drm_connector *connector)
{
	return container_of(connector, struct tc358762, connector);
}

static int tc358762_init(struct tc358762 *ctx)
{
	tc358762_write(ctx, DSI_LANEENABLE,
		       LANEENABLE_L0EN | LANEENABLE_CLEN);
	tc358762_write(ctx, PPI_D0S_CLRSIPOCOUNT, 5);
	tc358762_write(ctx, PPI_D1S_CLRSIPOCOUNT, 5);
	tc358762_write(ctx, PPI_D0S_ATMR, 0);
	tc358762_write(ctx, PPI_D1S_ATMR, 0);
	tc358762_write(ctx, PPI_LPTXTIMECNT, LPX_PERIOD);

	tc358762_write(ctx, SPICMR, 0x00);
	tc358762_write(ctx, LCDCTRL, 0x00100150);
	tc358762_write(ctx, SYSCTRL, 0x040f);
	msleep(100);

	tc358762_write(ctx, PPI_STARTPPI, PPI_START_FUNCTION);
	tc358762_write(ctx, DSI_STARTDSI, DSI_RX_START);

	msleep(100);

	return tc358762_clear_error(ctx);
}

static int tc358762_get_modes(struct drm_connector *connector)
{
	struct tc358762 *ctx = connector_to_tc358762(connector);

	return drm_panel_get_modes(ctx->panel);
}

static const
struct drm_connector_helper_funcs tc358762_connector_helper_funcs = {
	.get_modes = tc358762_get_modes,
};

static const struct drm_connector_funcs tc358762_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void tc358762_disable(struct drm_bridge *bridge)
{
	struct tc358762 *ctx = bridge_to_tc358762(bridge);
	int ret = drm_panel_disable(bridge_to_tc358762(bridge)->panel);

	if (ret < 0)
		dev_err(ctx->dev, "error disabling panel (%d)\n", ret);
}

static void tc358762_post_disable(struct drm_bridge *bridge)
{
	struct tc358762 *ctx = bridge_to_tc358762(bridge);
	int ret;

	ret = drm_panel_unprepare(ctx->panel);
	if (ret < 0)
		dev_err(ctx->dev, "error unpreparing panel (%d)\n", ret);

	ret = regulator_disable(ctx->regulator);
	if (ret < 0)
		dev_err(ctx->dev, "error disabling regulators (%d)\n", ret);
}

static void tc358762_pre_enable(struct drm_bridge *bridge)
{
	struct tc358762 *ctx = bridge_to_tc358762(bridge);
	int ret;

	ret = regulator_enable(ctx->regulator);
	if (ret < 0)
		dev_err(ctx->dev, "error enabling regulators (%d)\n", ret);

	ret = tc358762_init(ctx);
	if (ret < 0)
		dev_err(ctx->dev, "error initializing bridge (%d)\n", ret);

	ret = drm_panel_prepare(ctx->panel);
	if (ret < 0)
		dev_err(ctx->dev, "error preparing panel (%d)\n", ret);
}

static void tc358762_enable(struct drm_bridge *bridge)
{
	struct tc358762 *ctx = bridge_to_tc358762(bridge);
	int ret = drm_panel_enable(ctx->panel);

	if (ret < 0)
		dev_err(ctx->dev, "error enabling panel (%d)\n", ret);
}

static int tc358762_attach(struct drm_bridge *bridge)
{
	struct tc358762 *ctx = bridge_to_tc358762(bridge);
	struct drm_device *drm = bridge->dev;
	int ret;

	ret = drm_connector_init(drm, &ctx->connector,
				 &tc358762_connector_funcs,
				 DRM_MODE_CONNECTOR_DPI);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_connector_helper_add(&ctx->connector,
				 &tc358762_connector_helper_funcs);
	drm_connector_attach_encoder(&ctx->connector, bridge->encoder);
	drm_panel_attach(ctx->panel, &ctx->connector);
	ctx->connector.funcs->reset(&ctx->connector);
	drm_fb_helper_add_one_connector(drm->fb_helper, &ctx->connector);
	drm_connector_register(&ctx->connector);

	return 0;
}

static void tc358762_detach(struct drm_bridge *bridge)
{
	struct tc358762 *ctx = bridge_to_tc358762(bridge);
	struct drm_device *drm = bridge->dev;

	drm_connector_unregister(&ctx->connector);
	drm_fb_helper_remove_one_connector(drm->fb_helper, &ctx->connector);
	drm_panel_detach(ctx->panel);
	ctx->panel = NULL;
	drm_connector_put(&ctx->connector);
}

static const struct drm_bridge_funcs tc358762_bridge_funcs = {
	.disable = tc358762_disable,
	.post_disable = tc358762_post_disable,
	.enable = tc358762_enable,
	.pre_enable = tc358762_pre_enable,
	.attach = tc358762_attach,
	.detach = tc358762_detach,
};

static int tc358762_parse_dt(struct tc358762 *ctx)
{
	struct device *dev = ctx->dev;
	int ret;

	ret = drm_of_find_panel_or_bridge(ctx->dev->of_node, 1, 0, &ctx->panel,
					  NULL);
	if (ret && ret != -EPROBE_DEFER)
		dev_err(dev, "cannot find panel (%d)\n", ret);

	return ret;
}

static int tc358762_configure_regulators(struct tc358762 *ctx)
{
	ctx->regulator = devm_regulator_get(ctx->dev, "vddc");
	if (IS_ERR(ctx->regulator))
		return PTR_ERR(ctx->regulator);

	return 0;
}

static int tc358762_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct tc358762 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(struct tc358762), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;

	/* TODO: Find out how to get dual-lane mode working */
	dsi->lanes = 1;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_LPM;

	ret = tc358762_parse_dt(ctx);
	if (ret < 0)
		return ret;

	ret = tc358762_configure_regulators(ctx);
	if (ret < 0)
		return ret;

	ctx->bridge.funcs = &tc358762_bridge_funcs;
	ctx->bridge.of_node = dev->of_node;

	drm_bridge_add(&ctx->bridge);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_bridge_remove(&ctx->bridge);
		dev_err(dev, "failed to attach dsi\n");
	}

	return ret;
}

static int tc358762_remove(struct mipi_dsi_device *dsi)
{
	struct tc358762 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_bridge_remove(&ctx->bridge);

	return 0;
}

static const struct of_device_id tc358762_of_match[] = {
	{ .compatible = "toshiba,tc358762" },
	{ }
};
MODULE_DEVICE_TABLE(of, tc358762_of_match);

static struct mipi_dsi_driver tc358762_driver = {
	.probe = tc358762_probe,
	.remove = tc358762_remove,
	.driver = {
		.name = "tc358762",
		.of_match_table = tc358762_of_match,
	},
};
module_mipi_dsi_driver(tc358762_driver);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_DESCRIPTION("MIPI-DSI based Driver for TC358762 DSI/DPI Bridge");
MODULE_LICENSE("GPL v2");
