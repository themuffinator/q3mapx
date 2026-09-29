// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>

namespace q3mapx {
struct FtxView {
    unsigned width, height;
    std::span<const uint8_t> rgba;
};
// Ritual FTX: three little-endian words (width, height, flags), followed by
// tightly packed RGBA8 rows. Flag semantics do not change the stored pixels.
inline FtxView readFtx(std::span<const uint8_t> data) {
    if(data.size()<12) throw std::runtime_error("Truncated FTX header");
    const auto word=[&](size_t at) {
        return uint32_t(data[at]) | uint32_t(data[at+1])<<8
             | uint32_t(data[at+2])<<16 | uint32_t(data[at+3])<<24;
    };
    const auto width=word(0), height=word(4);
    if(width==0 || height==0 || width>8192 || height>8192)
        throw std::runtime_error("FTX dimensions must be in 1..8192");
    const uint64_t bytes=uint64_t(width)*height*4;
    if(bytes>data.size()-12) throw std::runtime_error("Truncated FTX pixel data");
    return {width,height,data.subspan(12,size_t(bytes))};
}
}
