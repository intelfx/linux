/*
 * Marvell Dove DRM driver - main
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
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/component.h>

#include "dove_drv.h"

#define DRIVER_NAME	"dove-drm"
#define DRIVER_DESC	"Marvell Dove DRM"
#define DRIVER_DATE	"20140204"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct drm_framebuffer *dove_fb_create(struct drm_device *drm,
					struct drm_file *file_priv,
					struct drm_mode_fb_cmd2 *mode_cmd)
{
	DRM_DEBUG_DRIVER("%.4s %dx%d\n",
			(char *) &mode_cmd->pixel_format,
			mode_cmd->width, mode_cmd->height);

	switch (mode_cmd->pixel_format) {
	case DRM_FORMAT_BGR888:
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_XBGR8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ABGR8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_YVU422:
	case DRM_FORMAT_YUV422:
	case DRM_FORMAT_YVU420:
	case DRM_FORMAT_YUV420:
		break;
	default:
		return ERR_PTR(-EINVAL);
	}
	return drm_fb_cma_create(drm, file_priv, mode_cmd);
}

static void dove_fb_output_poll_changed(struct drm_device *drm)
{
	struct dove_drm *dove_drm = drm_to_dove(drm);

	DRM_DEBUG_DRIVER("fb:%d\n", dove_drm->fbdev != NULL);
//	if (dove_drm->fbdev)
		drm_fbdev_cma_hotplug_event(dove_drm->fbdev);
}

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = dove_fb_create,
	.output_poll_changed = dove_fb_output_poll_changed,
};

/*
 * DRM operations:
 */
static int dove_unload(struct drm_device *drm)
{
	struct dove_drm *dove_drm = drm_to_dove(drm);
	struct dove_lcd *dove_lcd;
	int i;

	DRM_DEBUG_DRIVER("\n");

	for (i = 0; i < MAX_DOVE_LCD; i++) {
		dove_lcd = dove_drm->lcds[i];
		if (dove_lcd)
			drm_plane_cleanup(&dove_lcd->plane);
	}
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);
	drm_vblank_cleanup(drm);

	return 0;
}

static int dove_load(struct drm_device *drm, unsigned long flags)
{
	struct platform_device *pdev = drm->platformdev;
	struct dove_drm *dove_drm;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	drm_mode_config_init(drm);

/*	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32); */

	dove_drm = devm_kzalloc(&pdev->dev, sizeof *dove_drm, GFP_KERNEL);
	if (!dove_drm) {
		dev_err(&pdev->dev, "failed to allocate dove drm\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, dove_drm);

	dove_drm->drm = drm;
	drm->dev_private = dove_drm;

	/* initialize the subdevices */
	ret = component_bind_all(&pdev->dev, drm);
	if (ret < 0)
		goto fail;
//	if (dove_drm->dcon)
//		dove_drm->dcon->dove_drm = dove_drm;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 2048;
	drm->mode_config.max_height = 2048;
	drm->mode_config.funcs = &mode_config_funcs;

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(drm->dev, "failed to initialize vblank\n");
		goto fail;
	}

	dove_drm->fbdev = drm_fbdev_cma_init(drm,
					32,	/* bpp */
					drm->mode_config.num_crtc,
					drm->mode_config.num_connector);

	drm_kms_helper_poll_init(drm);
	return 0;
fail:
	dove_unload(drm);
	return ret;
}

static void dove_preclose(struct drm_device *drm, struct drm_file *file)
{
	struct dove_drm *dove_drm = drm_to_dove(drm);
	struct dove_lcd *dove_lcd;
	int i;

// is the crtc list usable here?
	for (i = 0; i < MAX_DOVE_LCD; i++) {
		dove_lcd = dove_drm->lcds[i];
		if (dove_lcd)
			dove_crtc_cancel_page_flip(dove_lcd, file);
	}
}

static void dove_lastclose(struct drm_device *drm)
{
	struct dove_drm *dove_drm = drm_to_dove(drm);

	drm_fbdev_cma_restore_mode(dove_drm->fbdev);
}

static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap		= drm_gem_cma_mmap,
};

static struct drm_driver dove_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME,
	.load			= dove_load,
	.unload			= dove_unload,
	.preclose		= dove_preclose,
	.lastclose		= dove_lastclose,
	.get_vblank_counter	= dove_vblank_count,
	.enable_vblank		= dove_enable_vblank,
	.disable_vblank		= dove_disable_vblank,
	.gem_free_object	= drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= dove_debugfs_init,
	.debugfs_cleanup	= dove_debugfs_cleanup,
#endif
	.fops			= &fops,

	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= DRIVER_DATE,
	.major			= DRIVER_MAJOR,
	.minor			= DRIVER_MINOR,
};

#ifdef CONFIG_PM_SLEEP
/*
 * Power management
 */
static int dove_pm_suspend(struct device *dev)
{
	struct dove_drm *dove_drm = dev_get_drvdata(dev);
	struct dove_lcd *dove_lcd;
	int i;

	drm_kms_helper_poll_disable(&dove_drm->drm);
	for (i = 0; i < MAX_DOVE_LCD; i++) {
		dove_lcd = dove_drm->lcds[i];
		if (dove_lcd)
			dove_crtc_stop(dove_lcd);
	}
	return 0;
}

static int dove_pm_resume(struct device *dev)
{
	struct dove_drm *dove_drm = dev_get_drvdata(dev);
	struct dove_lcd *dove_lcd;
	int i;

	for (i = 0; i < MAX_DOVE_LCD; i++) {
		dove_lcd = dove_drm->lcds[i];
		if (dove_lcd
		 && dove_lcd->dpms == DRM_MODE_DPMS_ON)
			dove_crtc_start(dove_lcd);
	}
	drm_kms_helper_poll_enable(&dove_drm->drm);
	return 0;
}
#endif

static const struct dev_pm_ops dove_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dove_pm_suspend, dove_pm_resume)
};

/*
 * Platform driver
 */

/* component stuff */
static int of_dev_node_match(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int dove_drm_add_components(struct device *master, struct master *m)
{
	struct device_node *node, *child, *np = master->of_node;
	int i, ret;

	/* search the video devices */
	for (i = 0; ; i++) {
		node = of_parse_phandle(np, "marvell,video-devices", i);
		if (!node)
			return 0;	/* all devices are found */

		ret = component_master_add_child(m, of_dev_node_match,
						 node);
		if (ret)
			goto err;

		/* search the encoders/connectors as child "port" */
		child = NULL;
		for (;;) {
			struct device_node *endpoint, *port, *i2c_node;

			child = of_get_next_child(node, child);
			if (!child)
				break;
			if (strcmp(child->name, "port") != 0)
				continue;

			endpoint = of_get_next_child(child, NULL);
			if (!endpoint) {
				dev_err(master,
					"dove drm: no port description\n");
				ret = -EINVAL;
				goto err;
			}
			port = of_parse_phandle(endpoint,
						"remote-endpoint", 0);
			of_node_put(endpoint);
			if (!port) {
				dev_err(master,
					"dove drm: no remote-endpoint\n");
				ret = -EINVAL;
				goto err;
			}
			i2c_node = of_get_parent(of_get_parent(port));
			of_node_put(port);
			ret = component_master_add_child(m, of_dev_node_match,
							 i2c_node);
			of_node_put(i2c_node);
			if (ret)
				goto err;
		}
		of_node_put(node);
	}

err:
	of_node_put(node);
	return ret;
}
static int dove_drm_bind(struct device *dev)
{
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&dove_driver, &to_platform_device(dev)->dev);
	if (!drm)
        	return -ENOMEM;
	drm->platformdev = to_platform_device(dev);

	ret = drm_dev_set_unique(drm, dev_name(drm->dev));
	if (ret < 0) {
        	drm_dev_unref(drm);
	        return ret;
	}

	ret = drm_dev_register(drm, 0);
	if (ret < 0) {
	        drm_dev_unref(drm);
	        return ret;
	}
	return ret;
}

static void dove_drm_unbind(struct device *dev)
{
	struct dove_drm *dove_drm = dev_get_drvdata(dev);

	drm_dev_unregister(dove_drm->drm);
	drm_dev_unref(dove_drm->drm);
}

static const struct component_master_ops dove_drm_comp_ops = {
	.add_components = dove_drm_add_components,
	.bind = dove_drm_bind,
	.unbind = dove_drm_unbind,
};

static int dove_pdev_probe(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("\n");

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "no device-tree\n");
		return -ENXIO;
	}

	return component_master_add(&pdev->dev, &dove_drm_comp_ops);
}

static int dove_pdev_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &dove_drm_comp_ops);
	return 0;
}

static struct of_device_id dove_of_match[] = {
	{ .compatible = "marvell,dove-video" },
	{ },
};
MODULE_DEVICE_TABLE(of, dove_of_match);

static struct platform_driver dove_platform_driver = {
	.probe      = dove_pdev_probe,
	.remove     = dove_pdev_remove,
	.driver     = {
		.owner  = THIS_MODULE,
		.name   = "dove-drm",
		.pm     = &dove_pm_ops,
		.of_match_table = dove_of_match,
	},
};

static int __init dove_drm_init(void)
{
	int ret;

	/* wait for other drivers to be loaded (si5351) */
	msleep(200);

/* uncomment to activate the drm trace at startup time */
/*	drm_debug = DRM_UT_CORE | DRM_UT_DRIVER | DRM_UT_KMS; */

	DRM_DEBUG_DRIVER("\n");

	ret = platform_driver_register(&dove_lcd_platform_driver);
	if (ret < 0)
		return ret;
//	ret = platform_driver_register(&dove_dcon_platform_driver);
//	if (ret < 0)
//		goto out1;
	ret = platform_driver_register(&dove_platform_driver);
	if (ret < 0)
		goto out2;
	return 0;

out2:
//	platform_driver_unregister(&dove_dcon_platform_driver);
//out1:
	platform_driver_unregister(&dove_lcd_platform_driver);
	return ret;
}
static void __exit dove_drm_fini(void)
{
	platform_driver_unregister(&dove_platform_driver);
//	platform_driver_unregister(&dove_dcon_platform_driver);
	platform_driver_unregister(&dove_lcd_platform_driver);
}
module_init(dove_drm_init);
module_exit(dove_drm_fini);

MODULE_AUTHOR("Jean-Francois Moine <moinejf@free.fr>");
MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>");
MODULE_DESCRIPTION("Marvell Dove DRM Driver");
MODULE_LICENSE("GPL");
