// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/vis_mask.h"
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

int main() try {
    using Word=q3mapx::VisMaskWord;
    std::mt19937_64 random(20260930);
    size_t checks=0;
    for(size_t n : {0,1,2,3,8,31,32,63,64,65,127,128,129,2048}) {
        std::vector<Word> dense(n),previous(n),portal(n),visible(n),output(n+2);
        for(size_t trial=0;trial<600;++trial) {
            for(size_t i=0;i<n;++i) {
                dense[i]=random(); previous[i]=random(); portal[i]=random(); visible[i]=random();
                if(trial%5==0 || (trial%5==1 && i!=trial%n)) dense[i]=0;
                else if(trial%5==2 && (i<n/4 || i>=3*n/4)) dense[i]=0;
                else if(trial%5==3) dense[i]=~Word{};
            }
            const auto range=q3mapx::trimVisMask(dense);
            if(range.first+range.count>n) throw std::runtime_error("Packed span exceeds input");
            const auto packed=std::span(dense).subspan(range.first,range.count);
            std::fill(output.begin(),output.end(),0xd0d0f0f0ababcdcdULL);
            const auto more=q3mapx::intersectVisMask(output.data()+1,previous.data(),portal.data(),visible.data(),
                                                     packed,range.first,n);
            Word expectedMore=0;
            for(size_t i=0;i<n;++i) {
                const Word restored=i>=range.first && i-range.first<range.count?packed[i-range.first]:0;
                if(restored!=dense[i]) throw std::runtime_error("Packed round-trip differs");
                const Word expected=previous[i]&dense[i]&portal[i];
                expectedMore|=expected&~visible[i];
                if(output[i+1]!=expected) throw std::runtime_error("Intersection or omitted-range clearing differs");
            }
            if(more!=expectedMore || output.front()!=0xd0d0f0f0ababcdcdULL || output.back()!=0xd0d0f0f0ababcdcdULL)
                throw std::runtime_error("More flag or output boundary differs");
            if(!range.count && range.first) throw std::runtime_error("Empty span is not canonical");
            if(range.count && (!packed.front() || !packed.back())) throw std::runtime_error("Span is not minimal");
            ++checks;
        }
    }
    std::cout<<checks<<" packed-mask round trips and dense intersections passed\n";
}
catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
