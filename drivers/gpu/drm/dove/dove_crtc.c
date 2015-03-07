/*
 * Marvell Dove DRM driver - CRTC
 *
 * Copyright (C) 2013-2014
 *   Jean-Francois Moine <moinejf@free.fr>
 *   Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/component.h>
#include <drm/drm_plane_helper.h>

#include "dove_drv.h"
#include "dove_lcd.h"

//#define LCD_DEBUG 1

#define to_dove_lcd(x) container_of(x, struct dove_lcd, crtc)

static inline void dove_write(struct dove_lcd *dove_lcd, u32 reg, u32 data)
{
	writel_relaxed(data, dove_lcd->mmio + reg);
}
static inline u32 dove_read(struct dove_lcd *dove_lcd, u32 reg)
{
	return readl_relaxed(dove_lcd->mmio + reg);
}
static inline void dove_set(struct dove_lcd *dove_lcd, u32 reg, u32 mask)
{
	dove_write(dove_lcd, reg, dove_read(dove_lcd, reg) | mask);
}
static inline void dove_clear(struct dove_lcd *dove_lcd, u32 reg, u32 mask)
{
	dove_write(dove_lcd, reg, dove_read(dove_lcd, reg) & ~mask);
}

#ifdef LCD_DEBUG
static ssize_t lcd_read_(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	struct dove_lcd *dove_lcd = dev_get_drvdata(dev);
	u32 addr, len, a;
	u32 val;
	unsigned char tmp[4 * 5 + 2], *p;

	if (sscanf(buf, "%x %x", &addr, &len) == 2) {
		if ((unsigned) len > 4)
			len = 4;
		p = tmp;
		a = addr;
		while (len--) {
			val = dove_read(dove_lcd, a);
			p += sprintf(p, " %04x", val);
			a += 4;
		}
		pr_info("lcd read %04x:%s\n", addr, tmp);
	} else {
		if (sscanf(buf, "%x", &addr) != 1) {
			pr_info("lcd read use: 'addr' [ 'len' ]\n");
			return size;
		}
		val = dove_read(dove_lcd, addr);
		pr_info("lcd read %04x: %04x\n", addr, val);
	}
	return size;
}

static ssize_t lcd_write(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t size)
{
	struct dove_lcd *dove_lcd = dev_get_drvdata(dev);
	unsigned int addr, val;

	sscanf(buf, "%x %x", &addr, &val);

	dove_write(dove_lcd, addr, val);

	pr_info("lcd write %04x @ %04x\n", val, addr);
	return size;
}
static DEVICE_ATTR(lcd_read, S_IWUSR, NULL, lcd_read);
static DEVICE_ATTR(lcd_write, S_IWUSR, NULL, lcd_write);
#endif

/*
 * vertical blank functions
 */
u32 dove_vblank_count(struct drm_device *drm, int crtc)
{
	struct dove_drm *dove_drm = drm_to_dove(drm);
	struct dove_lcd *dove_lcd = dove_drm->lcds[crtc];

	return STA_GRA_FRAME_COUNT(dove_read(dove_lcd, SPU_IRQ_ISR));
}

int dove_enable_vblank(struct drm_device *drm, int crtc)
{
	struct dove_drm *dove_drm = drm_to_dove(drm);
	struct dove_lcd *dove_lcd = dove_drm->lcds[crtc];

#ifdef HANDLE_INTERLACE
	dove_lcd->vblank_enabled = 1;
#endif
	dove_set(dove_lcd, SPU_IRQ_ENA, IRQ_GRA_FRAME_DONE);
	return 0;
}

void dove_disable_vblank(struct drm_device *drm, int crtc)
{
	struct dove_drm *dove_drm = drm_to_dove(drm);
	struct dove_lcd *dove_lcd = dove_drm->lcds[crtc];

#ifdef HANDLE_INTERLACE
	dove_lcd->vblank_enabled = 0;
	if (!dove_lcd->v_sync0)
#endif
	dove_clear(dove_lcd, SPU_IRQ_ENA, IRQ_GRA_FRAME_DONE);
}

#ifdef CONFIG_DEBUG_FS
static int dove_lcd_regs_show(struct seq_file *m,
			struct dove_lcd *dove_lcd)
{
	u32 x, shl, shh, total_v, total_h, active_h, active_v;
	u32 orig_buff_x, orig_buff_y, zoomed_x, zoomed_y;
	unsigned i;

	seq_printf(m, "\t\t*** LCD %d ***\n", dove_lcd->num);

	/* Get resolution */
	x = dove_read(dove_lcd, LCD_SPU_V_H_ACTIVE);
	active_h = H_LCD(x);
	active_v = V_LCD(x);

	/* Get total line */
	x = dove_read(dove_lcd, LCD_SPUT_V_H_TOTAL);
	total_h = H_LCD(x);
	total_v = V_LCD(x);
	seq_printf(m, "----total-------------------------<%4dx%4d>"
					"-------------------------\n"
			"----active--------------|", total_h, total_v);

	/* Get H Timings */
	x = dove_read(dove_lcd, LCD_SPU_H_PORCH);
	shl = F_LCD(x);
	shh = B_LCD(x);
	seq_printf(m, "->front porch(%d)->hsync(%d)->back porch(%d)\n",
		shl, total_h - shl -shh - active_h, shh);

	seq_printf(m,	"|\t\t\t|\n"
			"|\t\t\t|\n"
			"|\t<%4dx%4d>\t|\n"
			"|\t\t\t|\n"
			"|\t\t\t|\n"
			"------------------------|\n", active_h, active_v);

	/* Get V Timings */
	x = dove_read(dove_lcd, LCD_SPU_V_PORCH);
	shl = F_LCD(x);
	shh = B_LCD(x);
	seq_printf(m, "|\n|front porch(%d)\n|vsync(%d)\n|back porch(%d)\n",
		shl, total_v - shl - shh - active_v, shh);
	seq_printf(m, "----------------------------------"
			"-----------------------------------\n");

	/* Get Line Pitch */
	x = dove_read(dove_lcd, LCD_CFG_GRA_PITCH);
	shl = x & 0x0000ffff;
	seq_printf(m, "gfx line pitch in memory is <%d>\n", shl);

	/* Get scaling info */
	x = dove_read(dove_lcd, LCD_SPU_GRA_HPXL_VLN);
	orig_buff_x = H_LCD(x);
	orig_buff_y = V_LCD(x);
	x = dove_read(dove_lcd, LCD_SPU_GZM_HPXL_VLN);
	zoomed_x = H_LCD(x);
	zoomed_y = V_LCD(x);
	seq_printf(m, "Scaled from <%dx%d> to <%dx%d>\n",
			orig_buff_x, orig_buff_y, zoomed_x, zoomed_y);

	seq_printf(m, "======================================\n");

	for (i = 0x0080; i <= 0x01c4; i += 4) {
		x = dove_read(dove_lcd, i);
		seq_printf(m, "0x%04x 0x%08x\n", i, x);
	}
	return 0;
}

static int dove_regs_show(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = m->private;
	struct drm_device *drm = node->minor->dev;
	struct dove_drm *dove_drm = drm_to_dove(drm);
	struct dove_lcd *dove_lcd;
	unsigned i;

	for (i = 0; i < MAX_DOVE_LCD; i++) {
		dove_lcd = dove_drm->lcds[i];
		if (dove_lcd)
			dove_lcd_regs_show(m, dove_lcd);
	}
	return 0;
}

static struct drm_info_list dove_debugfs_list[] = {
	{ "lcd_regs", dove_regs_show, 0 },
	{ "fb",   drm_fb_cma_debugfs_show, 0 },
};

int dove_debugfs_init(struct drm_minor *minor)
{
	struct drm_device *dev = minor->dev;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	ret = drm_debugfs_create_files(dove_debugfs_list,
			ARRAY_SIZE(dove_debugfs_list),
			minor->debugfs_root, minor);
	if (ret)
		dev_err(dev->dev, "could not install dove_debugfs_list\n");

	return ret;
}

void dove_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files(dove_debugfs_list,
			ARRAY_SIZE(dove_debugfs_list), minor);
}
#endif

static void dove_update_base(struct dove_lcd *dove_lcd)
{
	struct drm_crtc *crtc = &dove_lcd->crtc;
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct drm_gem_cma_object *gem;
	unsigned int depth, bpp;
	dma_addr_t start;

	drm_fb_get_bpp_depth(fb->pixel_format, &depth, &bpp);
	gem = drm_fb_cma_get_gem_obj(fb, 0);
	start = gem->paddr + fb->offsets[0] +
			crtc->y * fb->pitches[0] +
			crtc->x * bpp / 8;

	dove_write(dove_lcd, LCD_CFG_GRA_START_ADDR0, start);
#ifdef HANDLE_INTERLACE
	if (dove_lcd->crtc.mode.mode->flags & DRM_MODE_FLAG_INTERLACE) {
		dove_write(dove_lcd, LCD_CFG_GRA_START_ADDR1,
					start + fb->pitches[0]);
		dove_write(dove_lcd, LCD_CFG_GRA_PITCH, fb->pitches[0] * 2);
		return;
	}
#endif
	dove_write(dove_lcd, LCD_CFG_GRA_START_ADDR1, start);
	dove_write(dove_lcd, LCD_CFG_GRA_PITCH, fb->pitches[0]);
}

static void set_frame_timings(struct dove_lcd *dove_lcd)
{
	struct drm_crtc *crtc = &dove_lcd->crtc;
	const struct drm_display_mode *mode = &crtc->mode;
	u32 h_active, v_active, h_orig, v_orig, h_zoom, v_zoom;
	u32 hfp, hbp, vfp, vbp, hs, vs, v_total;
	u32 x;

	/*
	 * Calc active size, zoomed size, porch.
	 */
	h_active = h_zoom = mode->hdisplay;
	v_active = v_zoom = mode->vdisplay;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	hs = mode->hsync_end - mode->hsync_start;
	vs = mode->vsync_end - mode->vsync_start;

	/*
	 * Calc original size.
	 */
	h_orig = h_active;
	v_orig = v_active;

#ifdef HANDLE_INTERLACE
	/* interlaced workaround */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		v_active /= 2;
		v_zoom /= 2;
		v_orig /= 2;
	}
#endif

	/* calc total width and height */
	v_total = v_active + vfp + vs + vbp;

	/* apply setting to registers */
	dove_write(dove_lcd, LCD_SPU_V_H_ACTIVE, LCD_H_V(h_active, v_active));
	dove_write(dove_lcd, LCD_SPU_GRA_HPXL_VLN, LCD_H_V(h_orig, v_orig));
	dove_write(dove_lcd, LCD_SPU_GZM_HPXL_VLN, LCD_H_V(h_zoom, v_zoom));
	dove_write(dove_lcd, LCD_SPU_H_PORCH, LCD_F_B(hfp, hbp));
	dove_write(dove_lcd, LCD_SPU_V_PORCH, LCD_F_B(vfp, vbp));
	dove_write(dove_lcd, LCD_SPUT_V_H_TOTAL,
					LCD_H_V(mode->htotal, v_total));

	/* configure vsync adjust logic */
	x = dove_read(dove_lcd, LCD_TV_CONTROL1);
	x &= ~(VSYNC_L_OFFSET_MASK | VSYNC_H_OFFSET_MASK);
	x |= VSYNC_OFFSET_EN |			/* VSYNC adjust enable */
		VSYNC_L_OFFSET(h_active + hfp) |
		VSYNC_H_OFFSET(h_active + hfp);
#ifdef HANDLE_INTERLACE
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		dove_lcd->v_sync0 = VSYNC_L_OFFSET(h_active + hfp) |
				VSYNC_H_OFFSET(h_active + hfp);
		dove_lcd->v_sync1 = VSYNC_L_OFFSET(h_active / 2 + hfp) |
				VSYNC_H_OFFSET(h_active / 2 + hfp);
	} else {
		dove_lcd->v_sync0 = 0;
	}
#endif
	dove_write(dove_lcd, LCD_TV_CONTROL1, x);
}

static void dove_set_clock(struct dove_lcd *dove_lcd)
{
	struct drm_crtc *crtc = &dove_lcd->crtc;
	const struct drm_display_mode *mode = &crtc->mode;
	struct clk *clk;
	u32 x, needed_pixclk, ref_clk, div, fract;
	int clk_src;

	fract = 0;
	needed_pixclk = mode->clock * 1000;
#ifdef HANDLE_INTERLACE
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		needed_pixclk /= 2;
#endif

	/* first check if pixclk is multiple of current clock */
	clk_src = dove_lcd->clk_src;
	clk = dove_lcd->clk;
	ref_clk = clk_get_rate(clk);

	DRM_DEBUG_DRIVER("clk src %d rate %u needed %u div %u mod %u\n",
			clk_src, ref_clk, needed_pixclk,
			ref_clk / needed_pixclk, ref_clk % needed_pixclk);

	if (ref_clk % needed_pixclk == 0) {
		div = ref_clk / needed_pixclk;
		goto set_clock;
	}

	/* try to set current clock to requested pixclk */
	clk_set_rate(clk, needed_pixclk);
	ref_clk = clk_get_rate(clk);
	if (ref_clk == needed_pixclk) {
		div = 1;
		goto set_clock;
	}

	/* use internal divider */
	if (false) {
/*fixme: does not work*/
		ref_clk /= 1000;
		needed_pixclk /= 1000;
		x = (ref_clk * 0x1000 + needed_pixclk - 1) / needed_pixclk;
		div = x >> 12;
		if (div < 1)
			div = 1;
		else
			fract = x & 0xfff;
	} else {
		div = (ref_clk + needed_pixclk - 1) / needed_pixclk;
		if (div < 1)
			div = 1;
	}

set_clock:
	DRM_DEBUG_DRIVER("set clk src %d ref %u div %u fract %u needed %u\n",
			clk_src, ref_clk, div, fract, needed_pixclk);
	x = SET_SCLK(clk_src, div, fract);
	dove_write(dove_lcd, LCD_CFG_SCLK_DIV, x);
}

static void set_dma_control(struct dove_lcd *dove_lcd)
{
	const struct drm_display_mode *mode = &dove_lcd->crtc.mode;
	u32 x;
	int fmt, rbswap;

	rbswap = 1;				/* default */
	switch (dove_lcd->crtc.primary->fb->pixel_format) {
	case DRM_FORMAT_BGR888:
		rbswap = 0;
	case DRM_FORMAT_RGB888:
		fmt = GMODE_RGB888PACKED;
		break;
	case DRM_FORMAT_XBGR8888:
		rbswap = 0;
	case DRM_FORMAT_XRGB8888:		/* depth 24 */
		fmt = GMODE_RGBA888;
		break;
	case DRM_FORMAT_ABGR8888:
		rbswap = 0;
	case DRM_FORMAT_ARGB8888:		/* depth 32 */
		fmt = GMODE_RGB888UNPACKED;
		break;
	case DRM_FORMAT_YVYU:
		rbswap = 0;
	case DRM_FORMAT_YUYV:
		fmt = GMODE_YUV422PACKED;
		break;
	case DRM_FORMAT_YVU422:
		rbswap = 0;
	case DRM_FORMAT_YUV422:
		fmt = GMODE_YUV422PLANAR;
		break;
	case DRM_FORMAT_YVU420:
		rbswap = 0;
	default:
/*	case DRM_FORMAT_YUV420: */
		fmt = GMODE_YUV420PLANAR;
		break;
	}

	x = dove_read(dove_lcd, LCD_SPU_DMA_CTRL0);
	x &= ~(CFG_PALETTE_ENA |		/* true color */
		CFG_GRAFORMAT_MASK |
		CFG_GRA_SWAPRB |
		CFG_GRA_FTOGGLE);
	x |= CFG_GRA_ENA |			/* graphic enable */
		CFG_GRA_HSMOOTH;		/* horiz. smooth scaling */
	x |= CFG_GRAFORMAT(fmt);

	if (!rbswap)
		x |= CFG_GRA_SWAPRB;
#ifdef HANDLE_INTERLACE
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		x |= CFG_GRA_FTOGGLE;
#endif
	dove_write(dove_lcd, LCD_SPU_DMA_CTRL0, x);

	/*
	 * trigger DMA on the falling edge of vsync if vsync is
	 * active low, or on the rising edge if vsync is active high
	 */
	x = dove_read(dove_lcd, LCD_SPU_DMA_CTRL1);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		x |= CFG_VSYNC_INV;
	else
		x &= ~CFG_VSYNC_INV;
	dove_write(dove_lcd, LCD_SPU_DMA_CTRL1, x);
}

/* this function is called on mode DRM_MODE_DPMS_ON
 * and also at loading time with gpio_only set */
static void set_dumb_panel_control(struct dove_lcd *dove_lcd,
				int gpio_only)
{
	const struct drm_display_mode *mode = &dove_lcd->crtc.mode;
	u32 x;

	x = 0;
	if (dove_lcd->dpms == DRM_MODE_DPMS_ON)
		x = CFG_DUMB_ENA;
	if (!gpio_only) {
		if (dove_lcd->dpms == DRM_MODE_DPMS_ON)
			/*
			 * When dumb interface isn't under 24bit
			 * It might be under SPI or GPIO. If set
			 * to 0x7 will force LCD_D[23:0] output
			 * blank color and damage GPIO and SPI
			 * behavior.
			 */
			x |= CFG_DUMBMODE(DUMB24_RGB888_0);
		else
			x |= CFG_DUMBMODE(7);
		if (mode->flags & DRM_MODE_FLAG_NVSYNC)
			x |= CFG_INV_VSYNC;
		if (mode->flags & DRM_MODE_FLAG_NHSYNC)
			x |= CFG_INV_HSYNC;
	}

	dove_write(dove_lcd, LCD_SPU_DUMB_CTRL, x);
}

void dove_crtc_start(struct dove_lcd *dove_lcd)
{
	struct drm_crtc *crtc = &dove_lcd->crtc;
	struct drm_display_mode *mode = &crtc->mode;

	DRM_DEBUG_DRIVER("\n");
	if (mode->clock == 0) {
		dev_err(dove_lcd->dev, "crtc_start: no clock!\n");
		dove_lcd->dpms = DRM_MODE_DPMS_OFF;
		return;
	}

	set_frame_timings(dove_lcd);
	dove_set_clock(dove_lcd);
	set_dma_control(dove_lcd);
	dove_update_base(dove_lcd);
	set_dumb_panel_control(dove_lcd, 0);

#ifdef HANDLE_INTERLACE
	if (dove_lcd->v_sync0) {		/* interlace mode on */
		dove_set(dove_lcd, SPU_IRQ_ENA, IRQ_GRA_FRAME_DONE);
	} else {				/* interlace mode off */
		if (!dove_lcd->vblank_enabled)
			dove_clear(dove_lcd, SPU_IRQ_ENA, IRQ_GRA_FRAME_DONE);
	}
#endif

	drm_mode_debug_printmodeline(mode);
}

void dove_crtc_stop(struct dove_lcd *dove_lcd)
{
	DRM_DEBUG_DRIVER("\n");

	dove_clear(dove_lcd, LCD_SPU_DMA_CTRL0, CFG_GRA_ENA);
	dove_clear(dove_lcd, LCD_SPU_DUMB_CTRL, CFG_DUMB_ENA);
#ifdef HANDLE_INTERLACE
	if (dove_lcd->v_sync0
	 && !dove_lcd->vblank_enabled)
		dove_clear(dove_lcd, SPU_IRQ_ENA, IRQ_GRA_FRAME_DONE);
#endif
}

/* -----------------------------------------------------------------------------
 * cursor
 */

/* load the hardware cursor */
static int load_cursor(struct dove_lcd *dove_lcd,
			struct drm_file *file_priv,
			uint32_t handle,
			int data_len)
{
	struct dove_drm *dove_drm = dove_lcd->dove_drm;
	struct drm_gem_object *obj;
	struct drm_gem_cma_object *cma_obj;
	u8 *p_pixel;
	u32 u, val;
	u32 ram, color;
	int i, j, ret;

	obj = drm_gem_object_lookup(dove_drm->drm, file_priv, handle);
	if (!obj)
		return -ENOENT;

	if (!drm_vma_node_has_offset(&obj->vma_node)) {
		dev_warn(dove_lcd->dev, "cursor not mapped\n");
		ret = -EINVAL;
		goto out;
	}

	if (data_len != obj->size) {
		dev_warn(dove_lcd->dev, "bad cursor size\n");
		ret = -EINVAL;
		goto out;
	}

	cma_obj = to_drm_gem_cma_obj(obj);
	p_pixel = cma_obj->vaddr;

	u = CFG_SRAM_INIT_WR_RD(SRAMID_INIT_WRITE) |
			CFG_SRAM_ADDR_LCDID(SRAMID_HWC);
	ram = CFG_SRAM_INIT_WR_RD(SRAMID_INIT_WRITE);

	/* load the RGBA cursor to SRAM */
	for (i = 0; i < data_len / 4 / 4; i++) {
		color = (p_pixel[3 * 4 + 0] << 24) |	/* red */
			(p_pixel[2 * 4 + 0] << 16) |
			(p_pixel[1 * 4 + 0] << 8) |
			p_pixel[0];
		dove_write(dove_lcd, LCD_SPU_SRAM_WRDAT, color);
		dove_write(dove_lcd, LCD_SPU_SRAM_CTRL,
				ram | CFG_SRAM_ADDR_LCDID(SRAMID_HWC32_RAM1));
		color = (p_pixel[3 * 4 + 1] << 24) |	/* green */
			(p_pixel[2 * 4 + 1] << 16) |
			(p_pixel[1 * 4 + 1] << 8) |
			p_pixel[1];
		dove_write(dove_lcd, LCD_SPU_SRAM_WRDAT, color);
		dove_write(dove_lcd, LCD_SPU_SRAM_CTRL,
				ram | CFG_SRAM_ADDR_LCDID(SRAMID_HWC32_RAM2));
		color = (p_pixel[3 * 4 + 2] << 24) |	/* blue */
			(p_pixel[2 * 4 + 2] << 16) |
			(p_pixel[1 * 4 + 2] << 8) |
			p_pixel[2];
		dove_write(dove_lcd, LCD_SPU_SRAM_WRDAT, color);
		dove_write(dove_lcd, LCD_SPU_SRAM_CTRL,
				ram | CFG_SRAM_ADDR_LCDID(SRAMID_HWC32_RAM3));
		p_pixel += 4 * 4;
		if ((++ram & 0xff) == 0) {
			ram -= 0x100;			/* I[7:0] */
			ram += 1 << 12;			/* J[1:0] */
		}
	}

	/* set the transparency */
	p_pixel = cma_obj->vaddr;
	for (i = 0; i < data_len / 16 / 4; i++) {
		val = 0;
		for (j = 16 * 4 - 4; j >= 0 ; j -= 4) {
			val <<= 2;
			if (p_pixel[j + 3])	/* alpha */
				val |= 1;	/* not transparent */
		}
		dove_write(dove_lcd, LCD_SPU_SRAM_WRDAT, val);
		dove_write(dove_lcd, LCD_SPU_SRAM_CTRL, u++);
		p_pixel += 16 * 4;
	}
	ret = 0;
out:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

static int dove_cursor_set(struct drm_crtc *crtc,
			struct drm_file *file_priv,
			uint32_t handle,
			uint32_t width,
			uint32_t height)
{
	struct dove_lcd *dove_lcd = to_dove_lcd(crtc);
	int ret;

	DRM_DEBUG_DRIVER("%dx%d handle %d\n", width, height, handle);

	/* disable cursor */
	dove_clear(dove_lcd, LCD_SPU_DMA_CTRL0, CFG_HWC_ENA);

	if (!handle)
		return 0;		/* cursor off */

	if (width != 64 || height != 64) {
		dev_err(dove_lcd->dev, "bad cursor size\n");
		return -EINVAL;
	}

	/* load the cursor */
	ret = load_cursor(dove_lcd, file_priv, handle, width * height * 4);
	if (ret < 0)
		return ret;

	/* set cursor size */
	dove_write(dove_lcd, LCD_SPU_HWC_HPXL_VLN, LCD_H_V(width, height));

	/* enable cursor */
	dove_set(dove_lcd, LCD_SPU_DMA_CTRL0, CFG_HWC_ENA);

	return 0;
}

static int dove_cursor_move(struct drm_crtc *crtc,
				int x, int y)
{
	struct dove_lcd *dove_lcd = to_dove_lcd(crtc);

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	dove_clear(dove_lcd, LCD_SPU_DMA_CTRL0, CFG_HWC_ENA);
	dove_write(dove_lcd, LCD_SPU_HWC_OVSA_HPXL_VLN, LCD_H_V(x, y));
	dove_set(dove_lcd, LCD_SPU_DMA_CTRL0, CFG_HWC_ENA);
	return 0;
}

static void dove_crtc_destroy(struct drm_crtc *crtc)
{
	struct dove_lcd *dove_lcd = to_dove_lcd(crtc);

	DRM_DEBUG_DRIVER("\n");

	WARN_ON(dove_lcd->dpms == DRM_MODE_DPMS_ON);

	drm_crtc_cleanup(crtc);
}

static int dove_crtc_page_flip(struct drm_crtc *crtc,
			struct drm_framebuffer *fb,
			struct drm_pending_vblank_event *event,
			uint32_t page_flip_flags)
{
	struct dove_lcd *dove_lcd = to_dove_lcd(crtc);
	struct drm_device *drm = crtc->dev;
	unsigned long flags;

	DRM_DEBUG_DRIVER("\n");

	spin_lock_irqsave(&drm->event_lock, flags);
	if (dove_lcd->event) {
		spin_unlock_irqrestore(&drm->event_lock, flags);
		dev_err(drm->dev, "already pending page flip!\n");
		return -EBUSY;
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);

	crtc->primary->fb = fb;
	dove_update_base(dove_lcd);

	if (event) {
		event->pipe = 0;
		spin_lock_irqsave(&drm->event_lock, flags);
		dove_lcd->event = event;
		spin_unlock_irqrestore(&drm->event_lock, flags);
		drm_vblank_get(drm, dove_lcd->num);
	}

	return 0;
}

static void dove_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct dove_lcd *dove_lcd = to_dove_lcd(crtc);

	/* we really only care about on or off */
	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	DRM_DEBUG_DRIVER("dpms %s\n", mode == DRM_MODE_DPMS_ON ? "on" : "off");

	if (dove_lcd->dpms == mode)
		return;

	dove_lcd->dpms = mode;

	if (mode == DRM_MODE_DPMS_ON)
		dove_crtc_start(dove_lcd);
	else
		dove_crtc_stop(dove_lcd);
}

static bool dove_crtc_mode_fixup(struct drm_crtc *crtc,
				const struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG_DRIVER("\n");
	if (mode->vrefresh == 0) {
pr_info("dove no vrefresh\n");
#if 0
/*		drm_mode_set_name(mode); */
		mode->vrefresh = drm_mode_vrefresh(mode);
		drm_mode_debug_printmodeline(mode);
#endif
	}
	return true;
}

static void dove_crtc_prepare(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER("\n");
	dove_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void dove_crtc_commit(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER("\n");
	dove_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static int dove_crtc_mode_set(struct drm_crtc *crtc,
			struct drm_display_mode *mode,
			struct drm_display_mode *adjusted_mode,
			int x, int y,
			struct drm_framebuffer *old_fb)
{
	DRM_DEBUG_DRIVER("\n");

	if (mode->hdisplay > 2048)
		return MODE_VIRTUAL_X;

	/* width must be multiple of 16 */
	if (mode->hdisplay & 0xf)
		return MODE_VIRTUAL_X;

	if (mode->vdisplay > 2048)
		return MODE_VIRTUAL_Y;

	return MODE_OK;
}

static int dove_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
				struct drm_framebuffer *old_fb)
{
	struct dove_lcd *dove_lcd = to_dove_lcd(crtc);

	DRM_DEBUG_DRIVER("\n");
	dove_update_base(dove_lcd);
	return 0;
}

static const struct drm_crtc_funcs dove_crtc_funcs = {
	.cursor_set	= dove_cursor_set,
	.cursor_move	= dove_cursor_move,
	.destroy        = dove_crtc_destroy,
	.set_config     = drm_crtc_helper_set_config,
	.page_flip      = dove_crtc_page_flip,
};

static const struct drm_crtc_helper_funcs dove_crtc_helper_funcs = {
	.dpms           = dove_crtc_dpms,
	.mode_fixup     = dove_crtc_mode_fixup,
	.prepare        = dove_crtc_prepare,
	.commit         = dove_crtc_commit,
	.mode_set       = dove_crtc_mode_set,
	.mode_set_base  = dove_crtc_mode_set_base,
};

void dove_crtc_cancel_page_flip(struct dove_lcd *dove_lcd,
				struct drm_file *file)
{
	struct drm_pending_vblank_event *event;
	struct drm_device *drm = dove_lcd->crtc.dev;
	unsigned long flags;

	DRM_DEBUG_DRIVER("\n");

	/*
	 * Destroy the pending vertical blanking event associated with the
	 * pending page flip, if any, and disable vertical blanking interrupts.
	 */
	spin_lock_irqsave(&drm->event_lock, flags);
	event = dove_lcd->event;
	if (event && event->base.file_priv == file) {
		dove_lcd->event = NULL;
		event->base.destroy(&event->base);
		drm_vblank_put(drm, dove_lcd->num);
	}
	spin_unlock_irqrestore(&drm->event_lock, flags);
}

/* configure default register values */
static void dove_set_defaults(struct dove_lcd *dove_lcd)
{
	u32 x;

	x = SET_SCLK(dove_lcd->clk_src, 1, 0);
	dove_write(dove_lcd, LCD_CFG_SCLK_DIV, x);
	dove_write(dove_lcd, LCD_SPU_BLANKCOLOR, 0);

	dove_write(dove_lcd, SPU_IOPAD_CONTROL, IOPAD_DUMB24);
	dove_write(dove_lcd, LCD_CFG_GRA_START_ADDR1, 0);
	dove_write(dove_lcd, LCD_SPU_GRA_OVSA_HPXL_VLN, 0);
	dove_write(dove_lcd, LCD_SPU_SRAM_PARA0, 0);
	dove_write(dove_lcd, LCD_SPU_SRAM_PARA1, CFG_CSB_256x32 |
						CFG_CSB_256x24 |
						CFG_CSB_256x8);
	dove_write(dove_lcd, LCD_SPU_DMA_CTRL1, CFG_VSYNC_TRIG(2) |
						CFG_GATED_ENA |
						CFG_PWRDN_ENA |
						CFG_ALPHA_MODE(2) |
						CFG_ALPHA(0xff) |
						CFG_PXLCMD(0x81));

	/*
	 * Fix me: to avoid jiggling issue for high resolution in
	 * dual display, we set watermark to affect LCD AXI read
	 * from MC (default 0x80). Lower watermark means LCD will
	 * do DMA read more often.
	 */
	x = dove_read(dove_lcd, LCD_CFG_RDREG4F);
	x &= ~DMA_WATERMARK_MASK;
	x |= DMA_WATERMARK(0x20);

	/*
	 * Disable LCD SRAM Read Wait State to resolve HWC32 make
	 * system hang while use external clock.
	 */
	x &= ~LCD_SRAM_WAIT;
	dove_write(dove_lcd, LCD_CFG_RDREG4F, x);

	/* prepare the hwc32 */
	dove_set(dove_lcd, LCD_TV_CONTROL1, HWC32_ENABLE);

	/* set hwc32 with 100% static alpha blending factor */
	dove_write(dove_lcd, LCD_SPU_ALPHA_COLOR1,
				HWC32_CFG_ALPHA(0xff));
}

static irqreturn_t dove_lcd_irq(int irq, void *dev_id)
{
	struct dove_lcd *dove_lcd = (struct dove_lcd *) dev_id;
	struct drm_pending_vblank_event *event;
	struct drm_device *drm = dove_lcd->crtc.dev;
	u32 isr;
	unsigned long flags;

	isr = dove_read(dove_lcd, SPU_IRQ_ISR);
	dove_write(dove_lcd, SPU_IRQ_ISR, 0);

	DRM_DEBUG_DRIVER("\n");

	if (isr & IRQ_GRA_FRAME_DONE) {
#ifdef HANDLE_INTERLACE
		if (dove_lcd->v_sync0) {
			u32 x;

			x = dove_read(dove_lcd, LCD_TV_CONTROL1);
			x &= ~(VSYNC_L_OFFSET_MASK | VSYNC_H_OFFSET_MASK);
			if (isr & IRQ_GRA_FRAME0)
				x |= dove_lcd->v_sync0;
			else
				x |= dove_lcd->v_sync1;
			dove_write(dove_lcd, LCD_TV_CONTROL1, x);
		}
		if (dove_lcd->vblank_enabled)
#endif
		drm_handle_vblank(drm, dove_lcd->num); 
		spin_lock_irqsave(&drm->event_lock, flags);
		event = dove_lcd->event;
		dove_lcd->event = NULL;
		if (event)
			drm_send_vblank_event(drm, dove_lcd->num, event);
		spin_unlock_irqrestore(&drm->event_lock, flags);
		if (event)
			drm_vblank_put(drm, dove_lcd->num);
	}

	return IRQ_HANDLED;
}

/* initialize a lcd */
static int dove_crtc_init(struct dove_lcd *dove_lcd)
{
	struct drm_crtc *crtc = &dove_lcd->crtc;
	struct dove_drm *dove_drm = dove_lcd->dove_drm;
	struct drm_device *drm = dove_drm->drm;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	dove_lcd->dpms = DRM_MODE_DPMS_OFF;

	ret = drm_crtc_init(drm, crtc, &dove_crtc_funcs);
	if (ret < 0)
		return ret;

	dove_write(dove_lcd, SPU_IRQ_ENA, 0);	/* disable interrupts */
	ret = devm_request_irq(dove_lcd->dev, dove_lcd->irq, dove_lcd_irq, 0,
			dove_lcd->name, dove_lcd);
	if (ret < 0) {
		dev_err(dove_lcd->dev, "unable to request irq %d\n",
				dove_lcd->irq);
		goto fail;
	}

	dove_set_defaults(dove_lcd);
	set_dumb_panel_control(dove_lcd, 1);

	drm_crtc_helper_add(crtc, &dove_crtc_helper_funcs);

	return 0;

fail:
	dove_crtc_destroy(crtc);
	return ret;
}

/* -----------------------------------------------------------------------------
 * Overlay plane
 */

static void plane_update_base(struct dove_lcd *dove_lcd,
				int plane_num,
				struct drm_framebuffer *fb,
				int fmt,
				int x, int y,
				int w, int h)
{
	struct drm_gem_cma_object *gem;
	dma_addr_t start, addr;

	DRM_DEBUG_DRIVER("%dx%d+%d+%d\n", w, h, x, y);

	gem = drm_fb_cma_get_gem_obj(fb, 0);
	if (!gem) {
		dev_err(dove_lcd->dev, "cannot get gem obj");
		return;
	}

	start = addr = gem->paddr + fb->offsets[0] + y * fb->pitches[0] + x;
	dove_write(dove_lcd, LCD_SPU_DMA_START_ADDR_Y0, addr);

	switch (fmt) {
	case VMODE_YUV422PLANAR:
	case VMODE_YUV420PLANAR:
		addr += fb->offsets[1];
		break;
	}
	dove_write(dove_lcd, LCD_SPU_DMA_START_ADDR_U0, addr);

	switch (fmt) {
	case VMODE_YUV422PLANAR:
	case VMODE_YUV420PLANAR:
		addr = start + fb->offsets[2];
		break;
	}
	dove_write(dove_lcd, LCD_SPU_DMA_START_ADDR_V0, addr);

	dove_write(dove_lcd, LCD_SPU_DMA_PITCH_YC,
				LCD_Y_C(fb->pitches[0], fb->pitches[0]));
	switch (fmt) {
	case VMODE_YUV422PLANAR:
	case VMODE_YUV420PLANAR:
		dove_write(dove_lcd, LCD_SPU_DMA_PITCH_UV,
				LCD_U_V(fb->pitches[1], fb->pitches[2]));
		break;
	default:
		dove_write(dove_lcd, LCD_SPU_DMA_PITCH_UV,
				LCD_U_V(fb->pitches[0], fb->pitches[0]));
		break;
	}
}

static int dove_plane_update(struct drm_plane *plane,
			struct drm_crtc *crtc,
			struct drm_framebuffer *fb,
			int crtc_x, int crtc_y,
			unsigned int crtc_w, unsigned int crtc_h,
			uint32_t src_x, uint32_t src_y,
			uint32_t src_w, uint32_t src_h)
{
	struct dove_lcd *dove_lcd = to_dove_lcd(crtc);
	u32 x, x_bk;
	int fmt, rbswap;

	DRM_DEBUG_DRIVER("fmt %.4s\n", (char *) &fb->pixel_format);

	rbswap = 1;				/* default */
	switch (fb->pixel_format) {
	case DRM_FORMAT_RGB888:
		rbswap = 0;
	case DRM_FORMAT_BGR888:
//		fmt = VMODE_RGB888UNPACKED;
		fmt = VMODE_RGB888PACKED;
		break;
	case DRM_FORMAT_YVYU:
		rbswap = 0;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
		fmt = VMODE_YUV422PACKED;
		break;
	case DRM_FORMAT_YVU422:
		rbswap = 0;
	case DRM_FORMAT_YUV422:
		fmt = VMODE_YUV422PLANAR;
		break;
	case DRM_FORMAT_YVU420:
		rbswap = 0;
	default:
/*	case DRM_FORMAT_YUV420: */
		fmt = VMODE_YUV420PLANAR;
		break;
	}

	x_bk = x = dove_read(dove_lcd, LCD_SPU_DMA_CTRL0);
					/* clear video layer's field */
	x &= ~(CFG_YUV2RGB_DMA | CFG_DMA_SWAP_MASK |
		CFG_DMA_TSTMODE | CFG_DMA_HSMOOTH | CFG_DMA_FTOGGLE |
		CFG_DMAFORMAT_MASK | CFG_PALETTE_ENA);
	x |= CFG_DMA_HSMOOTH;		/* enable horizontal smooth scaling */
	x |= CFG_DMAFORMAT(fmt);	/* configure hardware pixel format */
	if (fmt == VMODE_RGB888PACKED) {
		;
	} else if (fb->pixel_format == DRM_FORMAT_UYVY) {
		x |= CFG_YUV2RGB_DMA;
	} else if (fmt == VMODE_YUV422PACKED) {
		x |= CFG_YUV2RGB_DMA |
			CFG_DMA_SWAPYU |
			CFG_DMA_SWAPRB;
		if (rbswap)
			x |= CFG_DMA_SWAPUV;
	} else {				/* planar */
		x |= CFG_YUV2RGB_DMA |
			CFG_DMA_SWAPRB;
		if (!rbswap)
			x |= CFG_DMA_SWAPUV;
	}

	/* set the dma addresses */
	plane_update_base(dove_lcd, 0,
			fb, fmt, src_x, src_y, src_w, src_h);

	/* original size */
	dove_write(dove_lcd, LCD_SPU_DMA_HPXL_VLN,
				LCD_H_V(src_w, src_h));

	/* scaled size */
	dove_write(dove_lcd, LCD_SPU_DZM_HPXL_VLN,
				LCD_H_V(crtc_w, crtc_h));

	/* update video position offset */
	dove_write(dove_lcd, LCD_SPUT_DMA_OVSA_HPXL_VLN,
				LCD_H_V(crtc_x, crtc_y));

	x |= CFG_DMA_ENA;
	if (x != x_bk) {
		dove_write(dove_lcd, LCD_SPU_DMA_CTRL0, x);
//		dove_set(dove_lcd, SPU_IRQ_ENA,
//				DOVEFB_VID_INT_MASK | DOVEFB_VSYNC_INT_MASK);
	}

	return 0;
}

static int dove_plane_disable(struct drm_plane *plane)
{
	struct dove_lcd *dove_lcd = to_dove_lcd(plane->crtc);

	DRM_DEBUG_DRIVER("\n");
//pr_info("dove_drm plane_disable crtc: %p", plane->crtc);
	if (!plane->crtc)
		return 0;

	dove_clear(dove_lcd, LCD_SPU_DMA_CTRL0, CFG_DMA_ENA);
//	dove_clear(dove_lcd, SPU_IRQ_ENA,
//			DOVEFB_VID_INT_MASK | DOVEFB_VSYNC_INT_MASK);
	return 0;
}

static void dove_plane_destroy(struct drm_plane *plane)
{
	dove_plane_disable(plane);
	drm_plane_cleanup(plane);
}

static const struct drm_plane_funcs plane_funcs = {
	.update_plane = dove_plane_update,
	.disable_plane = dove_plane_disable,
	.destroy = dove_plane_destroy,
};
static const uint32_t gfx_formats[] = {
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV420,
};
static const uint32_t vid_formats[] = {
	DRM_FORMAT_BGR888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_UYVY,
};

static int dove_plane_init(struct dove_lcd *dove_lcd)
{
	struct drm_device *drm = dove_lcd->crtc.dev;
	struct drm_plane *plane;
	u32 x;
	int ret;

	plane = &dove_lcd->plane;
	ret = drm_plane_init(drm, plane, 1 << dove_lcd->num,
				&plane_funcs,
				vid_formats, ARRAY_SIZE(vid_formats), false);
	if (ret < 0)
		return ret;

	dove_write(dove_lcd, LCD_SPU_COLORKEY_Y, 0xfefefe00);
	dove_write(dove_lcd, LCD_SPU_COLORKEY_U, 0x01010100);
	dove_write(dove_lcd, LCD_SPU_COLORKEY_V, 0x01010100);
	x = dove_read(dove_lcd, LCD_SPU_DMA_CTRL1);
	x &= ~(CFG_COLOR_KEY_MASK | CFG_ALPHA_MODE_MASK | CFG_ALPHA_MASK);
	x |= CFG_COLOR_KEY_MODE(3) | CFG_ALPHA_MODE(1);
	dove_write(dove_lcd, LCD_SPU_DMA_CTRL1, x);

	plane->crtc = &dove_lcd->crtc;
	return ret;
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

/* at probe time, get the possible LCD clocks from the DT */
static int get_lcd_clocks(struct dove_lcd *dove_lcd)
{
	struct device *dev = dove_lcd->dev;
	struct device_node *np = dev->of_node;
	char *clk_name;
	struct clk *clk;
	int clk_src, ret;
	static char *clock_names[] = {		/* !! index SCLK_SRC_xxx !! */
		"axibus", "ext_ref_clk0", "plldivider", "ext_ref_clk1"
	};

	/* get the clock and its name */
	ret = of_property_read_string(np, "clock-names",
				(const char **) &clk_name);
	if (ret) {
		dev_err(dev, "no available clock\n");
		return -EINVAL;
	}
	for (clk_src = 0; clk_src < ARRAY_SIZE(clock_names); clk_src++) {
		if (strcmp(clk_name, clock_names[clk_src]) == 0)
			break;
	}
	if (clk_src >= ARRAY_SIZE(clock_names)) {
		dev_err(dev, "unknown clock %s\n", clk_name);
		return -EINVAL;
	}
	clk = clk_get(dev, clk_name);
	if (IS_ERR(clk))
		return PTR_ERR(clk);
	DRM_DEBUG_DRIVER("clock %s ok\n", clk_name);
	clk_prepare_enable(clk);
	dove_lcd->clk = clk;
	dove_lcd->clk_src = clk_src;
	return 0;
}

static int dove_lcd_bind(struct device *dev, struct device *master,
			void *data)
{
	struct drm_device *drm = data;
	struct dove_drm *dove_drm = drm_to_dove(drm);
	struct dove_lcd *dove_lcd = dev_get_drvdata(dev);
	int ret;

	DRM_DEBUG_DRIVER("\n");

	dove_lcd->dove_drm = dove_drm;

	ret = get_lcd_clocks(dove_lcd);
	if (ret < 0)
		return ret;

	dove_drm->lcds[dove_lcd->num] = dove_lcd;

	ret = dove_crtc_init(dove_lcd);
	if (ret < 0)
		goto fail;
	ret = dove_plane_init(dove_lcd);
	if (ret < 0)
		dev_err(dove_lcd->dev, "failed to create the video plane\n");

	return 0;

fail:
	if (dove_lcd->clk) {
		clk_disable_unprepare(dove_lcd->clk);
		clk_put(dove_lcd->clk);
	}
	return ret;
}

static void dove_lcd_unbind(struct device *dev, struct device *master,
			void *data)
{
	struct dove_drm *dove_drm = data;
	struct platform_device *pdev = to_platform_device(dev);
	struct dove_lcd *dove_lcd = platform_get_drvdata(pdev);
	struct clk *clk;

	dove_write(dove_lcd, SPU_IRQ_ENA, 0);	/* disable interrupts */

	clk = dove_lcd->clk;
	if (clk) {
		clk_disable_unprepare(clk);
		clk_put(clk);
	}

// is this useful?	
	if (dove_drm->lcds[dove_lcd->num] == dove_lcd)
		dove_drm->lcds[dove_lcd->num] = NULL;
}

static const struct component_ops comp_ops = {
	.bind = dove_lcd_bind,
	.unbind = dove_lcd_unbind,
};

static int dove_lcd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct dove_lcd *dove_lcd;
	struct resource *res;
	int id;

	id = of_alias_get_id(np, "lcd");
	if ((unsigned) id >= MAX_DOVE_LCD) {
		dev_err(dev, "no or bad alias for lcd\n");
		return -ENXIO;
	}

	dove_lcd = devm_kzalloc(dev, sizeof *dove_lcd, GFP_KERNEL);
	if (!dove_lcd) {
		dev_err(dev, "failed to allocate private data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, dove_lcd);
	dove_lcd->dev = dev;
	dove_lcd->num = id;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get memory resource\n");
		return -EINVAL;
	}

	dove_lcd->mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(dove_lcd->mmio)) {
		dev_err(dev, "failed to map registers\n");
		return PTR_ERR(dove_lcd->mmio);
	}

	snprintf(dove_lcd->name, sizeof dove_lcd->name, "dove-lcd%d", id);

	dove_lcd->irq = irq_of_parse_and_map(np, 0);
	if (dove_lcd->irq <= 0 || dove_lcd->irq == NO_IRQ) {
		dev_err(dev, "unable to get irq lcd %d\n", id);
		return -EINVAL;
	}

#ifdef LCD_DEBUG
	device_create_file(dev, &dev_attr_lcd_read);
	device_create_file(dev, &dev_attr_lcd_write);
/*	dev_set_drvdata(dev, dove_lcd); */
#endif

	return component_add(dev, &comp_ops);
}
static int dove_lcd_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &comp_ops);
	return 0;
}

static struct of_device_id dove_lcd_of_match[] = {
	{ .compatible = "marvell,dove-lcd" },
	{ },
};
struct platform_driver dove_lcd_platform_driver = {
	.probe      = dove_lcd_probe,
	.remove     = dove_lcd_remove,
	.driver     = {
		.owner  = THIS_MODULE,
		.name   = "dove-lcd",
		.of_match_table = dove_lcd_of_match,
	},
};
