// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/vis_rows.h"
#include <iostream>
#include <numeric>
#include <random>
#include <set>

static void require(bool ok) { if(!ok) throw std::runtime_error("VIS row oracle mismatch"); }
template<class F> static void rejects(F&& action) {
    try { action(); } catch(const std::exception&) { return; }
    throw std::runtime_error("Invalid VIS row input accepted");
}

int main() try {
    using Word=std::uint64_t;
    std::mt19937_64 random(20261002);
    std::size_t checks=0;
    for(int n:{1,2,3,63,64,65,127,128,129,1023}) for(int trial=0;trial<40;++trial) {
        std::vector<int> parent(n,-1),order(n),root(n);
        std::iota(order.begin(),order.end(),0);
        std::shuffle(order.begin(),order.end(),random);
        for(int i=0;i<n-1;++i) if(random()%4) parent[order[i]]=order[i+1+random()%(n-i-1)];
        // Independent pointer walking, not the optimized grouping algorithm.
        for(int i=0;i<n;++i) { root[i]=i; while(parent[root[i]]>=0) root[i]=parent[root[i]]; }
        q3mapx::VisRowGroups groups(parent);
        std::set<int> representatives(root.begin(),root.end());
        require(groups.groups().size()==representatives.size() && groups.clusters()==std::size_t(n));
        for(int i=0;i<n;++i) require(groups.groups()[groups.groupOf(i)].representative==root[i]);
        for(int portals:{0,1,63,64,65,127,128,129}) {
            std::vector<int> targets(portals),targetGroups(portals);
            for(int i=0;i<portals;++i) targetGroups[i]=groups.groupOf(targets[i]=int(random()%n));
            std::vector<Word> mask((portals+63)/64);
            for(auto& word:mask) word=trial%3==0?~Word{}:trial%3==1?0:random();
            const int source=int(random()%n);
            std::set<int> seen{root[source]};
            for(int i=0;i<portals;++i) if(mask[i/64]&(Word{1}<<(i%64))) seen.insert(root[targets[i]]);
            std::vector<Word> guarded((n+63)/64+2,0xfefefefefefefefeULL),expected((n+63)/64);
            int count=0;
            for(int i=0;i<n;++i) if(seen.contains(root[i])) { expected[i/64]|=Word{1}<<(i%64); ++count; }
            const auto row=std::span(guarded).subspan(1,expected.size());
            require(groups.expand(groups.groupOf(source),mask,targetGroups,row)==count);
            require(std::equal(row.begin(),row.end(),expected.begin()));
            require(guarded.front()==0xfefefefefefefefeULL && guarded.back()==guarded.front());
            ++checks;
        }
    }
    for(bool reverse:{false,true}) {
        std::vector<int> parent(q3mapx::VisRowGroups::maxClusters);
        for(int i=0;i<int(parent.size());++i) parent[i]=reverse?i-1:(i+1==int(parent.size())?-1:i+1);
        q3mapx::VisRowGroups groups(parent);
        require(groups.groups().size()==1 && groups.members(0).size()==parent.size());
        std::vector<Word> row(parent.size()/64);
        require(groups.expand(0,{},{},row)==int(parent.size()));
        require(std::all_of(row.begin(),row.end(),[](Word w){return w==~Word{};}));
    }
    require(q3mapx::VisRowGroups({}).groups().empty());
    for(const std::vector<int>& parent:std::vector<std::vector<int>>{{-2},{1},{0},{1,0},{-1,2,3,1}})
        rejects([&]{q3mapx::VisRowGroups groups(parent);});
    rejects([]{std::vector<int> parent(q3mapx::VisRowGroups::maxClusters+1,-1);q3mapx::VisRowGroups groups(parent);});
    const std::vector<int> parent{-1};
    q3mapx::VisRowGroups groups(parent);
    std::vector<Word> row(1),mask{1};
    const std::vector<int> target{0},invalid{-1};
    rejects([&]{groups.expand(0,mask,target,{});});
    rejects([&]{groups.expand(0,{},target,row);});
    rejects([&]{groups.expand(1,{},{},row);});
    rejects([&]{groups.expand(0,mask,invalid,row);});
    std::cout<<checks<<" independent row expansions, two maximum-depth forests and malformed-input guards passed\n";
}
catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
