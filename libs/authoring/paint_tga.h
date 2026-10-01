// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace q3mapx::authoring {
struct PaintTga {
    unsigned width=0,height=0;
    std::vector<unsigned char> rgba;
};
// Quake III's runtime treats TGA data as bottom-left regardless of origin bits.
// Preserve the original alpha bytes, including an entirely transparent image.
// Only bounded true-color uncompressed/RLE images are accepted here.
inline std::optional<PaintTga> decodePaintTga(std::string_view source) {
    if(source.size()<18 || source.size()>64*1024*1024) return std::nullopt;
    const auto byte=[&](std::size_t at){ return static_cast<unsigned char>(source[at]); };
    const unsigned type=byte(2),step=byte(16)/8;
    if(byte(1)!=0 || (type!=2 && type!=10) || (byte(16)!=24 && byte(16)!=32) || (byte(17)&0xc0)) return std::nullopt;
    PaintTga out;
    out.width=byte(12)+256u*byte(13); out.height=byte(14)+256u*byte(15);
    if(!out.width || !out.height || out.width>4096 || out.height>4096) return std::nullopt;
    const std::size_t count=std::size_t(out.width)*out.height;
    std::size_t at=18+byte(0),done=0;
    if(at>source.size() || (type==2 && count*step>source.size()-at)) return std::nullopt;
    out.rgba.resize(count*4);
    while(done<count) {
        unsigned length=1; bool repeat=false;
        if(type==10) {
            if(at==source.size()) return std::nullopt;
            const unsigned packet=byte(at++); length=(packet&127)+1; repeat=(packet&128)!=0;
        }
        if(length>count-done || std::size_t(repeat?1:length)*step>source.size()-at) return std::nullopt;
        std::array<unsigned char,4> color{};
        for(unsigned i=0;i<length;++i) {
            if(!repeat || i==0) {
                color={byte(at+2),byte(at+1),byte(at),step==4?byte(at+3):static_cast<unsigned char>(255)};
                at+=step;
            }
            const auto dst=((out.height-1-done/out.width)*out.width+done%out.width)*4;
            for(unsigned channel=0;channel<4;++channel) out.rgba[dst+channel]=color[channel];
            ++done;
        }
    }
    return out;
}
} // namespace q3mapx::authoring
