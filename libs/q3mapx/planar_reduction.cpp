// SPDX-License-Identifier: GPL-3.0-or-later
#include "planar_reduction.h"
#include "exact_predicates.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace q3mapx {
namespace {
struct Context {
    const ReductionLimits& limits;
    uint64_t used=0;
    void spend(uint64_t n=1) {
        if(n>limits.work-used) throw std::runtime_error("Planar reduction work budget exceeded");
        used+=n;
    }
};
using Point=std::array<float,2>;
using Edge=std::pair<uint32_t,uint32_t>;
Edge edge(uint32_t a,uint32_t b) { return std::minmax(a,b); }
int orient(const Point& a,const Point& b,const Point& c,Context& ctx) {
    ctx.spend(16); return orient2Exact(a,b,c);
}
bool onSegment(const Point& a,const Point& b,const Point& p) {
    return p[0]>=std::min(a[0],b[0]) && p[0]<=std::max(a[0],b[0])
        && p[1]>=std::min(a[1],b[1]) && p[1]<=std::max(a[1],b[1]);
}
bool intersect(const Point& a,const Point& b,const Point& c,const Point& d,Context& ctx) {
    const int ac=orient(a,b,c,ctx),ad=orient(a,b,d,ctx),ca=orient(c,d,a,ctx),cb=orient(c,d,b,ctx);
    return (ac*ad<0 && ca*cb<0) || (ac==0 && onSegment(a,b,c)) || (ad==0 && onSegment(a,b,d))
        || (ca==0 && onSegment(c,d,a)) || (cb==0 && onSegment(c,d,b));
}
struct Face { ReductionTriangle vertices; bool active=true; };
}

PlanarReduction reducePlanarMesh(std::span<const ReductionVertex> vertices,
    std::span<const ReductionTriangle> triangles,const ReductionLimits& limits) {
    if(vertices.size()>limits.vertices || triangles.size()>limits.triangles || triangles.size()>limits.historyFaces
        || vertices.size()>UINT32_MAX || limits.historyFaces>UINT32_MAX)
        throw std::runtime_error("Planar reduction geometry limit exceeded");
    Context ctx{limits};
    for(const auto& v:vertices) { ctx.spend(v.size()); for(float value:v)
        if(!std::isfinite(value)) throw std::runtime_error("Planar reduction requires finite vertex fields"); }
    std::vector<Face> faces;
    std::vector<std::set<uint32_t>> incident(vertices.size());
    struct EdgeUse { unsigned count=0; int orientation=0; };
    std::map<Edge,EdgeUse> edges;
    std::vector<bool> boundary(vertices.size()),protectedVertex(vertices.size());
    for(const auto& tri:triangles) {
        ctx.spend(3);
        for(uint32_t v:tri) if(v>=vertices.size()) throw std::runtime_error("Invalid planar reduction vertex index");
        bool degenerate=tri[0]==tri[1] || tri[1]==tri[2] || tri[2]==tri[0];
        bool area=false;
        for(size_t axis=0;axis<3 && !area;++axis) {
            const size_t u=(axis+1)%3,v=(axis+2)%3;
            area=orient({vertices[tri[0]][u],vertices[tri[0]][v]},
                        {vertices[tri[1]][u],vertices[tri[1]][v]},
                        {vertices[tri[2]][u],vertices[tri[2]][v]},ctx)!=0;
        }
        degenerate|=!area;
        for(size_t i=0;i<3;++i) {
            const uint32_t a=tri[i],b=tri[(i+1)%3];
            incident[a].insert(uint32_t(faces.size()));
            auto& use=edges[edge(a,b)]; ++use.count; use.orientation+=a<b?1:-1;
            if(degenerate) protectedVertex[a]=true;
        }
        faces.push_back({tri});
    }
    for(const auto& [key,use]:edges) {
        ctx.spend();
        if(use.count==1) boundary[key.first]=boundary[key.second]=true;
    }
    // A nonmanifold edge protects all vertices of each triangle touching it.
    // Ordinary boundary edges protect only their endpoints.
    for(const auto& tri:triangles) for(size_t i=0;i<3;++i) {
        ctx.spend(); const auto& use=edges.at(edge(tri[i],tri[(i+1)%3]));
        if(use.count>2 || (use.count==2 && use.orientation!=0)) for(uint32_t v:tri) protectedVertex[v]=true;
    }
    edges.clear();
    PlanarReduction result;
    result.boundaryVertices=std::count(boundary.begin(),boundary.end(),true);
    result.protectedVertices=std::count(protectedVertex.begin(),protectedVertex.end(),true);
    using Priority=std::pair<size_t,uint32_t>;
    std::set<Priority> pending;
    const auto eligible=[&](uint32_t v) { return !boundary[v] && !protectedVertex[v] && incident[v].size()>=3; };
    for(uint32_t v=0;v<vertices.size();++v) if(eligible(v)) pending.emplace(incident[v].size(),v);
    while(!pending.empty()) {
        const uint32_t center=pending.begin()->second; pending.erase(pending.begin());
        ctx.spend(); ++result.attemptedStars;
        if(incident[center].size()>limits.ring) { ++result.ringLimitRejected; continue; }
        std::map<uint32_t,uint32_t> next;
        std::set<uint32_t> incoming;
        bool valid=true;
        for(uint32_t id:incident[center]) {
            ctx.spend(); const auto& tri=faces[id].vertices;
            const auto i=size_t(std::find(tri.begin(),tri.end(),center)-tri.begin());
            if(i==3) throw std::runtime_error("Planar reduction incidence invariant failed");
            const uint32_t a=tri[(i+1)%3],b=tri[(i+2)%3];
            valid&=next.emplace(a,b).second && incoming.insert(b).second;
        }
        std::vector<uint32_t> ring;
        if(valid) {
            uint32_t v=next.begin()->first;
            for(size_t i=0;i<next.size();++i) {
                ctx.spend(); ring.push_back(v);
                auto it=next.find(v); if(it==next.end()) { valid=false; break; }
                v=it->second;
                if(v==ring.front() && i+1!=next.size()) { valid=false; break; }
            }
            valid&=v==ring.front();
        }
        if(!valid) { ++result.topologyRejected; continue; }
        size_t u=0,v=1;
        int sign=0;
        for(size_t axis=0;axis<3 && sign==0;++axis) {
            u=(axis+1)%3; v=(axis+2)%3;
            sign=orient({vertices[center][u],vertices[center][v]},
                        {vertices[ring[0]][u],vertices[ring[0]][v]},
                        {vertices[ring[1]][u],vertices[ring[1]][v]},ctx);
        }
        if(!sign) { ++result.topologyRejected; continue; }
        const auto point=[&](uint32_t id) { return Point{vertices[id][u],vertices[id][v]}; };
        // The third spatial field must be affine too; only then is projection a
        // valid coordinate system for this entire star, not just its first face.
        bool planar=true,affine=true;
        for(size_t field=0;field<ReductionVertex{}.size() && affine;++field) {
            if(field==u || field==v) continue;
            bool constant=true;
            for(uint32_t id:ring) { ctx.spend(); constant&=vertices[id][field]==vertices[center][field]; }
            if(constant) continue;
            const auto lifted=[&](uint32_t id) { return std::array{vertices[id][u],vertices[id][v],vertices[id][field]}; };
            for(size_t i=2;i<ring.size();++i) {
                ctx.spend(64);
                if(orient3Exact(lifted(center),lifted(ring[0]),lifted(ring[1]),lifted(ring[i]))!=0) {
                    if(field<3) planar=false;
                    affine=false; break;
                }
            }
        }
        if(!planar) { ++result.planarRejected; continue; }
        if(!affine) { ++result.attributeRejected; continue; }
        for(size_t i=0;i<ring.size() && valid;++i) {
            const size_t i2=(i+1)%ring.size();
            valid=orient(point(ring[i]),point(ring[i2]),point(center),ctx)==sign;
            for(size_t j=i+1;j<ring.size() && valid;++j) {
                const size_t j2=(j+1)%ring.size();
                if(i2==j || j2==i) continue;
                valid=!intersect(point(ring[i]),point(ring[i2]),point(ring[j]),point(ring[j2]),ctx);
            }
        }
        if(!valid) { ++result.topologyRejected; continue; }
        // Ear removal preserves every boundary segment, including collinear
        // T-junction vertices. Reject ears containing any other boundary vertex.
        auto polygon=ring;
        std::vector<ReductionTriangle> replacement;
        while(polygon.size()>3) {
            bool found=false;
            for(size_t i=0;i<polygon.size() && !found;++i) {
                const auto a=polygon[(i+polygon.size()-1)%polygon.size()],b=polygon[i],c=polygon[(i+1)%polygon.size()];
                if(orient(point(a),point(b),point(c),ctx)!=sign) continue;
                bool clear=true;
                for(uint32_t p:polygon) if(p!=a && p!=b && p!=c) {
                    if(orient(point(a),point(b),point(p),ctx)*sign>=0
                        && orient(point(b),point(c),point(p),ctx)*sign>=0
                        && orient(point(c),point(a),point(p),ctx)*sign>=0) { clear=false; break; }
                }
                if(clear) { replacement.push_back({a,b,c}); polygon.erase(polygon.begin()+i); found=true; }
            }
            if(!found) { valid=false; break; }
        }
        if(valid && orient(point(polygon[0]),point(polygon[1]),point(polygon[2]),ctx)==sign)
            replacement.push_back({polygon[0],polygon[1],polygon[2]});
        else valid=false;
        if(!valid || replacement.size()+2!=ring.size()) { ++result.topologyRejected; continue; }
        // A new diagonal must not identify an edge from another part of the
        // mesh. Local coverage alone cannot prove this global link condition.
        for(const auto& tri:replacement) for(size_t i=0;i<3 && valid;++i) {
            const uint32_t a=tri[i],b=tri[(i+1)%3];
            if(next.at(a)==b || next.at(b)==a) continue;
            for(uint32_t id:incident[a]) {
                ctx.spend(); const auto& other=faces[id].vertices;
                if(std::find(other.begin(),other.end(),b)!=other.end()) { valid=false; break; }
            }
        }
        if(!valid) { ++result.topologyRejected; continue; }
        if(replacement.size()>limits.historyFaces-faces.size()) throw std::runtime_error("Planar reduction history limit exceeded");
        for(uint32_t id:ring) pending.erase({incident[id].size(),id});
        ReductionEdit edit{center,{incident[center].begin(),incident[center].end()},std::move(replacement)};
        for(uint32_t id:edit.removedFaces) {
            ctx.spend(3); faces[id].active=false;
            for(uint32_t vertex:faces[id].vertices) incident[vertex].erase(id);
        }
        for(const auto& tri:edit.addedFaces) {
            ctx.spend(3);
            for(uint32_t vertex:tri) incident[vertex].insert(uint32_t(faces.size()));
            faces.push_back({tri});
        }
        result.edits.push_back(std::move(edit));
        for(uint32_t id:ring) if(eligible(id)) pending.emplace(incident[id].size(),id);
    }
    for(const auto& face:faces) { ctx.spend(); if(face.active) result.triangles.push_back(face.vertices); }
    result.workUsed=ctx.used;
    return result;
}
}
