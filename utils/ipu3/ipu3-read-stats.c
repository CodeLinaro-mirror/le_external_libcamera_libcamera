/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ipu3-read-stats - Dump IPU3 statistics
 *
 * Copyright 2022 Umang Jain <umang.jain@ideasonboard.com>
 */
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../include/linux/intel-ipu3.h"

static void usage(const char *argv0)
{
	printf("Utility for dumping IPU3 statistics\n");
	printf("Usage: %s --if <size> --bds <size> --gdc <size> input-file output-file\n",
	       basename(argv0));
	printf("<size> widthxheight\n");
	printf("If the output-file '-', output data will be written to standard output\n");
}

void readSize(const char *size, unsigned int *w, unsigned int *h)
{
	char *sz = strdup(size);
	const char *delim = "x";
	char *token = strtok(sz, delim);
	if (token)
		*w = atoi(token);

	token = strtok(NULL, delim);
	if (token)
		*h = atoi(token);
}

int main(int argc, char *argv[])
{
	int in_fd;
	FILE *fout;
	int ret;
	unsigned int bds_w, bds_h = 0;
	unsigned int iif_w, iif_h = 0;
	unsigned int gdc_w, gdc_h = 0;

	if (argc != 9) {
		usage(argv[0]);
		return 1;
	}

	if (strcmp(argv[1], "--if") == 0) {
		readSize(argv[2], &iif_w, &iif_h);
	}

	if (strcmp(argv[3], "--bds") == 0) {
		readSize(argv[4], &bds_w, &bds_h);
	}

	if (strcmp(argv[5], "--gdc") == 0) {
		readSize(argv[6], &gdc_w, &gdc_h);
	}

	if (!bds_w || !bds_h || !iif_w || !iif_h || !gdc_w || !gdc_h) {
		usage(argv[0]);
		return 1;
	}

	in_fd = open(argv[7], O_RDONLY);
	if (in_fd == -1) {
		fprintf(stderr, "Failed to open statistics file '%s': %s\n",
			argv[1], strerror(errno));
		return 1;
	}

	if (strcmp(argv[8], "-") == 0) {
		fout = stdout;
	} else {
		fout = fopen(argv[8], "w");
		if (!fout) {
			fprintf(stderr, "Failed to open output file '%s': %s\n",
				argv[4], strerror(errno));
			close(in_fd);
			return 1;
		}
	}

	int frame = 0;
	struct ipu3_uapi_stats_3a *stats = (struct ipu3_uapi_stats_3a *) malloc(sizeof(struct ipu3_uapi_stats_3a));

	while (1) {
		ret = read(in_fd, stats, sizeof(struct ipu3_uapi_stats_3a));
		if (ret < 0) {
			fprintf(stderr, "Failed to read stats data: %s\n",
				strerror(errno));
			goto done;
		}

		if ((unsigned)ret < sizeof(struct ipu3_uapi_stats_3a)) {
			if (ret != 0)
				fprintf(stderr, "%u bytes of stray data at end of input\n",
					ret);
			goto done;
		}

		/* ================ Dump AWB stats ================ */

		uint32_t k_min_grid_w = 16;
		uint32_t k_max_grid_w = 80;
		uint32_t minError = UINT_MAX;
		uint32_t best_log2 = 0;
		uint32_t stride = 0;

		struct ipu3_uapi_grid_config grid = stats->stats_4a_config.awb_config.grid;

		for (uint32_t shift = grid.block_width_log2; shift <= 6; ++shift) {
			uint32_t width = bds_w >> shift;
			if (width < k_min_grid_w)
				width = k_min_grid_w;
			else if (width > k_max_grid_w)
				width = k_max_grid_w;

			width = width << shift;
			uint32_t error = bds_w > width ? bds_w - width : width - bds_w;
			if (error >= minError)
				continue;

			minError = error;
			stride = width;
			best_log2 = shift;
		}

		stride = stride >> best_log2;

		/* Sum the per-channel averages */
		double red_sum = 0, green_sum = 0, blue_sum = 0;
		for (unsigned int cellY = 0; cellY < grid.height; cellY++) {
			for (unsigned int cellX = 0; cellX < grid.width; cellX++) {
				uint32_t cell_pos = cellY * stride + cellX;

				const struct ipu3_uapi_awb_set_item *cell =
					(const struct ipu3_uapi_awb_set_item *)(
						&stats->awb_raw_buffer.meta_data[cell_pos]
					);
				const uint8_t G_avg = (cell->Gr_avg + cell->Gb_avg) / 2;

				red_sum += cell->R_avg;
				green_sum += G_avg;
				blue_sum += cell->B_avg;
			}
		}

		fprintf(fout, "Frame: %d, redSum: %.1f, greenSum: %.1f, blueSum: %.1f\n",
			frame, red_sum, green_sum, blue_sum);
		frame++;
	}

done:
	free(stats);
	close(in_fd);
	fclose(fout);

	return ret ? 1 : 0;
}
