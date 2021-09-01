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
#include <tuple>

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

        template<class F, class...Ts, std::size_t...Is>
        void for_each_in_tuple(const std::tuple<Ts...> & tuple, F func, std::index_sequence<Is...>) {
   
                return (void(func(std::get<Is>(tuple))), ...);
        }

        template<class F, class...Ts>
        void for_each_in_tuple(const std::tuple<Ts...> & tuple, F func) {
                for_each_in_tuple(tuple, func, std::make_index_sequence<sizeof...(Ts)>());
        }

        template<typename T, typename... Args>
        void parse(const T &head, const Args &... rest)
        {
                for_each_in_tuple(head, [&](const auto &x) {
                        // parse parameters for diff algorithm
                });
        }

        virtual int configure(PixelFormat inputFormat, PixelFormat outputFormat, Size inputSize, Size outputSize) = 0;

        virtual void processing(FrameBuffer *srcBuffer, FrameBuffer *dstBuffer,
                                int width, int height) = 0;

        virtual int exportBuffers(std::vector<std::unique_ptr<FrameBuffer>> *buffers,
                                  unsigned int count, int width, int height) = 0;

        virtual void start() = 0;

        virtual void stop() = 0;

        Signal<FrameBuffer *, FrameBuffer *> ispCompleted;
};

} /* namespace libcamera */

#endif /* __LIBCAMERA_INTERNAL_ISP_H__ */
