// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/cell_graph.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <string>

using namespace q3mapx;
static void require(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
static bool near(double a,double b) { return std::abs(a-b)<=1e-7*std::max({1.0,std::abs(a),std::abs(b)}); }
struct Box { CellPoint lo,hi; };

int main() try {
    const CellPoint lo{-128,-128,-128},hi{128,128,128};
    std::mt19937 random(20261001);
    size_t checks=0;
    for(int trial=0;trial<24;++trial) {
        std::vector<CellNode> nodes; std::vector<int> clusters; std::vector<Box> boxes;
        const auto generate=[&](auto&& self,Box box,int depth)->int {
            if(depth==6) {
                const int leaf=int(clusters.size()); clusters.push_back(trial%3==0 && leaf%4==0?-1:leaf);
                boxes.push_back(box); return -1-leaf;
            }
            const size_t axis=random()%3; const double split=(box.lo[axis]+box.hi[axis])/2;
            const int node=int(nodes.size()); nodes.push_back({{{},split},{}}); nodes[node].plane.normal[axis]=1;
            auto front=box,back=box; front.lo[axis]=split; back.hi[axis]=split;
            const int a=self(self,front,depth+1),b=self(self,back,depth+1);
            nodes[node].children={a,b}; return node;
        };
        const int head=generate(generate,{lo,hi},0);
        const auto graph=reconstructCellGraph(nodes,clusters,head,lo,hi);
        require(std::string(graph.status)=="reconstructed" && graph.cells.size()==boxes.size(),"Grid cells missing");
        require(graph.degenerateFragments==0 && near(graph.cellVolume,256.*256*256),"Grid volume/degeneracy differs");
        std::map<std::pair<int,int>,double> expected,actual;
        for(size_t a=0;a<boxes.size();++a) for(size_t b=a+1;b<boxes.size();++b) {
            if(clusters[a]<0 && clusters[b]<0) continue;
            for(size_t axis=0;axis<3;++axis) {
                if(boxes[a].lo[axis]!=boxes[b].hi[axis] && boxes[a].hi[axis]!=boxes[b].lo[axis]) continue;
                double overlap=1;
                for(size_t k=0;k<3;++k) if(k!=axis)
                    overlap*=std::max(0.0,std::min(boxes[a].hi[k],boxes[b].hi[k])-std::max(boxes[a].lo[k],boxes[b].lo[k]));
                if(overlap>0) expected[{int(a),int(b)}]+=overlap;
            }
        }
        for(const auto& cell:graph.cells) {
            const auto& box=boxes.at(cell.leaf); double volume=1;
            for(size_t axis=0;axis<3;++axis) {
                require(near(cell.mins[axis],box.lo[axis]) && near(cell.maxs[axis],box.hi[axis]),"Cell bounds differ from box oracle");
                require(cell.center[axis]>box.lo[axis] && cell.center[axis]<box.hi[axis],"Interior point is not strict");
                volume*=box.hi[axis]-box.lo[axis];
            }
            require(near(volume,cell.volume) && cell.cluster==clusters[cell.leaf],"Cell volume/cluster differs");
        }
        for(const auto& face:graph.interfaces) {
            const int a=graph.cells.at(face.front).leaf,b=graph.cells.at(face.back).leaf;
            actual[std::minmax(a,b)]+=face.area;
            for(const auto& p:face.points) for(const auto& box:{boxes[a],boxes[b]}) for(size_t axis=0;axis<3;++axis)
                require(p[axis]>=box.lo[axis]-1e-8 && p[axis]<=box.hi[axis]+1e-8,"Interface leaves one of its cells");
        }
        require(actual.size()==expected.size(),"Interface set differs from independent box-face adjacency");
        for(const auto& [pair,area]:expected) { require(actual.contains(pair) && near(actual.at(pair),area),"Interface area differs"); ++checks; }
    }
    const std::vector<int> clusters{0,1};
    const std::vector<CellNode> diagonal{{{{7,7,0},0},{-1,-2}}};
    auto graph=reconstructCellGraph(diagonal,clusters,0,{-1,-1,-1},{1,1,1});
    require(graph.cells.size()==2 && graph.interfaces.size()==1 && near(graph.interfaces[0].area,4*std::sqrt(2.0)),"Oblique cut area differs");
    require(near(graph.cells[0].volume,4) && near(graph.cells[1].volume,4),"Scaled oblique plane volumes differ");
    graph=reconstructCellGraph({},clusters,-1,{-1,-1,-1},{1,1,1});
    require(graph.cells.size()==1 && graph.interfaces.empty() && graph.enclosedOpenFaces==6 && near(graph.cellVolume,8),"Single-leaf world differs");
    const std::vector<CellNode> transverse{{{{0,1,0},0},{-1,-2}}};
    graph=reconstructCellGraph(transverse,clusters,0,{-1e-5,-1,-1},{1e-5,1,1});
    require(graph.cells.size()==2 && graph.interfaces.size()==1 && graph.degenerateFragments==0
        && std::abs(graph.cellVolume-8e-5)<1e-15,"Resolvable thin slab differs");
    graph=reconstructCellGraph(transverse,clusters,0,{-1e-8,-1,-1},{1e-8,1,1});
    require(graph.degenerateFragments>0,"Cap collapse at the merge tolerance was not disclosed");
    auto shared=diagonal; shared[0].children={-1,-1};
    graph=reconstructCellGraph(shared,clusters,0,{-1,-1,-1},{1,1,1});
    require(graph.cells.size()==2 && graph.cells[0].leaf==graph.cells[1].leaf && graph.interfaces.size()==1,"Shared leaves lost distinct path cells");
    const std::vector<CellNode> axial{{{{1,0,0},0},{-1,-2}}};
    CellLimits exact; exact.cells=2; exact.faces=2; exact.points=8; exact.cellFaces=6; exact.cellPoints=24; exact.pendingPoints=48;
    graph=reconstructCellGraph(axial,clusters,0,{-1,-1,-1},{1,1,1},exact);
    require(graph.interfaces.size()==1 && near(graph.interfaces[0].area,4),"Exact geometry limits rejected");
    const auto fails=[&](auto operation,const char* expected) {
        try { operation(); }
        catch(const std::runtime_error& error) { require(std::string(error.what()).find(expected)!=std::string::npos,"Wrong failure diagnostic"); ++checks; return; }
        throw std::runtime_error("Invalid/over-budget cell graph accepted");
    };
    for(int constraint=0;constraint<7;++constraint) {
        auto small=exact;
        if(constraint==0) small.cells=1;
        if(constraint==1) small.faces=1;
        if(constraint==2) small.points=7;
        if(constraint==3) small.cellFaces=5;
        if(constraint==4) small.cellPoints=23;
        if(constraint==5) small.pendingPoints=47;
        if(constraint==6) small.work=1;
        fails([&] { reconstructCellGraph(axial,clusters,0,{-1,-1,-1},{1,1,1},small); },constraint==6?"budget":"limit");
    }
    for(auto children:{std::array{0,-1},std::array{1,-1},std::array{-3,-1},std::array{std::numeric_limits<int>::min(),-1}}) {
        auto bad=axial; bad[0].children=children;
        fails([&] { reconstructCellGraph(bad,clusters,0,{-1,-1,-1},{1,1,1}); },children[0]<0?"leaf":"node");
    }
    for(double bad:{0.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto input=axial; input[0].plane.normal={bad,0,0};
        fails([&] { reconstructCellGraph(input,clusters,0,{-1,-1,-1},{1,1,1}); },"plane");
    }
    fails([&] { reconstructCellGraph(axial,clusters,0,{-1e8,-1,-1},{1,1,1}); },"enclosure");
    fails([&] { reconstructCellGraph(axial,clusters,0,{1,1,1},{1,1,1}); },"enclosure");
    auto lowPending=exact; lowPending.pendingPoints=23;
    fails([&] { reconstructCellGraph(axial,clusters,0,{-1,-1,-1},{1,1,1},lowPending); },"limit");
    const std::vector<CellNode> sharedNodes{{{{1,0,0},0},{1,1}},{{{0,1,0},0},{-1,-2}}};
    fails([&] { reconstructCellGraph(sharedNodes,clusters,0,{-1,-1,-1},{1,1,1}); },"node");
    // An actual deep graph, traversed iteratively; repeated coplanar cuts have
    // no positive-volume back cell and must not create phantom interfaces.
    std::vector<CellNode> deep(10000,axial[0]);
    for(size_t i=0;i+1<deep.size();++i) deep[i].children[0]=int(i+1);
    graph=reconstructCellGraph(deep,clusters,0,{-1,-1,-1},{1,1,1});
    require(graph.cells.size()==2 && graph.interfaces.size()==1,"Deep coplanar paths created phantom cells");
    std::cout<<checks<<" independent adjacency/area and rejection checks passed; oblique, single/shared-leaf, thin/degenerate, exact-capacity and 10000-node controls passed\n";
}
catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
