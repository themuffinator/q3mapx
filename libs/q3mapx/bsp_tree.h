// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

namespace q3mapx {
inline constexpr unsigned maxBspNodeDepth = 1024;

// Retain the same strictly interior grid boundaries, but divide their ordered
// set in half instead of peeling one boundary off at each recursion level.
inline std::optional<float> balancedBlockSplit(float lo, float hi, int size) {
    if(size<=0) return {};
    if(!std::isfinite(lo) || !std::isfinite(hi)) throw std::runtime_error("Non-finite BSP block bounds");
    if(lo>=hi) return {};
    const double first=std::floor(double(lo)/size)+1;
    const double last=std::ceil(double(hi)/size)-1;
    if(first>last) return {};
    const float split=float((first+std::floor((last-first)/2))*size);
    if(!(lo<split && split<hi))
        throw std::runtime_error("BSP block boundary is not representable; increase _blocksize");
    return split;
}

inline void requireBspNodeDepth(unsigned depth) {
    if(depth>maxBspNodeDepth) throw std::runtime_error("node depth exceeds safety limit of 1024");
}

// Check every component, including unreachable nodes and shared subtrees.
// Memoized subtree heights measure the longest path even when a child was
// visited earlier from a shorter path. The traversal itself is iterative.
template<class Nodes>
unsigned bspNodeGraphDepth(const Nodes& nodes) {
    struct Frame { std::size_t node; unsigned next=0, height=1; };
    std::vector<unsigned char> state(nodes.size(),0);
    std::vector<unsigned> height(nodes.size(),0);
    std::vector<Frame> stack;
    unsigned maximum=0;
    for(std::size_t root=0;root<nodes.size();++root) {
        if(state[root]==2) continue;
        stack.push_back({root}); state[root]=1;
        while(!stack.empty()) {
            auto& frame=stack.back();
            if(frame.next==2) {
                requireBspNodeDepth(frame.height);
                height[frame.node]=frame.height; state[frame.node]=2;
                maximum=std::max(maximum,frame.height); stack.pop_back();
                continue;
            }
            const int child=nodes[frame.node].children[frame.next];
            if(child<0) { ++frame.next; continue; }
            if(std::size_t(child)>=nodes.size()) throw std::runtime_error("out-of-range node child");
            if(state[child]==1) throw std::runtime_error("cycle in node graph");
            if(state[child]==2) {
                frame.height=std::max(frame.height,1+height[child]); ++frame.next;
            } else {
                requireBspNodeDepth(unsigned(stack.size()+1));
                state[child]=1; stack.push_back({std::size_t(child)});
            }
        }
    }
    return maximum;
}
}
