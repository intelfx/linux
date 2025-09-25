// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for the RP1 ADC and temperature sensor
 *
 * Copyright (C) 2023 Raspberry Pi Ltd.
 * Copyright (c) 2025, SUSE.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>

#define RP1_ADC_CS			0x00
#define RP1_ADC_RESULT			0x04
#define RP1_ADC_FCS			0x08
#define RP1_ADC_FIFO			0x0c
#define RP1_ADC_DIV			0x10
#define RP1_ADC_INTR			0x14
#define RP1_ADC_INTE			0x18
#define RP1_ADC_INTF			0x1c
#define RP1_ADC_INTS			0x20

#define RP1_ADC_RWTYPE_SET		0x2000
#define RP1_ADC_RWTYPE_CLR		0x3000

#define RP1_ADC_CS_RROBIN_MASK		0x1f
#define RP1_ADC_CS_RROBIN_SHIFT		16
#define RP1_ADC_CS_AINSEL_MASK		0x7
#define RP1_ADC_CS_AINSEL_SHIFT		12
#define RP1_ADC_CS_ERR_STICKY		0x400
#define RP1_ADC_CS_ERR			0x200
#define RP1_ADC_CS_READY		0x100
#define RP1_ADC_CS_START_MANY		0x8
#define RP1_ADC_CS_START_ONCE		0x4
#define RP1_ADC_CS_TS_EN		0x2
#define RP1_ADC_CS_EN			0x1

#define RP1_ADC_FCS_THRESH_MASK		0xf
#define RP1_ADC_FCS_THRESH_SHIFT	24
#define RP1_ADC_FCS_LEVEL_MASK		0xf
#define RP1_ADC_FCS_LEVEL_SHIFT		16
#define RP1_ADC_FCS_OVER		0x800
#define RP1_ADC_FCS_UNDER		0x400
#define RP1_ADC_FCS_FULL		0x200
#define RP1_ADC_FCS_EMPTY		0x100
#define RP1_ADC_FCS_DREQ_EN		0x8
#define RP1_ADC_FCS_ERR			0x4
#define RP1_ADC_FCS_SHIFR		0x2
#define RP1_ADC_FCS_EN			0x1

#define RP1_ADC_FIFO_ERR		0x8000
#define RP1_ADC_FIFO_VAL_MASK		0xfff

#define RP1_ADC_DIV_INT_MASK		0xffff
#define RP1_ADC_DIV_INT_SHIFT		8
#define RP1_ADC_DIV_FRAC_MASK		0xff
#define RP1_ADC_DIV_FRAC_SHIFT		0

#define RP1_ADC_TEMP_CHAN		4

struct rp1_adc_data {
	void __iomem		*base;
	struct mutex		lock;
	struct device		*hwmon;
	int			vref_mv;
	struct clk		*clk;
	struct regulator	*reg;
};

static int rp1_adc_read(struct rp1_adc_data *rp1, int channel, long *val)
{
	u32 regval;
	int ret;

	writel(RP1_ADC_CS_AINSEL_MASK << RP1_ADC_CS_AINSEL_SHIFT,
	       rp1->base + RP1_ADC_RWTYPE_CLR + RP1_ADC_CS);
	writel(channel << RP1_ADC_CS_AINSEL_SHIFT,
	       rp1->base + RP1_ADC_RWTYPE_SET + RP1_ADC_CS);
	writel(RP1_ADC_CS_START_ONCE,
	       rp1->base + RP1_ADC_RWTYPE_SET + RP1_ADC_CS);

	ret = readl_poll_timeout(rp1->base + RP1_ADC_CS, regval,
				 regval & RP1_ADC_CS_READY, 10, 1000);
	if (ret)
		return ret;

	/* Asserted if the completed conversion had a convergence error */
	if (readl(rp1->base + RP1_ADC_CS) & RP1_ADC_CS_ERR)
		return -EIO;

	*val = readl(rp1->base + RP1_ADC_RESULT);

	return 0;
}

static int rp1_adc_read_temp(struct rp1_adc_data *rp1, long *val)
{
	int ret, mv;

	writel(RP1_ADC_CS_TS_EN, rp1->base + RP1_ADC_RWTYPE_SET + RP1_ADC_CS);

	ret = rp1_adc_read(rp1, RP1_ADC_TEMP_CHAN, val);
	if (ret)
		return ret;

	mv = ((u64)rp1->vref_mv * *val) / 4095;

	/* T = 27 - (ADC_voltage - 0.706)/0.001721 */
	*val = 27000 - DIV_ROUND_CLOSEST((mv - 706) * (s64)1000000, 1721);

	return 0;
}

static umode_t rp1_adc_hwmon_is_visible(const void *drvdata,
					enum hwmon_sensor_types type,
					u32 attr, int channel)
{
	return 0444;
}

static int rp1_adc_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	struct rp1_adc_data *rp1 = dev_get_drvdata(dev);
	int ret = -EOPNOTSUPP;

	mutex_lock(&rp1->lock);

	if (type == hwmon_temp && attr == hwmon_temp_input)
		ret = rp1_adc_read_temp(rp1, val);
	else if (type == hwmon_in && attr == hwmon_in_input)
		ret = rp1_adc_read(rp1, channel, val);

	mutex_unlock(&rp1->lock);

	return ret;
}

static const char *const rp1_adc_hwmon_in_labels[] = {
	"ain0", "ain1", "ain2", "ain3",
};

static const char *const rp1_adc_hwmon_temp_label = {
	"RP1 die temp"
};

static int rp1_adc_hwmon_read_string(struct device *dev,
				     enum hwmon_sensor_types type, u32 attr,
				     int channel, const char **str)
{
	if (type == hwmon_temp && attr == hwmon_temp_label) {
		*str = rp1_adc_hwmon_temp_label;
	} else if (type == hwmon_in && attr == hwmon_in_label) {
		if (channel < ARRAY_SIZE(rp1_adc_hwmon_in_labels))
			*str = rp1_adc_hwmon_in_labels[channel];
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_ops rp1_adc_hwmon_ops = {
	.is_visible	= rp1_adc_hwmon_is_visible,
	.read		= rp1_adc_hwmon_read,
	.read_string	= rp1_adc_hwmon_read_string,
};

static const struct hwmon_channel_info * const rp1_adc_hwmon_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL,
			       HWMON_I_INPUT | HWMON_I_LABEL,
			       HWMON_I_INPUT | HWMON_I_LABEL,
			       HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_chip_info rp1_adc_chip_info = {
	.ops	= &rp1_adc_hwmon_ops,
	.info	= rp1_adc_hwmon_info,
};

static int rp1_adc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rp1_adc_data *rp1;
	int ret;

	rp1 = devm_kzalloc(dev, sizeof(*rp1), GFP_KERNEL);
	if (!rp1)
		return -ENOMEM;

	rp1->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rp1->base))
		return dev_err_probe(dev, PTR_ERR(rp1->base), "can't ioremap resource\n");

	rp1->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(rp1->clk))
		return dev_err_probe(dev, PTR_ERR(rp1->clk), "can't get clock\n");

	rp1->reg = devm_regulator_get(dev, "vref");
	if (IS_ERR(rp1->reg))
		return dev_err_probe(dev, PTR_ERR(rp1->reg), "can't get regulator\n");

	ret = devm_mutex_init(dev, &rp1->lock);
	if (ret)
		return ret;

	ret = clk_set_rate(rp1->clk, 50000000);
	if (ret)
		return ret;

	ret = clk_prepare_enable(rp1->clk);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, rp1);

	ret = regulator_get_voltage(rp1->reg);
	if (ret < 0)
		return ret;

	rp1->vref_mv = DIV_ROUND_CLOSEST(ret, 1000);

	ret = regulator_enable(rp1->reg);
	if (ret)
		return ret;

	rp1->hwmon = devm_hwmon_device_register_with_info(dev, "rp1_adc", rp1,
							  &rp1_adc_chip_info, NULL);
	if (IS_ERR(rp1->hwmon))
		return PTR_ERR(rp1->hwmon);

	/* Disable interrupts */
	writel(0, rp1->base + RP1_ADC_INTE);

	/* Enable the block, clearing any sticky error */
	writel(RP1_ADC_CS_EN | RP1_ADC_CS_ERR_STICKY, rp1->base + RP1_ADC_CS);

	return 0;
}

static void rp1_adc_remove(struct platform_device *pdev)
{
	struct rp1_adc_data *rp1 = platform_get_drvdata(pdev);

	clk_disable_unprepare(rp1->clk);
	regulator_disable(rp1->reg);
}

static int rp1_adc_suspend(struct device *dev)
{
	struct rp1_adc_data *rp1 = dev_get_drvdata(dev);

	clk_disable_unprepare(rp1->clk);
	return regulator_disable(rp1->reg);
}

static int rp1_adc_resume(struct device *dev)
{
	struct rp1_adc_data *rp1 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_enable(rp1->reg);
	if (ret)
		return ret;

	return clk_prepare_enable(rp1->clk);
}

static DEFINE_SIMPLE_DEV_PM_OPS(rp1_adc_pm_ops, rp1_adc_suspend, rp1_adc_resume);

static const struct of_device_id rp1_adc_match_table[] = {
	{ .compatible = "raspberrypi,rp1-adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rp1_adc_match_table);

static struct platform_driver rp1_adc_driver = {
	.probe = rp1_adc_probe,
	.remove = rp1_adc_remove,
	.driver = {
		.name = "rp1-adc",
		.of_match_table = rp1_adc_match_table,
		.pm = pm_ptr(&rp1_adc_pm_ops),
	},
};
module_platform_driver(rp1_adc_driver);

MODULE_DESCRIPTION("RP1 ADC driver");
MODULE_AUTHOR("Phil Elwell <phil@raspberrypi.com>");
MODULE_AUTHOR("Stanimir Varbanov <svarbanov@suse.de>");
MODULE_LICENSE("GPL");
