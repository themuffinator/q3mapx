// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/planar_reduction.h"
#include "q3mapx/exact_predicates.h"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

using namespace q3mapx;
static void require(bool value,const char* why) { if(!value) throw std::runtime_error(why); }
using Edge=std::pair<uint32_t,uint32_t>;
static std::map<Edge,int> boundary(std::span<const ReductionTriangle> triangles) {
    std::map<Edge,int> result;
    for(const auto& tri:triangles) for(size_t i=0;i<3;++i) {
        uint32_t a=tri[i],b=tri[(i+1)%3]; result[std::minmax(a,b)]+=a<b?1:-1;
    }
    std::erase_if(result,[](const auto& entry) { return entry.second==0; }); return result;
}
static ReductionVertex vertex(float x,float y,bool sloped=false) {
    ReductionVertex v{}; v[0]=x; v[1]=y; v[2]=sloped?3*x-2*y+8:0;
    for(size_t a=3;a<v.size();++a) v[a]=a%3?2*x-3*y+float(a):float(a);
    return v;
}
static double area2(const ReductionVertex& a,const ReductionVertex& b,const ReductionVertex& c) {
    return (double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]);
}
static size_t verify(const std::vector<ReductionVertex>& vertices,const std::vector<ReductionTriangle>& original,
                    const PlanarReduction& result) {
    require(boundary(original)==boundary(result.triangles),"Boundary/T-junction segments changed");
    require(original.size()==result.triangles.size()+2*result.edits.size(),"Triangle savings differ from edit history");
    auto history=original;
    std::vector<bool> active(original.size(),true);
    std::set<uint32_t> removed;
    for(const auto& edit:result.edits) {
        require(removed.insert(edit.vertex).second,"Vertex removed more than once");
        std::vector<ReductionTriangle> before;
        for(uint32_t id:edit.removedFaces) {
            require(id<history.size() && active[id],"History names an unavailable face");
            require(std::find(history[id].begin(),history[id].end(),edit.vertex)!=history[id].end(),"Edit removed unrelated face");
            before.push_back(history[id]); active[id]=false;
        }
        require(before.size()==edit.addedFaces.size()+2 && boundary(before)==boundary(edit.addedFaces),"Edit does not conserve its boundary");
        for(const auto& tri:edit.addedFaces) {
            for(uint32_t v:tri) require(!removed.contains(v),"New face uses removed vertex");
            history.push_back(tri); active.push_back(true);
        }
    }
    std::vector<ReductionTriangle> replayed;
    for(size_t i=0;i<history.size();++i) if(active[i]) replayed.push_back(history[i]);
    require(replayed==result.triangles,"Final mesh differs from independent history replay");
    double beforeArea=0,afterArea=0;
    for(const auto& tri:original) beforeArea+=area2(vertices[tri[0]],vertices[tri[1]],vertices[tri[2]]);
    for(const auto& tri:result.triangles) {
        const double area=area2(vertices[tri[0]],vertices[tri[1]],vertices[tri[2]]);
        require(area*beforeArea>0,"New face has wrong winding or zero area"); afterArea+=area;
    }
    require(beforeArea==afterArea,"Exact integer-coordinate area changed");
    // Independent point coverage and barycentric attribute interpolation over
    // both triangulations. Samples on an edge are excluded, not double-counted.
    size_t samples=0;
    const auto sample=[&](const auto& triangles,float x,float y,std::array<double,32>& values) {
        int count=0; const auto p=vertex(x,y);
        for(const auto& tri:triangles) {
            const auto& a=vertices[tri[0]]; const auto& b=vertices[tri[1]]; const auto& c=vertices[tri[2]];
            const double total=area2(a,b,c),wa=area2(p,b,c)/total,wb=area2(a,p,c)/total,wc=1-wa-wb;
            if(std::min({wa,wb,wc})<-1e-10) continue;
            if(std::min({std::abs(wa),std::abs(wb),std::abs(wc)})<1e-10) return -1;
            ++count;
            for(size_t field=0;field<32;++field) values[field]=wa*a[field]+wb*b[field]+wc*c[field];
        }
        return count;
    };
    for(unsigned i=0;i<192;++i) {
        const float x=float(int((i*137)%1009)-200)/13,y=float(int((i*241)%1013)-200)/17;
        std::array<double,32> a{},b{};
        const int ca=sample(original,x,y,a),cb=sample(result.triangles,x,y,b);
        if(ca<0 || cb<0) continue;
        require(ca==cb && ca<=1,"Coverage differs or overlaps");
        if(ca) for(size_t f=0;f<32;++f) require(std::abs(a[f]-b[f])<1e-10,"Interpolated field differs");
        ++samples;
    }
    return samples;
}

int main(int argc,char** argv) try {
    if(argc==2 && std::string(argv[1])=="--predicates") {
        unsigned dimensions;
        while(std::cin>>std::dec>>dimensions) {
            require(dimensions==2 || dimensions==3,"Invalid predicate dimensions");
            std::array<std::array<float,3>,4> points{};
            for(size_t i=0;i<dimensions+1;++i) for(size_t j=0;j<dimensions;++j) {
                uint32_t bits; require(bool(std::cin>>std::hex>>bits),"Truncated predicate input"); points[i][j]=std::bit_cast<float>(bits);
            }
            std::cout<<std::dec<<(dimensions==2?orient2Exact({points[0][0],points[0][1]},
                {points[1][0],points[1][1]},{points[2][0],points[2][1]}):orient3Exact(points[0],points[1],points[2],points[3]))<<'\n';
        }
        return 0;
    }
    if(argc==2 && std::string(argv[1])=="--mesh") {
        size_t count,triangleCount;
        uint64_t work;
        while(std::cin>>std::dec>>count>>triangleCount>>work) {
            ReductionLimits limits; limits.work=work;
            require(count<=limits.vertices && triangleCount<=limits.triangles,"Oversize mesh test input");
            std::vector<ReductionVertex> vertices(count);
            std::vector<ReductionTriangle> triangles(triangleCount);
            for(auto& vertex:vertices) for(float& field:vertex) {
                uint32_t bits; require(bool(std::cin>>std::hex>>bits),"Truncated mesh vertex input"); field=std::bit_cast<float>(bits);
            }
            for(auto& triangle:triangles) for(auto& v:triangle) require(bool(std::cin>>std::dec>>v),"Truncated mesh face input");
            const auto result=reducePlanarMesh(vertices,triangles,limits);
            std::cout<<std::dec<<result.triangles.size()<<' '<<result.edits.size()<<' '<<result.workUsed<<' '
                <<result.topologyRejected<<' '<<result.planarRejected<<' '<<result.attributeRejected<<'\n';
            for(const auto& triangle:result.triangles) std::cout<<triangle[0]<<' '<<triangle[1]<<' '<<triangle[2]<<'\n';
        }
        return 0;
    }
    require(argc==1,"Unknown test-driver option");
    size_t samples=0,edits=0,meshes=0;
    for(unsigned n:{2,3,5,9,17}) for(bool slope:{false,true}) for(bool reverse:{false,true}) {
        std::vector<ReductionVertex> vertices;
        std::vector<ReductionTriangle> triangles;
        for(unsigned y=0;y<=n;++y) for(unsigned x=0;x<=n;++x) vertices.push_back(vertex(float(x*4),float(y*4),slope));
        for(unsigned y=0;y<n;++y) for(unsigned x=0;x<n;++x) {
            const unsigned a=y*(n+1)+x,b=a+1,c=b+n+1,d=a+n+1;
            if((x+y)%2) { triangles.push_back({a,b,d}); triangles.push_back({b,c,d}); }
            else { triangles.push_back({a,b,c}); triangles.push_back({a,c,d}); }
        }
        if(reverse) for(auto& t:triangles) std::swap(t[0],t[1]);
        const auto result=reducePlanarMesh(vertices,triangles);
        require(result.triangles.size()==4*n-2,"Grid did not reach the boundary-preserving minimum triangle count");
        samples+=verify(vertices,triangles,result); edits+=result.edits.size(); ++meshes;
    }
    std::vector<ReductionVertex> vertices{vertex(0,0),vertex(-4,-4),vertex(4,-4),vertex(4,4),vertex(-4,4)};
    const std::vector<ReductionTriangle> fan{{0,1,2},{0,2,3},{0,3,4},{0,4,1}};
    auto result=reducePlanarMesh(vertices,fan);
    require(result.edits.size()==1 && result.triangles.size()==2,"Simple fan was not reduced");
    samples+=verify(vertices,fan,result);
    auto limits=ReductionLimits{}; limits.work=result.workUsed;
    require(reducePlanarMesh(vertices,fan,limits).triangles==result.triangles,"Exact work budget rejected");
    const auto fails=[](auto operation,const char* expected) {
        try { operation(); }
        catch(const std::runtime_error& error) { require(std::string(error.what()).find(expected)!=std::string::npos,"Unexpected failure diagnostic"); return; }
        throw std::runtime_error("Malformed/over-budget input accepted");
    };
    --limits.work; fails([&] { reducePlanarMesh(vertices,fan,limits); },"budget");
    limits={}; limits.historyFaces=5; fails([&] { reducePlanarMesh(vertices,fan,limits); },"history");
    limits.historyFaces=6; require(reducePlanarMesh(vertices,fan,limits).edits.size()==1,"Exact history capacity rejected");
    limits={}; limits.ring=3; result=reducePlanarMesh(vertices,fan,limits);
    require(result.edits.empty() && result.ringLimitRejected==1,"Ring limit did not retain original mesh");
    limits={}; limits.vertices=4; fails([&] { reducePlanarMesh(vertices,fan,limits); },"geometry");
    auto invalid=fan; invalid[0][0]=UINT32_MAX;
    fails([&] { reducePlanarMesh(vertices,invalid); },"index");
    for(size_t field:{size_t(2),size_t(3),size_t(8),size_t(31)}) {
        auto changed=vertices; changed[0][field]=std::nextafter(changed[0][field],std::numeric_limits<float>::infinity());
        result=reducePlanarMesh(changed,fan);
        require(result.triangles==fan && result.edits.empty(),"One-ULP geometric/attribute discontinuity removed");
        require(field==2?result.planarRejected==1:result.attributeRejected==1,"Discontinuity reason differs");
    }
    for(float invalidValue:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto changed=vertices; changed[0][31]=invalidValue;
        fails([&] { reducePlanarMesh(changed,fan); },"finite");
        fails([&] { orient2Exact({invalidValue,0},{0,1},{1,0}); },"finite");
    }
    const int rounding=std::fegetround();
    require(std::fesetround(FE_DOWNWARD)==0,"Cannot set rounding-mode control");
    fails([&] { orient2Exact({0,0},{1,0},{0,1}); },"round-to-nearest");
    require(std::fesetround(rounding)==0,"Cannot restore rounding mode");
    auto duplicate=fan; duplicate.push_back(fan[0]); result=reducePlanarMesh(vertices,duplicate);
    require(result.triangles==duplicate && result.protectedVertices>=3,"Nonmanifold edge was edited");
    // Each possible diagonal already belongs to remote geometry: neither may
    // become the fan's new shared edge, even though its projected area agrees.
    auto linked=fan;
    auto linkedVertices=vertices; linkedVertices.push_back(vertex(0,0,true));
    linked.push_back({1,3,5}); linked.push_back({2,4,5});
    result=reducePlanarMesh(linkedVertices,linked);
    require(result.triangles==linked && result.topologyRejected==1,"Existing external diagonal became nonmanifold");
    // A nonconvex, simple star must still reduce without flattening its notch.
    vertices={vertex(0,0),vertex(-4,-4),vertex(4,-4),vertex(2,0),vertex(4,4),vertex(-4,4)};
    const std::vector<ReductionTriangle> concave{{0,1,2},{0,2,3},{0,3,4},{0,4,5},{0,5,1}};
    result=reducePlanarMesh(vertices,concave); require(result.triangles.size()==3,"Concave simple star not reduced");
    samples+=verify(vertices,concave,result);
    vertices={vertex(0,0),vertex(0,6),vertex(-6,2),vertex(-4,-5),vertex(4,-5),vertex(6,2)};
    const std::vector<ReductionTriangle> crossed{{0,1,3},{0,3,5},{0,5,2},{0,2,4},{0,4,1}};
    result=reducePlanarMesh(vertices,crossed);
    require(result.triangles==crossed && result.topologyRejected==1,"Crossed star boundary was accepted");
    std::cout<<meshes<<" grids reached their boundary-preserving minimum; "<<edits<<" interior removals; "
             <<samples<<" independent coverage/interpolation samples; history, discontinuity, topology and limit controls passed\n";
}
catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
