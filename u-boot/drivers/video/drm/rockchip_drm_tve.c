/*
 * (C) Copyright 2008-2015 Fuzhou Rockchip Electronics Co., Ltd
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */
#include <asm/arch/rkplat.h>
#include <dm/device.h>
#include <linux/fb.h>
#include <linux/media-bus-format.h>
#include <common.h>
#include <edid.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <lcd.h>
#include <../board/rockchip/common/config.h>
#include "rockchip_drm_tve.h"
#include "../rockchip_display.h"
#include "../rockchip_crtc.h"
#include "../rockchip_connector.h"
#include "../rockchip_phy.h"

DECLARE_GLOBAL_DATA_PTR;

static struct drm_tve tve_s;

#define tve_writel(offset, v)	writel(v, tve_s.reg_phy_base  + offset)
#define tve_readl(offset)	readl(tve_s.reg_phy_base + offset)

#define tve_dac_writel(offset, v)   writel(v, tve_s.vdacbase + offset)
#define tve_dac_readl(offset)	readl(tve_s.vdacbase + offset)

#define RK322X_VDAC_STANDARD 0x15

#ifdef CONFIG_RK_EFUSE
extern int32 FtEfuseRead(void *base, void *buff, uint32 addr, uint32 size);
#endif

#define TVE_REG_NUM 0x28

static const struct drm_display_mode tve_modes[] = {
	/* 0 - 720x576i@50Hz */
	{ DRM_MODE(DRM_MODE_TYPE_DRIVER, 13500, 720, 753,
		   816, 864, 576, 580, 586, 625, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
	  .vrefresh = 50, },
	/* 1 - 720x480i@60Hz */
	{ DRM_MODE(DRM_MODE_TYPE_DRIVER, 13500, 720, 753,
		   815, 858, 480, 480, 486, 525, 0,
		   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC |
		   DRM_MODE_FLAG_INTERLACE | DRM_MODE_FLAG_DBLCLK),
	  .vrefresh = 60, },
};

int drm_tve_show_reg(void)
{
	int i = 0;
	u32 val = 0;

	printf("\n>>>drm tve reg");

	printf("\n\n\n\n--------------------------%s------------------------------------",__FUNCTION__);

	for (i = 0; i <= TVE_REG_NUM; i++) {
		val = readl(tve_s.reg_phy_base + i * 0x04);
		if (i % 4 == 0)
			printf("\n%8x:", i*0x04 + tve_s.reg_phy_base - 0x200);
		printf(" %08x", val);
	}
	printf("\n-----------------------------------------------------------------\n");

	return 0;
}


static void dac_enable(int enable)
{
	u32 mask, val;
	u32 grfreg = 0;

#if defined(CONFIG_RKCHIP_RK322X) || defined(CONFIG_RKCHIP_RK322XH)
	tve_dac_writel(VDAC_VDAC2, v_CUR_CTR(tve_s.daclevel));
	tve_dac_writel(VDAC_VDAC3, v_CAB_EN(0));
#endif
	if (enable) {
		mask = m_VBG_EN | m_DAC_EN | m_DAC_GAIN;
		#if defined(CONFIG_RKCHIP_RK3128)
			val = m_VBG_EN | m_DAC_EN | v_DAC_GAIN(tve_s.daclevel);
			grfreg = GRF_TVE_CON0;
		#elif defined(CONFIG_RKCHIP_RK3036)
			val = m_VBG_EN | m_DAC_EN | v_DAC_GAIN(tve_s.daclevel);
			grfreg = GRF_SOC_CON3;
		#endif
		val |= mask << 16;

		#if defined(CONFIG_RKCHIP_RK322X) || defined(CONFIG_RKCHIP_RK322XH)
			val = v_CUR_REG(tve_s.dac1level) | v_DR_PWR_DOWN(0) | v_BG_PWR_DOWN(0);
		#endif
	} else {
		mask = m_VBG_EN | m_DAC_EN;
		val = 0;
		#if defined(CONFIG_RKCHIP_RK3128)
			grfreg = GRF_TVE_CON0;
		#elif defined(CONFIG_RKCHIP_RK3036)
			grfreg = GRF_SOC_CON3;
		#endif
		val |= mask << 16;

		#if defined(CONFIG_RKCHIP_RK322X) || defined(CONFIG_RKCHIP_RK322XH)
			val = v_CUR_REG(tve_s.dac1level) | m_DR_PWR_DOWN | m_BG_PWR_DOWN;
		#endif
	}

	if (grfreg)
		writel(val, RKIO_GRF_PHYS + grfreg);
	else if (tve_s.vdacbase)
		tve_dac_writel(VDAC_VDAC1, val);
}

static void tve_set_mode(int mode)
{
	if (tve_s.soctype != SOC_RK322X &&
	    tve_s.soctype != SOC_RK322XH) {
		tve_writel(TV_RESET, v_RESET(1));
		udelay(100);
		tve_writel(TV_RESET, v_RESET(0));
	}

	if (tve_s.soctype == SOC_RK3036)
		tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) |
			   v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) |
			   v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(0));
	else
		tve_writel(TV_CTRL, v_CVBS_MODE(mode) | v_CLK_UPSTREAM_EN(2) |
			   v_TIMING_EN(2) | v_LUMA_FILTER_GAIN(0) |
			   v_LUMA_FILTER_UPSAMPLE(1) | v_CSC_PATH(3));

	tve_writel(TV_LUMA_FILTER0, tve_s.lumafilter0);
	tve_writel(TV_LUMA_FILTER1, tve_s.lumafilter1);
	tve_writel(TV_LUMA_FILTER2, tve_s.lumafilter2);

	if(mode == TVOUT_CVBS_NTSC) {
		tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(1) |
			v_Y_AGC_PULSE_ON(1) | v_Y_VIDEO_ON(1) |
			v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
		tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_NTSC) | v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));
		tve_writel(TV_SATURATION, 0x0052543C);
		if(tve_s.test_mode)
			tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00008300);
		else
			tve_writel(TV_BRIGHTNESS_CONTRAST, 0x00007900);

		tve_writel(TV_FREQ_SC,	0x21F07BD7);
		tve_writel(TV_SYNC_TIMING, 0x00C07a81);
		tve_writel(TV_ADJ_TIMING, 0x96B40000);
		tve_writel(TV_ACT_ST,	0x001500D6);
		tve_writel(TV_ACT_TIMING, 0x169800FC | (1 << 12) | (1 << 28));

	} else if (mode == TVOUT_CVBS_PAL) {
		tve_writel(TV_ROUTING, v_DAC_SENSE_EN(0) | v_Y_IRE_7_5(0) |
			v_Y_AGC_PULSE_ON(0) | v_Y_VIDEO_ON(1) |
			v_YPP_MODE(1) | v_Y_SYNC_ON(1) | v_PIC_MODE(mode));
		tve_writel(TV_BW_CTRL, v_CHROMA_BW(BP_FILTER_PAL) |
			v_COLOR_DIFF_BW(COLOR_DIFF_FILTER_BW_1_3));

		tve_writel(TV_SATURATION, tve_s.saturation);
		tve_writel(TV_BRIGHTNESS_CONTRAST, tve_s.brightcontrast);

		tve_writel(TV_FREQ_SC,	0x2A098ACB);
		tve_writel(TV_SYNC_TIMING, 0x00C28381);
		tve_writel(TV_ADJ_TIMING, (0xc << 28) | 0x06c00800 | 0x80);
		tve_writel(TV_ACT_ST,	0x001500F6);
		tve_writel(TV_ACT_TIMING, 0x0694011D | (1 << 12) | (2 << 28));

		tve_writel(TV_ADJ_TIMING, tve_s.adjtiming);
		tve_writel(TV_ACT_TIMING, 0x0694011D | (1 << 12) | (2 << 28));
	}
}

static uint8 rk_get_vdac_value(void)
{
	uint8 value = 0;

#ifdef CONFIG_RK_EFUSE
#if defined(CONFIG_RKCHIP_RK322X)
	FtEfuseRead((void *)(unsigned long)RKIO_EFUSE_256BITS_PHYS, &value, 29, 1);
	value = (value >> 3) & 0x1f;
#endif
#endif /* CONFIG_RK_EFUSE */
	if (value > 0)
		value += 5;
	TVEDBG("%s value = 0x%x\n", __func__, value);

	return value;
}

static int rockchip_drm_tve_init(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	int node = 0;
	int dac_value, getvdac;

	conn_state->type = DRM_MODE_CONNECTOR_TV;

#if defined(CONFIG_RKCHIP_RK3036)
	tve_s.reg_phy_base = 0x10118000 + 0x200;
	tve_s.soctype = SOC_RK3036;
#elif defined(CONFIG_RKCHIP_RK3128)
	tve_s.reg_phy_base = 0x1010e000 + 0x200;
	tve_s.soctype = SOC_RK312X;
	tve_s.saturation = 0;
#elif defined(CONFIG_RKCHIP_RK322X)
	tve_s.reg_phy_base = 0x20050000 + 0x3e00;
	tve_s.soctype = SOC_RK322X;
	tve_s.saturation = 0;
	tve_s.vdacbase = 0x12020000;
#elif defined(CONFIG_RKCHIP_RK322XH)
	tve_s.reg_phy_base = 0xff370000 + 0x3e00;
	tve_s.soctype = SOC_RK322XH;
	tve_s.vdacbase = 0xff420000;
#endif

	if (gd->fdt_blob)
	{
		node = fdt_node_offset_by_compatible(gd->fdt_blob,
			0, "rockchip,rk312x-tve");
		if (node < 0) {
			printf("can't find dts node for rk312x-tve\n");
			node = fdt_node_offset_by_compatible(gd->fdt_blob, 0, "rockchip,rk322x-tve");
			if (node < 0) {
				printf("can't find dts node for rk322x-tve\n");
				node = fdt_node_offset_by_compatible(gd->fdt_blob, 0, "rockchip,rk3328-tve");
				if (node < 0) {
					printf("can't find dts node for rk322xh-tve\n");
					goto err;
				}
			}
		}

		if (!fdt_device_is_available(gd->fdt_blob, node)) {
			printf("rk312x-tve is disabled\n");
			goto err;
		}

		if (tve_s.soctype == SOC_RK312X)
			tve_s.test_mode = fdtdec_get_int(gd->fdt_blob, node, "test_mode", 0);

		tve_s.preferred_mode = fdtdec_get_int(gd->fdt_blob, node, "rockchip,tvemode", -1);
		if (tve_s.preferred_mode < 0) {
			tve_s.preferred_mode = 0;
		} else if (tve_s.preferred_mode > 1) {
			printf("tve mode value invalid\n");
			goto err;
		}

		tve_s.saturation = fdtdec_get_int(gd->fdt_blob, node, "rockchip,saturation", 0);
		if (tve_s.saturation == 0) {
			printf("tve saturation err\n");
			goto err;
		}

		tve_s.brightcontrast = fdtdec_get_int(gd->fdt_blob, node, "rockchip,brightcontrast", 0);
		if (tve_s.brightcontrast == 0) {
			printf("tve brightcontrast err\n");
			goto err;
		}

		tve_s.adjtiming = fdtdec_get_int(gd->fdt_blob, node, "rockchip,adjtiming", 0);
		if (tve_s.adjtiming == 0) {
			printf("tve adjtiming err\n");
			goto err;
		}

		tve_s.lumafilter0 = fdtdec_get_int(gd->fdt_blob, node, "rockchip,lumafilter0", 0);
		if (tve_s.lumafilter0 == 0) {
			printf("tve lumafilter0 err\n");
			goto err;
		}

		tve_s.lumafilter1 = fdtdec_get_int(gd->fdt_blob, node, "rockchip,lumafilter1", 0);
		if (tve_s.lumafilter1 == 0) {
			printf("tve lumafilter1 err\n");
			goto err;
		}

		tve_s.lumafilter2 = fdtdec_get_int(gd->fdt_blob, node, "rockchip,lumafilter2", 0);
		if (tve_s.lumafilter2 == 0) {
			printf("tve lumafilter2 err\n");
			goto err;
		}

		dac_value = fdtdec_get_int(gd->fdt_blob, node, "rockchip,daclevel", 0);
		if (dac_value == 0) {
			printf("tve dac_value err\n");
			goto err;
		}

		tve_s.daclevel = dac_value;

		if (tve_s.soctype == SOC_RK322X) {
			getvdac = rk_get_vdac_value();
			if (getvdac > 0) {
				tve_s.daclevel = dac_value + getvdac - RK322X_VDAC_STANDARD;
				if (tve_s.daclevel > 0x3f ||
				    tve_s.daclevel < 0) {
					printf("rk322x daclevel error!\n");
					tve_s.daclevel = dac_value;
				}
			}
		}

		if (tve_s.soctype == SOC_RK322X ||
		    tve_s.soctype == SOC_RK322XH) {
			tve_s.dac1level = fdtdec_get_int(gd->fdt_blob, node, "rockchip,dac1level", 0);
			if (tve_s.dac1level == 0) {
				printf("rk322x dac1level error!\n");
				goto err;
			}
		}
		TVEDBG("tve_s.test_mode = 0x%x\n", tve_s.test_mode);
		TVEDBG("tve_s.saturation = 0x%x\n", tve_s.saturation);
		TVEDBG("tve_s.brightcontrast = 0x%x\n", tve_s.brightcontrast);
		TVEDBG("tve_s.adjtiming = 0x%x\n", tve_s.adjtiming);
		TVEDBG("tve_s.lumafilter0 = 0x%x\n", tve_s.lumafilter0);
		TVEDBG("tve_s.lumafilter1 = 0x%x\n", tve_s.lumafilter1);
		TVEDBG("tve_s.lumafilter2 = 0x%x\n", tve_s.lumafilter2);
		TVEDBG("tve_s.daclevel = 0x%x\n", tve_s.daclevel);
	}

	return 0;

err:
	dac_enable(0);
	return -ENODEV;
}

static int rockchip_drm_tve_enable(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;
	int tve_type;

#ifdef CONFIG_RKCHIP_INNO_HDMI_PHY
	/* set inno hdmi phy clk. */
	rockchip_phy_set_pll(state, 27000000);
#endif
	if (mode->vdisplay == 576)
		tve_type = TVOUT_CVBS_PAL;
	else
		tve_type = TVOUT_CVBS_NTSC;
	dac_enable(0);
	tve_set_mode(tve_type);
	dac_enable(1);

	return 0;
}

static void rockchip_drm_tve_deinit(struct display_state *state)
{
	dac_enable(0);
}

static int rockchip_drm_tve_prepare(struct display_state *state)
{
	return 0;
}

static int rockchip_drm_tve_disable(struct display_state *state)
{
	dac_enable(0);
	return 0;
}

static int rockchip_drm_tve_detect(struct display_state *state)
{
	return 1;
}

static void drm_tve_selete_output(struct overscan *overscan,
				  struct drm_display_mode *mode)
{
	int ret, i, screen_size;
	struct base_screen_info *screen_info = NULL;
	struct base_disp_info base_parameter;
	struct drm_display_mode modes[2];
	const struct base_overscan *scan;
	const disk_partition_t *ptn_baseparameter;
	char baseparameter_buf[8 * RK_BLK_SIZE] __aligned(ARCH_DMA_MINALIGN);
	int max_scan = 100;
	int min_scan = 50;

	overscan->left_margin = max_scan;
	overscan->right_margin = max_scan;
	overscan->top_margin = max_scan;
	overscan->bottom_margin = max_scan;

	for (i = 0; i < 2; i++) {
		modes[i] = tve_modes[i];
		if (i == tve_s.preferred_mode)
			modes[i].type |= DRM_MODE_TYPE_PREFERRED;
	}
	*mode = modes[tve_s.preferred_mode];

	ptn_baseparameter = get_disk_partition("baseparameter");
	if (!ptn_baseparameter) {
		printf("%s; fail get baseparameter\n", __func__);
		return;
	}
	ret = StorageReadLba(ptn_baseparameter->start, baseparameter_buf, 8);
	if (ret < 0) {
		printf("%s; fail read baseparameter\n", __func__);
		return;
	}

	memcpy(&base_parameter, baseparameter_buf, sizeof(base_parameter));
	scan = &base_parameter.scan;

	screen_size = sizeof(base_parameter.screen_list) /
		sizeof(base_parameter.screen_list[0]);

	for (i = 0; i < screen_size; i++) {
		if (base_parameter.screen_list[i].type ==
		    DRM_MODE_CONNECTOR_TV) {
			screen_info = &base_parameter.screen_list[i];
			break;
		}
	}

	if (scan->leftscale < min_scan && scan->leftscale > 0)
		overscan->left_margin = min_scan;
	else if (scan->leftscale < max_scan)
		overscan->left_margin = scan->leftscale;

	if (scan->rightscale < min_scan && scan->rightscale > 0)
		overscan->right_margin = min_scan;
	else if (scan->rightscale < max_scan)
		overscan->right_margin = scan->rightscale;

	if (scan->topscale < min_scan && scan->topscale > 0)
		overscan->top_margin = min_scan;
	else if (scan->topscale < max_scan)
		overscan->top_margin = scan->topscale;

	if (scan->bottomscale < min_scan && scan->bottomscale > 0)
		overscan->bottom_margin = min_scan;
	else if (scan->bottomscale < max_scan)
		overscan->bottom_margin = scan->bottomscale;

	if (screen_info &&
	    (screen_info->mode.hdisplay == 720 &&
	    screen_info->mode.vdisplay == 576 &&
	    screen_info->mode.hsync_start == 753 &&
	    screen_info->mode.hsync_end == 816))
		*mode = modes[0];
	else if (screen_info &&
		 screen_info->mode.vdisplay == 480 &&
		 screen_info->mode.vsync_start == 480 &&
		 screen_info->mode.vsync_end == 486)
		*mode = modes[1];

	if (screen_info)
		printf("base_parameter.mode:%dx%d\n",
		       screen_info->mode.hdisplay,
		       screen_info->mode.vdisplay);
}

static int rockchip_drm_tve_get_timing(struct display_state *state)
{
	struct connector_state *conn_state = &state->conn_state;
	struct drm_display_mode *mode = &conn_state->mode;

	conn_state->output_mode = ROCKCHIP_OUT_MODE_P888;
	conn_state->bus_format = MEDIA_BUS_FMT_YUV8_1X24;
	drm_tve_selete_output(&conn_state->overscan, mode);

	return 0;
}

static int rockchip_drm_tve_probe(struct udevice *dev)
{
	return 0;
}


const struct rockchip_connector_funcs rockchip_drm_tve_funcs = {
	.init = rockchip_drm_tve_init,
	.deinit = rockchip_drm_tve_deinit,
	.prepare = rockchip_drm_tve_prepare,
	.enable = rockchip_drm_tve_enable,
	.disable = rockchip_drm_tve_disable,
	.get_timing = rockchip_drm_tve_get_timing,
	.detect = rockchip_drm_tve_detect,
};

static const struct rockchip_connector rk3328_drm_tve_data = {
	.funcs = &rockchip_drm_tve_funcs,
};

static const struct udevice_id rockchip_drm_tve_ids[] = {
	{
	 .compatible = "rockchip,rk3328-tve",
	 .data = (ulong)&rk3328_drm_tve_data,
	}, {}
};

U_BOOT_DRIVER(rockchip_drm_tve) = {
	.name = "rockchip_drm_tve",
	.id = UCLASS_DISPLAY,
	.of_match = rockchip_drm_tve_ids,
	.probe	= rockchip_drm_tve_probe,
};