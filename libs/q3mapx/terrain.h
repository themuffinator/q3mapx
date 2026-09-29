// SPDX-License-Identifier: GPL-3.0-or-later
// MOHAA variance-tree observations: fnTech3/OpenMoHAA, credited in GAME-COVERAGE.md.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace q3mapx {
// Expand the fixed six-level binary triangle trees. At the last node, bits
// 0x2000/0x1000 remove its left/right child. Coordinates index a 9x9 grid.
// The two native tree orientations change the association of holes to cells.
inline std::vector<int> mohaaTerrainTriangles(std::span<const uint16_t,126> flags, bool alternate) {
    struct Point { int x,y; };
    std::vector<int> result; result.reserve(384);
    const auto emit=[&](Point a,Point b,Point c) {
        if((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)<0) std::swap(b,c);
        for(const auto p:{a,b,c}) result.push_back(p.y*9+p.x);
    };
    const auto split=[&](auto&& self,Point a,Point b,Point c,int tree,int node)->void {
        const Point middle{(a.x+b.x)/2,(a.y+b.y)/2};
        if(node<31) {
            self(self,b,c,middle,tree,node*2+1);
            self(self,c,a,middle,tree,node*2+2);
        } else {
            const auto bits=flags[tree*63+node];
            if(!(bits&0x2000)) emit(b,c,middle);
            if(!(bits&0x1000)) emit(c,a,middle);
        }
    };
    if(alternate) {
        split(split,Point{8,0},Point{0,8},Point{8,8},0,0);
        split(split,Point{0,8},Point{8,0},Point{0,0},1,0);
    } else {
        split(split,Point{8,8},Point{0,0},Point{0,8},0,0);
        split(split,Point{0,0},Point{8,8},Point{8,0},1,0);
    }
    return result;
}
}
