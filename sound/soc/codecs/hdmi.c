/*
 * ALSA SoC codec driver for HDMI audio codecs.
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Ricardo Neri <ricardo.neri@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */
#include <linux/module.h>
#include <sound/soc.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <sound/pcm_params.h>
#include <sound/hdmi.h>

#define DRV_NAME "hdmi-audio-codec"

struct hdmi_priv {
	struct hdmi_data hdmi_data;
	struct snd_pcm_hw_constraint_list rate_constraints;
};

static int hdmi_dev_match(struct device *dev, void *data)
{
	return !strcmp(dev_name(dev), (char *) data);
}

/* get the codec device */
static struct device *hdmi_get_cdev(struct device *dev)
{
	struct device *cdev;

	cdev = device_find_child(dev,
				 DRV_NAME,
				 hdmi_dev_match);
	if (!cdev)
		dev_err(dev, "Cannot get codec device");
	return cdev;
}

static int hdmi_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct device *cdev;
	struct hdmi_priv *priv;
	struct snd_pcm_hw_constraint_list *rate_constraints;
	int ret, max_channels, rate_mask, fmt;
	u64 formats;
	static const u32 hdmi_rates[] = {
		32000, 44100, 48000, 88200, 96000, 176400, 192000
	};

	cdev = hdmi_get_cdev(dai->dev);
	if (!cdev)
		return -ENODEV;
	priv = dev_get_drvdata(cdev);

	/* get the EDID values and the rate constraints buffer */
	ret = priv->hdmi_data.get_audio(dai->dev,
					&max_channels, &rate_mask, &fmt);
	if (ret < 0)
		goto out;				/* no screen */

	/* convert the EDID values to audio constraints */
	rate_constraints = &priv->rate_constraints;
	rate_constraints->list = hdmi_rates;
	rate_constraints->count = ARRAY_SIZE(hdmi_rates);
	rate_constraints->mask = rate_mask;
	snd_pcm_hw_constraint_list(runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   rate_constraints);

	formats = 0;
	if (fmt & 1)
		formats |= SNDRV_PCM_FMTBIT_S16_LE;
	if (fmt & 2)
		formats |= SNDRV_PCM_FMTBIT_S20_3LE;
	if (fmt & 4)
		formats |= SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S24_3LE |
			   SNDRV_PCM_FMTBIT_S32_LE;
	snd_pcm_hw_constraint_mask64(runtime,
				SNDRV_PCM_HW_PARAM_FORMAT,
				formats);

	snd_pcm_hw_constraint_minmax(runtime,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				1, max_channels);
out:
	put_device(cdev);
	return ret;
}

static int hdmi_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params,
			  struct snd_soc_dai *dai)
{
	struct device *cdev;
	struct hdmi_priv *priv;

	cdev = hdmi_get_cdev(dai->dev);
	if (!cdev)
		return -ENODEV;
	priv = dev_get_drvdata(cdev);

	priv->hdmi_data.audio_switch(dai->dev, dai->id,
				     params_rate(params),
				     params_format(params));
	put_device(cdev);
	return 0;
}

static void hdmi_shutdown(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct device *cdev;
	struct hdmi_priv *priv;

	cdev = hdmi_get_cdev(dai->dev);
	if (!cdev)
		return;
	priv = dev_get_drvdata(cdev);

	priv->hdmi_data.audio_switch(dai->dev, -1, 0, 0);	/* stop */
	put_device(cdev);
}

static const struct snd_soc_dai_ops hdmi_ops = {
	.startup = hdmi_startup,
	.hw_params = hdmi_hw_params,
	.shutdown = hdmi_shutdown,
};

static int hdmi_codec_probe(struct snd_soc_codec *codec)
{
	struct hdmi_priv *priv;
	struct device *dev = codec->dev;	/* encoder device */
	struct device *cdev;			/* codec device */

	cdev = hdmi_get_cdev(dev);
	if (!cdev)
		return -ENODEV;

	/* allocate some memory to store
	 * the encoder callback functions and the rate constraints */
	priv = devm_kzalloc(cdev, sizeof *priv, GFP_KERNEL);
	if (!priv) {
		put_device(cdev);
		return -ENOMEM;
	}
	dev_set_drvdata(cdev, priv);

	memcpy(&priv->hdmi_data, cdev->platform_data,
				sizeof priv->hdmi_data);
	put_device(cdev);
	return 0;
}

static const struct snd_soc_dapm_widget hdmi_widgets[] = {
	SND_SOC_DAPM_INPUT("RX"),
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route hdmi_routes[] = {
	{ "Capture", NULL, "RX" },
	{ "TX", NULL, "Playback" },
};

static struct snd_soc_dai_driver hdmi_codec_dai = {
	.name = "hdmi-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.sig_bits = 24,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_32000 |
			SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			SNDRV_PCM_FMTBIT_S24_LE,
	},

};

#ifdef CONFIG_OF
static const struct of_device_id hdmi_audio_codec_ids[] = {
	{ .compatible = "linux,hdmi-audio", },
	{ }
};
MODULE_DEVICE_TABLE(of, hdmi_audio_codec_ids);
#endif

static struct snd_soc_codec_driver hdmi_codec = {
	.dapm_widgets = hdmi_widgets,
	.num_dapm_widgets = ARRAY_SIZE(hdmi_widgets),
	.dapm_routes = hdmi_routes,
	.num_dapm_routes = ARRAY_SIZE(hdmi_routes),
	.ignore_pmdown_time = true,
};

static int hdmi_codec_dev_probe(struct platform_device *pdev)
{
	struct hdmi_data *pdata = pdev->dev.platform_data;
	struct snd_soc_dai_driver *dais;
	struct snd_soc_codec_driver *driver;
	int i, ret;

	if (!pdata)
		return snd_soc_register_codec(&pdev->dev, &hdmi_codec,
						&hdmi_codec_dai, 1);

	/* creation from a video encoder as a child device */
	dais = devm_kmemdup(&pdev->dev,
			    pdata->dais,
			    sizeof *pdata->dais * pdata->ndais,
			    GFP_KERNEL);
	for (i = 0; i < pdata->ndais; i++)
		dais[i].ops = &hdmi_ops;

	driver = devm_kmemdup(&pdev->dev,
			    pdata->driver,
			    sizeof *pdata->driver,
			    GFP_KERNEL);
	driver->probe = hdmi_codec_probe;

	/* register the codec on the video encoder */
	ret = snd_soc_register_codec(pdev->dev.parent, driver,
					dais, pdata->ndais);
	return ret;
}

static int hdmi_codec_dev_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver hdmi_codec_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = of_match_ptr(hdmi_audio_codec_ids),
	},

	.probe		= hdmi_codec_dev_probe,
	.remove		= hdmi_codec_dev_remove,
};

module_platform_driver(hdmi_codec_driver);

MODULE_AUTHOR("Ricardo Neri <ricardo.neri@ti.com>");
MODULE_DESCRIPTION("ASoC generic HDMI codec driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
