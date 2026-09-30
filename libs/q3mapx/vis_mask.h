// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace q3mapx {
using VisMaskWord = std::uint64_t;
struct VisMaskRange { size_t first = 0, count = 0; };

inline VisMaskRange trimVisMask(std::span<const VisMaskWord> words) {
    size_t first=0, end=words.size();
    while(first<end && !words[first]) ++first;
    if(first==end) return {};
    while(!words[end-1]) --end;
    return {first,end-first};
}

// Internal packed-mask contract: first + packed.size() <= totalWords; all full
// arrays contain totalWords words, and output does not alias any input. Clear
// both omitted ranges: flow-stack storage is reused for unrelated passages.
inline VisMaskWord intersectVisMask(VisMaskWord* output, const VisMaskWord* previous,
        const VisMaskWord* portal, const VisMaskWord* visible,
        std::span<const VisMaskWord> packed, size_t first, size_t totalWords) {
    if(!totalWords) return 0;
    std::fill_n(output,first,VisMaskWord{});
    VisMaskWord more=0;
    for(size_t i=0;i<packed.size();++i) {
        const size_t j=first+i;
        output[j]=previous[j]&packed[i]&portal[j];
        more|=output[j]&~visible[j];
    }
    std::fill(output+first+packed.size(),output+totalWords,VisMaskWord{});
    return more;
}
}
