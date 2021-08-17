/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2021, Siyuan Fan <siyuan.fan@foxmail.com>
 *
 * isp.h - The software ISP class
 */
#ifndef __LIBCAMERA_SWISP_ISP_H__
#define __LIBCAMERA_SWISP_ISP_H__

#include <map>

#include <libcamera/formats.h>
#include <libcamera/geometry.h>
#include <libcamera/framebuffer.h>
#include <libcamera/pixel_format.h>

#include "libcamera/base/object.h"
#include "libcamera/base/signal.h"
#include "libcamera/base/thread.h"

namespace libcamera{

using std::uint16_t;
using std::uint8_t;

class ISP : public Object
{
public:
        ISP() {}

        virtual ~ISP() {}

        enum outputPixelFormat {
            RGB888,
            BGR888,
        };

        virtual outputPixelFormat getOutputPixelFormat(PixelFormat format) = 0;

        virtual void processing(FrameBuffer *srcBuffer, FrameBuffer *dstBuffer, int width, int height) = 0;

        virtual std::map<PixelFormat, std::vector<SizeRange>> pixelFormatConfiguration() = 0;

        virtual void paramConfiguration() = 0;

        virtual int exportBuffers(std::vector<std::unique_ptr<FrameBuffer>> *buffers,
                                  unsigned int count, int width, int height) = 0;

        virtual void startThreadISP() = 0;

        virtual void stopThreadISP() = 0;

        Signal<FrameBuffer *, FrameBuffer *> ispCompleted;

        std::map<PixelFormat, std::vector<SizeRange>> ispFormat;
};

class ISPCPU : public ISP
{
public:
        struct BLC_PARAM {
            uint16_t black_level;
        };

        struct LSC_PARAM {
            float bGain[12][16];
            float rGain[12][16];
        };

        outputPixelFormat getOutputPixelFormat(PixelFormat format) override;

        void processing(FrameBuffer *srcBuffer, FrameBuffer *dstBuffer, int width, int height) override;

        std::map<PixelFormat, std::vector<SizeRange>> pixelFormatConfiguration() override;

        void paramConfiguration() override;

        int exportBuffers(std::vector<std::unique_ptr<FrameBuffer>> *buffers,
                           unsigned int count, int width, int height) override;

        void startThreadISP() override;

        void stopThreadISP() override;

        enum outputPixelFormat outputpixelformat;

private:
        void autoContrast(uint16_t *data, float lowCut, float highCut, int width, int height);

        void blackLevelCorrect(uint16_t *data, uint16_t offset, int width, int height);

        void readChannels(uint16_t *data, uint16_t *R, uint16_t *G, uint16_t *B,
                          int width, int height);

        void firstPixelInsert(uint16_t *src, uint16_t *dst, int width, int height);

        void twoPixelInsert(uint16_t *src, uint16_t *dst, int width, int height);

        void lastPixelInsert(uint16_t *src, uint16_t *dst, int width, int height);

        void demosaic(uint16_t *data, uint16_t *R, uint16_t *G, uint16_t *B,
                      int width, int height);

        void autoWhiteBalance(uint16_t *R, uint16_t *G, uint16_t *B, int width, int height);

        void gammaCorrect(uint16_t *R, uint16_t *G, uint16_t *B, float val, int width, int height);

        float distance(int x, int y, int i, int j);

        double gaussian(float x, double sigma);

        void bilateralFilter(uint16_t *R, uint16_t *G, uint16_t *B,
                             int diameter, double sigmaI, double sigmaS,
                             int width, int height);

        void noiseReduction(uint16_t *R, uint16_t *G, uint16_t *B, int width, int height);

        void compressAndTransformFormat(uint16_t *src, uint8_t *dst, int width, int height);    

        Thread thread_;
};

} /* namespace libcamera */

#endif /* __LIBCAMERA_SWISP_ISP_H__ */
