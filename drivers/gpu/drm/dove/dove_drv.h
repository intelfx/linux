/*
 * Copyright (C) 2013-2014 Jean-François Moine
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

#ifndef __DOVE_DRV_H__
#define __DOVE_DRV_H__

#include <linux/clk.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>

/* (not tested) */
/*#define HANDLE_INTERLACE 1*/

#define MAX_DOVE_LCD 2		/* max number of dove lcd devices */

struct dove_drm;

struct dove_lcd {
	void __iomem *mmio;
	struct device *dev;
	struct dove_drm *dove_drm;
	struct drm_crtc crtc;

	u8 num;			/* index in dove_drm */
	u8 dpms;

#ifdef HANDLE_INTERLACE
	u32 v_sync0;
	u32 v_sync1;
	u8 vblank_enabled;
#endif

	u8 clk_src;		/* clock source index = SCLK_SRC_xxx */
	struct clk *clk;

	int irq;
	char name[16];

	struct drm_pending_vblank_event *event;

	struct drm_plane plane;	/* video plane */
};

struct dove_dcon {
	void __iomem *mmio;
	struct device *dev;
	struct dove_drm *dove_drm;
};

struct dove_drm {
	struct drm_device *drm;
	struct dove_lcd *lcds[MAX_DOVE_LCD];
	struct dove_dcon *dcon;

	struct drm_fbdev_cma *fbdev;
};

#define drm_to_dove(x) x->dev_private

u32 dove_vblank_count(struct drm_device *dev, int crtc);
int dove_enable_vblank(struct drm_device *dev, int crtc);
void dove_disable_vblank(struct drm_device *dev, int crtc);
int dove_lcd_init(struct dove_lcd *dove_lcd);
void dove_crtc_cancel_page_flip(struct dove_lcd *dove_lcd,
				struct drm_file *file);
void dove_crtc_start(struct dove_lcd *dove_lcd);
void dove_crtc_stop(struct dove_lcd *dove_lcd);
#ifdef CONFIG_DEBUG_FS
int dove_debugfs_init(struct drm_minor *minor);
void dove_debugfs_cleanup(struct drm_minor *minor);
#endif
extern struct platform_driver dove_lcd_platform_driver;

#endif /* __DOVE_DRV_H__ */
