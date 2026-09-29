// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/ftx.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>
static void require(bool test) { if(!test) std::exit(1); }
int main() {
    std::vector<uint8_t> bytes={2,0,0,0, 1,0,0,0, 255,255,255,255, 10,20,30,40, 50,60,70,80};
    auto view=q3mapx::readFtx(bytes);
    require(view.width==2 && view.height==1 && view.rgba.size()==8 && view.rgba.data()==bytes.data()+12);
    for(size_t i=0;i<8;++i) require(view.rgba[i]==10*(i+1));
    for(size_t size=0;size<bytes.size();++size) {
        try { q3mapx::readFtx({bytes.data(),size}); require(false); }
        catch(const std::runtime_error&) {}
    }
    for(const uint32_t dimension : {0u,8193u,0x80000000u,0xffffffffu}) {
        auto bad=bytes;
        for(int i=0;i<4;++i) bad[i]=uint8_t(dimension>>(i*8));
        try { q3mapx::readFtx(bad); require(false); }
        catch(const std::runtime_error&) {}
    }
    bytes.push_back(123); require(q3mapx::readFtx(bytes).rgba.size()==8);
    std::cout << "FTX pixels, alpha, flags, bounds and truncated data passed\n";
}
