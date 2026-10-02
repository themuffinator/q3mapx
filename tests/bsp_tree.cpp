// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/bsp_tree.h"
#include <array>
#include <bit>
#include <functional>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <set>

struct Node { std::array<int,2> children{-1,-1}; };
void require(bool ok) { if(!ok) throw std::runtime_error("BSP tree oracle mismatch"); }
template<class Fn> void rejects(Fn fn) {
    bool rejected=false; try { fn(); } catch(const std::runtime_error&) { rejected=true; }
    require(rejected);
}
int main() try {
    unsigned cuts=0,graphs=0;
    for(int size:{1,3,8,64,1024}) for(int lo=-17;lo<=17;++lo) for(int span:{0,1,2,3,4,7,32,1024}) {
        const float a=float(lo*size)-0.25f,b=a+float(span*size)+0.5f;
        std::set<float> expected,observed;
        for(int i=lo-1;i<=lo+span+1;++i) if(a<float(i*size) && float(i*size)<b) expected.insert(float(i*size));
        unsigned maximum=0;
        std::function<void(float,float,unsigned)> visit=[&](float first,float last,unsigned depth) {
            const auto split=q3mapx::balancedBlockSplit(first,last,size);
            if(!split) return;
            require(first<*split && *split<last && observed.insert(*split).second);
            maximum=std::max(maximum,depth+1); visit(first,*split,depth+1); visit(*split,last,depth+1);
        };
        visit(a,b,0); require(expected==observed); require(maximum==std::bit_width(expected.size())); ++cuts;
    }
    for(int size:{1,8,1024}) {
        require(!q3mapx::balancedBlockSplit(0,float(size),size));
        require(q3mapx::balancedBlockSplit(-float(size),float(size),size)==0.f);
    }
    require(!q3mapx::balancedBlockSplit(-100,100,0));
    require(!q3mapx::balancedBlockSplit(-100,100,-1));
    rejects([]{q3mapx::balancedBlockSplit(0,std::numeric_limits<float>::infinity(),1);});
    rejects([]{q3mapx::balancedBlockSplit(16777216,16777218,1);});
    q3mapx::requireBspNodeDepth(1024); rejects([]{q3mapx::requireBspNodeDepth(1025);});
    require(q3mapx::bspNodeGraphDepth(std::vector<Node>{})==0);
    for(bool reverse:{false,true}) for(int size:{1,1024,1025,65536}) {
        std::vector<Node> nodes(size);
        for(int i=0;i<size-1;++i) nodes[reverse?i+1:i].children[0]=reverse?i:i+1;
        if(size<=1024) require(q3mapx::bspNodeGraphDepth(nodes)==unsigned(size));
        else rejects([&]{q3mapx::bspNodeGraphDepth(nodes);});
        ++graphs;
    }
    std::vector<Node> shared(1025);
    for(int i=0;i<1024;++i) shared[i].children[1]=i+1;
    shared[0].children[0]=600; rejects([&]{q3mapx::bspNodeGraphDepth(shared);}); ++graphs;
    std::vector<Node> cycle(3); cycle[1].children[0]=2; cycle[2].children[1]=1;
    rejects([&]{q3mapx::bspNodeGraphDepth(cycle);});
    cycle[2].children[1]=3; rejects([&]{q3mapx::bspNodeGraphDepth(cycle);});
    std::mt19937 random(819);
    for(int n=1;n<=80;++n) for(int run=0;run<10;++run) {
        std::vector<Node> nodes(n); std::vector<int> order(n); std::iota(order.begin(),order.end(),0);
        std::shuffle(order.begin(),order.end(),random);
        for(int i=0;i<n-1;++i) for(int side=0;side<2;++side)
            if(random()%3) nodes[order[i]].children[side]=order[i+1+random()%(n-i-1)];
        // Independent relaxation propagates path lengths upward without DFS.
        std::vector<unsigned> heights(n,1);
        for(int pass=0;pass<n;++pass) for(int i=0;i<n;++i) for(int child:nodes[i].children)
            if(child>=0) heights[i]=std::max(heights[i],heights[child]+1);
        require(q3mapx::bspNodeGraphDepth(nodes)==*std::max_element(heights.begin(),heights.end())); ++graphs;
    }
    std::cout<<cuts<<" independently enumerated block partitions and "<<graphs<<" graph-depth controls passed\n";
}
catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
