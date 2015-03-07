#ifndef SND_HDMI_H
#define SND_HDMI_H

#include <sound/soc.h>

/* platform_data */
struct hdmi_data {
	int (*get_audio)(struct device *dev,
			 int *max_channels,
			 int *rate_mask,
			 int *fmt);
	void (*audio_switch)(struct device *dev,
			     int port_index,
			     unsigned sample_rate,
			     int sample_format);
	int ndais;
	struct snd_soc_dai_driver *dais;
	struct snd_soc_codec_driver *driver;
};
#endif
