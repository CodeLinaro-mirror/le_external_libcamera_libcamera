/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Siyuan Fan <siyuan.fan@foxmail.com>
 *
 * isp.h - The software ISP abstract base class
 */
#ifndef __LIBCAMERA_INTERNAL_ISP_H__
#define __LIBCAMERA_INTERNAL_ISP_H__

#include <vector>
#include <memory>

#include <libcamera/formats.h>
#include <libcamera/framebuffer.h>
#include <libcamera/geometry.h>
#include <libcamera/pixel_format.h>

#include "libcamera/base/object.h"
#include "libcamera/base/signal.h"

namespace libcamera{

class ISP : public Object
{
public:
        ISP() {}

        virtual ~ISP() {}

        virtual int configure(PixelFormat &inputFormat, PixelFormat &outputFormat, Size &inputSize, Size &outputSize) = 0;

        virtual void processing(struct isp_stats *stats, FrameBuffer *srcBuffer, FrameBuffer *dstBuffer, int width, int height) = 0;

        virtual void calculating(struct isp_stats* stats, FrameBuffer *srcBuffer) = 0;

        virtual int exportBuffers(std::vector<std::unique_ptr<FrameBuffer>> *buffers, unsigned int count, int width, int height) = 0;

        virtual int exportStats(struct isp_stats *stats) = 0;

        virtual void start() = 0;

        virtual void stop() = 0;

        Signal<FrameBuffer *, FrameBuffer *> ispCompleted;
        Signal<struct isp_stats *, FrameBuffer *> calcStats;
        Signal<struct isp_stats *, FrameBuffer *, FrameBuffer *, int, int> imgProcess;

        struct isp_black_level {
            __u32 enabled;
            __u16 bl_r;
            __u16 bl_gr;
            __u16 bl_gb;
            __u16 bl_b;
        };

        struct isp_lens_shading {
            __u32 enabled;
            __u16 grid[16][16];
        };

        struct isp_denoise_raw {
            __u32 enabled;
        };

        struct isp_auto_white_balance {
            __u32 enabled;
            __u64 r_mean;
            __u64 g_mean;
            __u64 b_mean;
        };

        struct isp_gamma_correct {
            __u32 enabled;
            __u16 gammaX[33];
            __u16 gammaY[33];
        };

        struct isp_color_correct {
            __u32 enabled;
            __s32 ccm[3][3];
        };

        struct isp_histogram {
            __u32 r_hist[1024];
            __u32 g_hist[1024];
            __u32 b_hist[1024];
        };

        struct isp_stats {
            struct isp_black_level isp_bl;
            struct isp_lens_shading isp_ls;
            struct isp_denoise_raw isp_dr;
            struct isp_auto_white_balance isp_awb;
            struct isp_gamma_correct isp_gc;
            struct isp_color_correct isp_cc;
            struct isp_histogram isp_hist;
        };    
};

} /* namespace libcamera */

#endif /* __LIBCAMERA_INTERNAL_ISP_H__ */
