// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "portal_evidence.h"
#include <bit>
#include <map>
#include <numeric>
#include <numbers>
#include <set>
#include <stdexcept>

namespace q3mapx {
namespace {
struct Budget {
    uint64_t used, limit;
    void spend(uint64_t n) {
        if(n>limit-used) throw std::runtime_error("Portal evidence work budget exceeded; no report was published");
        used+=n;
    }
};
using Point=std::array<double,3>;
Point subtract(const Point& a,const Point& b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
Point cross(const Point& a,const Point& b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double dot(const Point& a,const Point& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
Point point(const std::array<float,3>& p) { return {p[0],p[1],p[2]}; }
using PlaneKey=std::array<float,4>;
PlaneKey key(const bspPlane_t& plane) {
    float sign=1;
    for(size_t a=0;a<3;++a) if(const float v=plane.normal()[a]; v!=0) { sign=v<0?-1.f:1.f; break; }
    return {sign*plane.normal()[0],sign*plane.normal()[1],sign*plane.normal()[2],sign*plane.dist()};
}

PortalMeasure measure(const PortalPolygon& polygon, Point& normal, Budget& budget) {
    PortalMeasure result;
    const size_t n=polygon.points.size();
    budget.spend(2*n*n+n);
    for(const auto& p:polygon.points) for(size_t a=0;a<3;++a) {
        if(std::abs(double(p[a]))>10'000'000) return result;
        result.center[a]+=double(p[a])/n;
    }
    for(size_t i=0;i<n;++i) {
        for(size_t j=i+1;j<n;++j) {
            const auto difference=subtract(point(polygon.points[i]),point(polygon.points[j]));
            if(dot(difference,difference)<=1e-14) return result;
        }
        const auto a=subtract(point(polygon.points[i]),result.center);
        const auto b=subtract(point(polygon.points[(i+1)%n]),result.center);
        const auto product=cross(a,b);
        for(size_t j=0;j<3;++j) normal[j]+=product[j];
        const auto edge=subtract(b,a);
        result.perimeter+=std::sqrt(dot(edge,edge));
    }
    const double magnitude=std::sqrt(dot(normal,normal));
    if(magnitude<=1e-12 || result.perimeter<=1e-12) return result;
    for(double& v:normal) v/=magnitude;
    for(size_t i=0;i<n;++i) {
        const auto p=point(polygon.points[i]);
        if(std::abs(dot(subtract(p,result.center),normal))>0.01) return result;
        const auto edge=subtract(point(polygon.points[(i+1)%n]),p);
        const double tolerance=0.01*std::sqrt(dot(edge,edge));
        for(const auto& q:polygon.points)
            if(dot(cross(edge,subtract(point(q),p)),normal)<-tolerance) return result;
    }
    result.planarConvex=true;
    result.area=0.5*magnitude;
    result.width=2*result.area/result.perimeter;
    result.compactness=4*std::numbers::pi*result.area/(result.perimeter*result.perimeter);
    return result;
}

int pointCluster(int head,const Point& p,Budget& budget) {
    while(head>=0) {
        budget.spend(1);
        const auto& node=bspNodes[head]; const auto& plane=bspPlanes[node.planeNum];
        const double distance=double(plane.normal()[0])*p[0]+double(plane.normal()[1])*p[1]+double(plane.normal()[2])*p[2]-plane.dist();
        head=node.children[distance>=0?0:1];
    }
    return bspLeafs[size_t(-1-int64_t(head))].cluster;
}
}

PortalEvidence analyzePortalEvidence(BSPEvidence& bsp,const PortalGraph& graph,uint64_t workLimit) {
    if(bsp.workUsed>workLimit) throw std::runtime_error("Portal evidence work budget already exhausted");
    Budget budget{bsp.workUsed,workLimit};
    PortalEvidence result;
    result.worldMapping=bsp.hasWorldHead && bsp.uniqueNodePaths;
    result.clusters.resize(graph.clusters);
    result.portals.resize(graph.portals.size());
    for(const auto& region:bsp.regions) { PortalRegionEvidence r; r.node=region.node; result.regions.push_back(r); }
    const int remainder=int(result.regions.size());
    result.regions.emplace_back();
    std::vector<std::set<PlaneKey>> regionPlanes(result.regions.size());
    std::vector<std::set<int>> brushSamples(result.regions.size());
    std::vector<std::vector<int>> surfaces(graph.clusters), edges(graph.clusters);
    if(result.worldMapping) {
        std::vector<int> selected(bspNodes.size(),-1);
        for(size_t r=0;r<bsp.regions.size();++r) selected[bsp.regions[r].node]=int(r);
        std::vector<byte> visited(bspLeafs.size());
        std::vector<std::pair<int,int>> pending{{bsp.worldHead,remainder}};
        while(!pending.empty()) {
            auto [index,region]=pending.back(); pending.pop_back(); budget.spend(1);
            if(index>=0) {
                if(selected[index]>=0) region=selected[index];
                const auto& node=bspNodes[index]; regionPlanes[region].insert(key(bspPlanes[node.planeNum]));
                for(int child:node.children) pending.emplace_back(child,region);
                continue;
            }
            const size_t l=size_t(-1-int64_t(index)); const auto& leaf=bspLeafs[l];
            budget.spend(leaf.numBSPLeafBrushes);
            auto& samples=brushSamples[region];
            for(int j=0;j<leaf.numBSPLeafBrushes;++j) {
                const int b=bspLeafBrushes[leaf.firstBSPLeafBrush+j];
                if(bsp.brushes[b].model==0) {
                    samples.insert(b);
                    if(samples.size()>64) samples.erase(std::prev(samples.end()));
                }
            }
            if(leaf.cluster<0) continue;
            if(leaf.cluster>=graph.clusters) throw std::runtime_error("PRT1 clusters do not cover reachable BSP clusters");
            auto& cluster=result.clusters[leaf.cluster];
            if(cluster.region<0) cluster.region=region;
            else if(cluster.region!=region) cluster.region=remainder;
            if(visited[l]) continue;
            visited[l]=1;
            ++cluster.leaves;
            budget.spend(leaf.numBSPLeafSurfaces);
            for(int j=0;j<leaf.numBSPLeafSurfaces;++j) {
                const int s=bspLeafSurfaces[leaf.firstBSPLeafSurface+j]; const auto& world=bspModels[0];
                if(s>=world.firstBSPSurface && s-world.firstBSPSurface<world.numBSPSurfaces) surfaces[leaf.cluster].push_back(s);
            }
        }
    }
    for(size_t r=0;r<brushSamples.size();++r) for(int b:brushSamples[r]) {
        for(int side:bsp.brushes[b].leafPathSides) {
            budget.spend(1);
            if(regionPlanes[r].contains(key(bspPlanes[bspBrushSides[side].planeNum]))) {
                result.regions[r].associatedBrushSample.push_back(b); break;
            }
        }
    }
    for(int c=0;c<graph.clusters;++c) {
        auto& cluster=result.clusters[c];
        if(cluster.region<0) cluster.region=remainder;
        result.unmappedClusters+=cluster.leaves==0;
        result.regions[cluster.region].clusters.push_back(c);
        auto& ss=surfaces[c]; std::sort(ss.begin(),ss.end()); ss.erase(std::unique(ss.begin(),ss.end()),ss.end());
        cluster.worldSurfaces=ss.size();
        for(int s:ss) cluster.worldTriangles+=bspDrawSurfaces[s].numIndexes/3;
    }
    for(size_t p=0;p<graph.portals.size();++p) {
        budget.spend(2);
        const auto& portal=graph.portals[p];
        edges[portal.front].push_back(int(p)); edges[portal.back].push_back(int(p));
    }
    // Iterative bridge search retains distinct parallel openings. Only the
    // exact parent edge is skipped; another edge to that vertex is a back edge.
    std::vector<int> entered(graph.clusters),low(graph.clusters),parent(graph.clusters,-1),next(graph.clusters);
    int time=0;
    for(int start=0;start<graph.clusters;++start) if(!entered[start]) {
        const int component=int(result.components++);
        std::vector<int> stack{start}; entered[start]=low[start]=++time;
        while(!stack.empty()) {
            const int c=stack.back(); result.clusters[c].component=component; budget.spend(1);
            if(size_t(next[c])<edges[c].size()) {
                const int e=edges[c][next[c]++]; if(e==parent[c]) continue;
                const auto& portal=graph.portals[e]; const int other=portal.front==c?portal.back:portal.front;
                if(!entered[other]) { parent[other]=e; entered[other]=low[other]=++time; stack.push_back(other); }
                else low[c]=std::min(low[c],entered[other]);
            }
            else {
                stack.pop_back();
                if(parent[c]>=0) {
                    const auto& portal=graph.portals[parent[c]]; const int other=portal.front==c?portal.back:portal.front;
                    low[other]=std::min(low[other],low[c]);
                    if(low[c]>entered[other]) { result.portals[parent[c]].bridge=true; ++result.bridges; }
                }
            }
        }
    }
    std::map<std::pair<int,int>,uint64_t> neighborPairs;
    for(size_t p=0;p<graph.portals.size();++p) {
        const auto& portal=graph.portals[p]; Point normal{};
        const bool bridge=result.portals[p].bridge;
        auto& m=result.portals[p]; m=measure(portal,normal,budget); m.bridge=bridge;
        result.invalidGeometry+=!m.planarConvex;
        if(m.planarConvex && result.worldMapping) {
            auto front=m.center,back=m.center;
            for(size_t a=0;a<3;++a) { front[a]+=0.02*normal[a]; back[a]-=0.02*normal[a]; }
            const int a=pointCluster(bsp.worldHead,front,budget),b=pointCluster(bsp.worldHead,back,budget);
            if((a==portal.front && b==portal.back) || (b==portal.front && a==portal.back)) {
                m.probe="agrees"; ++result.probeMatches;
            }
            else { m.probe="disagrees"; ++result.probeDisagreements; }
        }
        const int a=result.clusters[portal.front].region,b=result.clusters[portal.back].region;
        const bool repeated=neighborPairs[std::minmax(portal.front,portal.back)]++!=0;
        for(int region : {a,b}) {
            auto& r=result.regions[region]; ++r.incident;
            if(a==b) ++r.internal; else ++r.boundary;
            r.hints+=(portal.flags&1)!=0; r.skies+=(portal.flags&2)!=0; r.unknownFlags+=(portal.flags&~3)!=0;
            r.bridges+=bridge; r.parallelOpenings+=repeated;
            r.small+=m.planarConvex && m.area<64; r.slender+=m.planarConvex && m.compactness<0.1;
            if(a==b) break;
        }
    }
    for(int c=0;c<graph.clusters;++c) {
        auto& cluster=result.clusters[c]; cluster.degree=edges[c].size();
        const auto pairs=cluster.degree ? cluster.degree*(cluster.degree-1):0;
        result.passagePairs+=pairs; result.regions[cluster.region].passagePairs+=pairs;
    }
    result.pvsCosts=result.worldMapping && !result.unmappedClusters && bsp.visibility.present && bsp.visibility.clusters==graph.clusters;
    if(result.pvsCosts) {
        std::vector<int> marks(bspDrawSurfaces.size(),-1);
        const size_t bytes=(size_t(graph.clusters)+7)/8;
        for(int c=0;c<graph.clusters;++c) {
            auto& cluster=result.clusters[c]; auto& region=result.regions[cluster.region];
            const byte* row=bspVisBytes.data()+8+size_t(c)*bsp.visibility.rowBytes; budget.spend(bytes);
            for(size_t j=0;j<bytes;++j) {
                unsigned bits=row[j];
                if(j+1==bytes && graph.clusters%8) bits&=(1u<<(graph.clusters%8))-1;
                while(bits) {
                    const int visible=int(j*8+std::countr_zero(bits)); bits&=bits-1;
                    budget.spend(1+surfaces[visible].size());
                    region.visibleInternalPairs+=result.clusters[visible].region==cluster.region;
                    for(int s:surfaces[visible]) if(marks[s]!=c) {
                        marks[s]=c; ++cluster.visibleSurfaces;
                        cluster.visibleTriangles+=bspDrawSurfaces[s].numIndexes/3;
                        cluster.visiblePatches+=bspDrawSurfaces[s].surfaceType==MST_PATCH;
                    }
                }
            }
            region.sumVisibleTriangles+=cluster.visibleTriangles;
            region.maxVisibleTriangles=std::max(region.maxVisibleTriangles,cluster.visibleTriangles);
        }
    }
    result.rankedRegions.resize(result.regions.size());
    std::iota(result.rankedRegions.begin(),result.rankedRegions.end(),0);
    std::sort(result.rankedRegions.begin(),result.rankedRegions.end(),[&](int a,int b) {
        return result.regions[a].passagePairs!=result.regions[b].passagePairs
            ? result.regions[a].passagePairs>result.regions[b].passagePairs : a<b;
    });
    bsp.workUsed=budget.used;
    return result;
}
}
