// SPDX-License-Identifier: GPL-2.0

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <internal/lib.h>

#include "util/compress.h"
#include "util/debug.h"

int zstd_init(struct zstd_data *data, int level)
{
	data->comp_level = level;
	data->dstream = NULL;
	data->cstream = NULL;
	return 0;
}

static int zstd_init_cstream(struct zstd_data *data)
{
	size_t ret;

	if (!data->cstream) {
		data->cstream = ZSTD_createCStream();
		if (data->cstream == NULL) {
			pr_err("zstd: failed to create compression stream\n");
			return -1;
		}

		ret = ZSTD_initCStream(data->cstream, data->comp_level);
		if (ZSTD_isError(ret)) {
			pr_err("zstd: failed to initialize compression stream: %s\n",
				ZSTD_getErrorName(ret));
			return -1;
		}
	}

	return 0;
}

static int zstd_init_dstream(struct zstd_data *data)
{
	size_t ret;
	
	if (!data->dstream) {
		data->dstream = ZSTD_createDStream();
		if (data->dstream == NULL) {
			pr_err("zstd: failed to create decompression stream\n");
			return -1;
		}

		ret = ZSTD_initDStream(data->dstream);
		if (ZSTD_isError(ret)) {
			pr_err("zstd: failed to initialize decompression stream: %s\n",
				ZSTD_getErrorName(ret));
			return -1;
		}
	}

	return 0;
}

int zstd_fini(struct zstd_data *data)
{
	if (data->dstream) {
		ZSTD_freeDStream(data->dstream);
		data->dstream = NULL;
	}

	if (data->cstream) {
		ZSTD_freeCStream(data->cstream);
		data->cstream = NULL;
	}

	return 0;
}

ssize_t zstd_compress_stream_to_records(struct zstd_data *data, void *dst, size_t dst_size,
				       void *src, size_t src_size, size_t max_record_size,
				       size_t process_header(void *record, size_t increment))
{
	ssize_t ret, size, compressed = 0;
	ZSTD_inBuffer input = { src, src_size, 0 };
	ZSTD_outBuffer output;
	void *record;

	ret = zstd_init_cstream(data);
	if (ret < 0)
		return ret;

	while (input.pos < input.size) {
		record = dst;
		size = process_header(record, 0);
		compressed += size;
		dst += size;
		dst_size -= size;
		output = (ZSTD_outBuffer){ dst, (dst_size > max_record_size) ?
						max_record_size : dst_size, 0 };
		ret = ZSTD_compressStream(data->cstream, &output, &input);
		ZSTD_flushStream(data->cstream, &output);
		if (ZSTD_isError(ret)) {
			pr_err("failed to compress %ld bytes: %s\n",
				(long)src_size, ZSTD_getErrorName(ret));
			memcpy(dst, src, src_size);
			return src_size;
		}
		size = output.pos;
		size = process_header(record, size);
		compressed += size;
		dst += size;
		dst_size -= size;
	}

	return compressed;
}

size_t zstd_decompress_buffer(struct zstd_data *data, void *src, size_t src_size,
			      void *dst, size_t dst_size)
{
	ssize_t ret;
	ZSTD_inBuffer input = { src, src_size, 0 };
	ZSTD_outBuffer output = { dst, dst_size, 0 };

	ret = zstd_init_dstream(data);
	if (ret < 0)
		return ret;

	while (input.pos < input.size) {
		ret = ZSTD_decompressStream(data->dstream, &output, &input);
		if (ZSTD_isError(ret)) {
			pr_err("failed to decompress (B): %zd -> %zd, dst_size %zd : %s\n",
			       src_size, output.size, dst_size, ZSTD_getErrorName(ret));
			break;
		}
		output.dst  = dst + output.pos;
		output.size = dst_size - output.pos;
	}

	return output.pos;
}

int zstd_decompress_stream(struct zstd_data *data, int input_fd, int output_fd)
{
	ssize_t ret;
	bool unflushed = false;
	char buf_in[ZSTD_DStreamInSize()];
	char buf_out[ZSTD_DStreamOutSize()];
	ZSTD_inBuffer in = {};
	ZSTD_outBuffer out = {};

	ret = zstd_init_dstream(data);
	if (ret < 0)
		return ret;

	for (;;) {
		if (in.pos == in.size) {
			/* out of input, read more */
			ret = read(input_fd, buf_in, sizeof(buf_in));
			if (ret < 0) {
				pr_err("zstd: failed to read() %zu bytes: %s\n", sizeof(buf_in), strerror(errno));
				return ret;
			}
			in = (ZSTD_inBuffer) {
				.src = &buf_in,
				.pos = 0,
				.size = ret,
			};
		}

		if (in.pos == in.size && !unflushed) {
			/* out of input, out of file, and nothing to flush, exit */
			break;
		}

		out = (ZSTD_outBuffer) {
			.dst = &buf_out,
			.pos = 0,
			.size = sizeof(buf_out),
		};
		ret = ZSTD_decompressStream(data->dstream, &out, &in);
		if (ZSTD_isError(ret)) {
			pr_err("zstd: failed to decompress %zu bytes into buffer of %zu bytes (wrote %zu): %s\n",
				in.size, out.size, out.pos, ZSTD_getErrorName(ret));
			return -1;
		}
		unflushed = ret > 0;

		ret = writen(output_fd, out.dst, out.pos);
		if (ret < 0) {
			pr_err("zstd: failed to write() %zu bytes: %s\n", out.pos, strerror(errno));
			return ret;
		}
	}

	return 0;
}

int zstd_decompress_to_fd(const char *input, int output_fd)
{
	int input_fd = -1;
	int ret;
	struct zstd_data data;

	input_fd = open(input, O_RDONLY);
	if (input_fd < 0) {
		pr_err("zstd: failed to open(%s): %s\n", input, strerror(errno));
		return -1;
	}

	ret = zstd_init(&data, ZSTD_CLEVEL_DEFAULT);
	if (ret < 0)
		goto err;

	ret = zstd_decompress_stream(&data, input_fd, output_fd);

err:
	zstd_fini(&data);
	close(input_fd);
	return ret;
}

bool zstd_is_compressed(const char *file)
{
	int fd = open(file, O_RDONLY);
	const uint32_t magic_le = htole32(ZSTD_MAGICNUMBER);
	char buf[4];
	ssize_t rc;

	if (fd < 0) {
		pr_err("zstd: failed to open(%s): %s\n", file, strerror(errno));
		return -1;
	}

	rc = read(fd, buf, sizeof(buf));
	close(fd);
	return rc == sizeof(buf) ?
	       memcmp(buf, (const char *) &magic_le, sizeof(buf)) == 0 : false;
}
