// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/terrain.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>

using Tri=std::array<int,3>;
static void require(bool condition) { if(!condition) { std::cerr<<"Terrain contract failed\n"; std::exit(1); } }
static std::set<Tri> triangles(const std::vector<int>& indices) {
    std::set<Tri> result;
    require(indices.size()%3==0);
    for(size_t i=0;i<indices.size();i+=3) {
        Tri tri{indices[i],indices[i+1],indices[i+2]};
        for(int v:tri) require(v>=0 && v<81);
        const int ax=tri[0]%9, ay=tri[0]/9, bx=tri[1]%9, by=tri[1]/9, cx=tri[2]%9, cy=tri[2]/9;
        require((bx-ax)*(cy-ay)-(by-ay)*(cx-ax)==1); // upward, half of a unit square
        std::sort(tri.begin(),tri.end()); require(result.insert(tri).second);
    }
    return result;
}
int main() {
    std::set<Tri> expected;
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
        const int a=y*9+x,b=a+1,c=a+10,d=a+9;
        const auto add=[&](Tri t){std::sort(t.begin(),t.end()); expected.insert(t);};
        if((x+y)&1) {add({b,c,d}); add({a,b,d});}
        else {add({a,c,d}); add({a,b,c});}
    }
    for(bool alternate:{false,true}) {
        std::array<uint16_t,126> flags{};
        require(triangles(q3mapx::mohaaTerrainTriangles(flags,alternate))==expected);
        // Known terminal-node bit locations from the native square contract.
        flags[31]=0x2000;
        auto omitted=expected; omitted.erase(alternate ? Tri{58,59,68} : Tri{38,46,47});
        require(triangles(q3mapx::mohaaTerrainTriangles(flags,alternate))==omitted);
        for(int tree=0;tree<2;++tree) for(int node=31;node<63;++node) for(uint16_t bit:{0x1000,0x2000}) {
            flags.fill(0); flags[tree*63+node]=bit;
            require(triangles(q3mapx::mohaaTerrainTriangles(flags,alternate)).size()==127);
        }
        flags.fill(0xffff);
        require(q3mapx::mohaaTerrainTriangles(flags,alternate).empty());
        for(int tree=0;tree<2;++tree) for(int node=31;node<63;++node) flags[tree*63+node]=0;
        require(triangles(q3mapx::mohaaTerrainTriangles(flags,alternate))==expected);
    }
    std::cout<<"Terrain topology, winding, native hole positions and all terminal bits passed\n";
}
