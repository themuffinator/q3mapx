// SPDX-License-Identifier: GPL-3.0-or-later
#include "cell_graph.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace q3mapx {
namespace {
using Polygon=std::vector<CellPoint>;
struct Face { CellPlane plane; Polygon points; int node=-1; bool front=false; };
using Cell=std::vector<Face>;
constexpr double mergeDistance=cellVertexMergeDistance, minArea=cellMinimumArea;
struct Context {
    const CellLimits& limits;
    uint64_t used, degenerates=0;
    void spend(uint64_t n) {
        if(used>limits.work || n>limits.work-used) throw std::runtime_error("Cell adjacency work budget exceeded; no report was published");
        used+=n;
    }
    [[noreturn]] void limit() const { throw std::runtime_error("Cell adjacency geometry limit exceeded; no report was published"); }
};
CellPoint sub(const CellPoint& a,const CellPoint& b) { return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
CellPoint cross(const CellPoint& a,const CellPoint& b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double dot(const CellPoint& a,const CellPoint& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
double distance(const CellPlane& plane,const CellPoint& p) { return dot(plane.normal,p)-plane.distance; }
CellPlane flip(CellPlane plane) { for(double& n:plane.normal) n=-n; plane.distance=-plane.distance; return plane; }
size_t points(const Cell& cell) { size_t n=0; for(const auto& f:cell) n+=f.points.size(); return n; }
double area(const Polygon& p) {
    CellPoint sum{};
    for(size_t i=1;i+1<p.size();++i) {
        const auto c=cross(sub(p[i],p[0]),sub(p[i+1],p[0]));
        for(size_t a=0;a<3;++a) sum[a]+=c[a];
    }
    return std::sqrt(dot(sum,sum))*0.5;
}
void bounded(const Cell& cell,Context& ctx) {
    if(cell.size()>ctx.limits.cellFaces || points(cell)>ctx.limits.cellPoints) ctx.limit();
}
Cell box(const CellPoint& lo,const CellPoint& hi) {
    std::array<CellPoint,8> corners;
    for(size_t i=0;i<8;++i) for(size_t a=0;a<3;++a) corners[i][a]=(i&(1u<<a))?hi[a]:lo[a];
    Cell result;
    for(const auto& indices:{std::array{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}}) {
        Face face{}; const size_t side=result.size(),axis=side/2;
        face.plane.normal[axis]=side%2?1:-1;
        face.plane.distance=side%2?hi[axis]:-lo[axis];
        for(int i:indices) face.points.push_back(corners[i]);
        result.push_back(std::move(face));
    }
    return result;
}
// Strict double clipping to n.p <= d; no artificial oversized base winding.
Polygon clipPolygon(const Polygon& input,const CellPlane& plane,Context& ctx,Polygon* cap=nullptr) {
    ctx.spend(input.size());
    Polygon output;
    for(size_t i=0;i<input.size();++i) {
        const auto& a=input[i]; const auto& b=input[(i+1)%input.size()];
        const double da=distance(plane,a),db=distance(plane,b);
        if(da<=0) output.push_back(a);
        if((da<0 && db>0) || (da>0 && db<0)) {
            const double t=da/(da-db); CellPoint p;
            for(size_t k=0;k<3;++k) p[k]=a[k]+t*(b[k]-a[k]);
            output.push_back(p); if(cap) cap->push_back(p);
        }
        else if(da==0 && cap) cap->push_back(a);
        if(output.size()>ctx.limits.cellPoints || (cap && cap->size()>ctx.limits.cellPoints)) ctx.limit();
    }
    return output;
}
Cell clipCell(const Cell& input,const CellPlane& plane,int node,bool front,Context& ctx) {
    Cell output; Polygon cap;
    for(const auto& face:input) {
        auto clipped=clipPolygon(face.points,plane,ctx,&cap);
        if(clipped.size()>=3) {
            const double a=area(clipped);
            if(a>minArea) output.push_back({face.plane,std::move(clipped),face.node,face.front});
            else if(a>0) ++ctx.degenerates;
        }
    }
    ctx.spend(uint64_t(cap.size())*cap.size());
    Polygon unique;
    for(const auto& p:cap) if(std::none_of(unique.begin(),unique.end(),[&](const auto& q) {
        const auto delta=sub(p,q); return dot(delta,delta)<=mergeDistance*mergeDistance;
    })) unique.push_back(p);
    if(unique.size()>=3) {
        CellPoint center{};
        for(const auto& p:unique) for(size_t a=0;a<3;++a) center[a]+=p[a]/unique.size();
        size_t axis=0;
        for(size_t a=1;a<3;++a) if(std::abs(plane.normal[a])<std::abs(plane.normal[axis])) axis=a;
        CellPoint ref{}; ref[axis]=1;
        auto u=cross(plane.normal,ref); const double length=std::sqrt(dot(u,u));
        for(double& v:u) v/=length;
        const auto v=cross(plane.normal,u);
        std::sort(unique.begin(),unique.end(),[&](const auto& p,const auto& q) {
            const auto a=sub(p,center),b=sub(q,center);
            const double ap=std::atan2(dot(a,v),dot(a,u)),bp=std::atan2(dot(b,v),dot(b,u));
            return ap!=bp?ap<bp:p<q;
        });
        const double a=area(unique);
        if(a>minArea) output.push_back({plane,std::move(unique),node,front});
        else if(a>0) ++ctx.degenerates;
    }
    else if(!cap.empty()) ++ctx.degenerates;
    bounded(output,ctx); return output;
}
ReconstructedCell measure(const Cell& cell,int leaf,int cluster,Context& ctx) {
    ReconstructedCell result; result.leaf=leaf; result.cluster=cluster; result.faces=unsigned(cell.size());
    result.mins.fill(std::numeric_limits<double>::infinity());
    result.maxs.fill(-std::numeric_limits<double>::infinity());
    const size_t count=points(cell); ctx.spend(count);
    for(const auto& f:cell) for(const auto& p:f.points) for(size_t a=0;a<3;++a) {
        result.center[a]+=p[a]/count;
        result.mins[a]=std::min(result.mins[a],p[a]); result.maxs[a]=std::max(result.maxs[a],p[a]);
    }
    for(const auto& f:cell) for(size_t i=1;i+1<f.points.size();++i)
        result.volume+=std::abs(dot(sub(f.points[0],result.center),cross(sub(f.points[i],result.center),sub(f.points[i+1],result.center))))/6;
    return result;
}
Polygon intersection(Polygon polygon,const Face& other,Context& ctx) {
    for(size_t i=0;i<other.points.size() && polygon.size()>=3;++i) {
        const auto& p=other.points[i]; const auto edge=sub(other.points[(i+1)%other.points.size()],p);
        auto normal=cross(edge,other.plane.normal); const double length=std::sqrt(dot(normal,normal));
        if(length<=mergeDistance) continue;
        for(double& n:normal) n/=length;
        polygon=clipPolygon(polygon,{normal,dot(normal,p)},ctx);
    }
    return polygon;
}
}

CellGraph reconstructCellGraph(std::span<const CellNode> nodes,std::span<const int> clusters,
    int head,CellPoint mins,CellPoint maxs,const CellLimits& limits,uint64_t used) {
    Context ctx{limits,used}; ctx.spend(nodes.size()); ctx.spend(clusters.size());
    if(nodes.size()>2'000'000 || clusters.size()>2'000'000) ctx.limit();
    for(size_t a=0;a<3;++a)
        if(!std::isfinite(mins[a]) || !std::isfinite(maxs[a]) || mins[a]>=maxs[a]
            || std::abs(mins[a])>cellCoordinateLimit || std::abs(maxs[a])>cellCoordinateLimit)
            throw std::runtime_error("Cell adjacency requires a finite, nonempty enclosure within 10000000 units");
    // Validate even when called outside the native loader. Shared leaves are
    // supported as distinct paths; shared internal nodes or cycles are not.
    std::vector<unsigned char> seen(nodes.size()); std::vector<int> todo{head};
    std::vector<CellPlane> planes(nodes.size());
    while(!todo.empty()) {
        const int id=todo.back(); todo.pop_back(); ctx.spend(1);
        if(id<0) {
            if(uint64_t(-1-int64_t(id))>=clusters.size()) throw std::runtime_error("Invalid cell adjacency leaf reference");
            continue;
        }
        if(size_t(id)>=nodes.size() || seen[id]++) throw std::runtime_error("Cell adjacency requires unique, acyclic world node paths");
        auto plane=nodes[id].plane;
        const double length=std::hypot(plane.normal[0],plane.normal[1],plane.normal[2]);
        if(!std::isfinite(length) || length<=0 || !std::isfinite(plane.distance)) throw std::runtime_error("Invalid cell adjacency plane");
        for(double& n:plane.normal) n/=length;
        plane.distance/=length;
        if(!std::isfinite(plane.distance)) throw std::runtime_error("Invalid normalized cell adjacency plane");
        planes[id]=plane;
        for(int child:nodes[id].children) todo.push_back(child);
    }
    CellGraph result; result.mins=mins; result.maxs=maxs;
    result.enclosureVolume=(maxs[0]-mins[0])*(maxs[1]-mins[1])*(maxs[2]-mins[2]);
    struct Frame { int node; Cell cell; };
    std::vector<Frame> pending; pending.push_back({head,box(mins,maxs)});
    size_t pendingPoints=points(pending[0].cell),storedPoints=0;
    if(pendingPoints>limits.pendingPoints) ctx.limit();
    struct Boundary { size_t cell; Face face; CellPoint mins,maxs; };
    std::vector<Boundary> boundaries;
    std::vector<std::array<std::vector<size_t>,2>> groups(nodes.size());
    while(!pending.empty()) {
        auto frame=std::move(pending.back()); pending.pop_back();
        pendingPoints-=points(frame.cell); ctx.spend(1); bounded(frame.cell,ctx);
        if(frame.node<0) {
            if(result.cells.size()==limits.cells) ctx.limit();
            const int leaf=int(-1-int64_t(frame.node)),cluster=clusters[leaf];
            const size_t cell=result.cells.size(); auto measured=measure(frame.cell,leaf,cluster,ctx);
            if(!std::isfinite(measured.volume) || measured.volume<=0) { ++ctx.degenerates; continue; }
            result.cellVolume+=measured.volume; result.cells.push_back(measured);
            for(auto& face:frame.cell) {
                if(face.node<0) { result.enclosedOpenFaces+=cluster>=0; continue; }
                storedPoints+=face.points.size();
                if(storedPoints>limits.points || boundaries.size()==limits.faces) ctx.limit();
                Boundary b{cell,std::move(face),{},{}};
                b.mins.fill(std::numeric_limits<double>::infinity()); b.maxs.fill(-std::numeric_limits<double>::infinity());
                for(const auto& p:b.face.points) for(size_t a=0;a<3;++a) { b.mins[a]=std::min(b.mins[a],p[a]); b.maxs[a]=std::max(b.maxs[a],p[a]); }
                groups[b.face.node][b.face.front?0:1].push_back(boundaries.size());
                boundaries.push_back(std::move(b));
            }
            continue;
        }
        const auto& plane=planes[frame.node];
        double lo=std::numeric_limits<double>::infinity(),hi=-lo;
        ctx.spend(points(frame.cell));
        for(const auto& f:frame.cell) for(const auto& p:f.points) { const double d=distance(plane,p); lo=std::min(lo,d); hi=std::max(hi,d); }
        const auto push=[&](int child,Cell cell) {
            if(cell.empty()) return;
            pendingPoints+=points(cell); if(pendingPoints>limits.pendingPoints) ctx.limit();
            pending.push_back({child,std::move(cell)});
        };
        if(hi<=0) push(nodes[frame.node].children[1],std::move(frame.cell));
        else if(lo>=0) push(nodes[frame.node].children[0],std::move(frame.cell));
        else {
            push(nodes[frame.node].children[1],clipCell(frame.cell,plane,frame.node,false,ctx));
            push(nodes[frame.node].children[0],clipCell(frame.cell,flip(plane),frame.node,true,ctx));
        }
    }
    size_t outputPoints=0;
    for(size_t node=0;node<groups.size();++node) for(size_t a:groups[node][0]) for(size_t b:groups[node][1]) {
        ctx.spend(1);
        const auto& front=boundaries[a]; const auto& back=boundaries[b];
        if(result.cells[front.cell].cluster<0 && result.cells[back.cell].cluster<0) continue;
        bool separated=false;
        for(size_t axis=0;axis<3;++axis) separated|=front.maxs[axis]+mergeDistance<back.mins[axis] || back.maxs[axis]+mergeDistance<front.mins[axis];
        if(separated) continue;
        auto polygon=intersection(front.face.points,back.face,ctx);
        if(polygon.size()<3) continue;
        const double size=area(polygon);
        if(size<=minArea) { ctx.degenerates+=size>0; continue; }
        outputPoints+=polygon.size();
        if(outputPoints>limits.points || result.interfaces.size()==limits.faces) ctx.limit();
        result.interfaces.push_back({front.cell,back.cell,int(node),size,std::move(polygon)});
    }
    result.workUsed=ctx.used; result.degenerateFragments=ctx.degenerates;
    const bool balanced=std::abs(result.enclosureVolume-result.cellVolume)
        <=std::max(cellVolumeAbsoluteTolerance,result.enclosureVolume*cellVolumeRelativeTolerance);
    result.status=balanced?"reconstructed":"volume_mismatch";
    return result;
}
}
