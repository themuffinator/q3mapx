// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "bsp_evidence.h"
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace q3mapx {
namespace {
using Point = std::array<double, 3>;
using Face = std::vector<Point>;
using Cell = std::vector<Face>;
constexpr size_t maxCellPoints = 2048, maxPendingPoints = 8192, maxFaces = 256;
constexpr double maxCoordinate = 1e7, witnessMargin = 0.01, mergeEpsilon = 1e-7;
struct GeometryLimit {};
struct Plane { Point normal{}; double distance = 0; bool valid = false; };
struct Budget {
    uint64_t limit;
    std::atomic<uint64_t> used;
    void spend(uint64_t amount) {
        const auto before = used.fetch_add(amount, std::memory_order_relaxed);
        if (before > limit || amount > limit - before)
            throw std::runtime_error("BSP brush-cell work budget exceeded; no output was published");
    }
};
Point subtract(const Point& a, const Point& b) { return {a[0]-b[0], a[1]-b[1], a[2]-b[2]}; }
Point cross(const Point& a, const Point& b) { return {a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]}; }
double dot(const Point& a, const Point& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
double distance(const Plane& plane, const Point& point) { return dot(plane.normal, point)-plane.distance; }
Plane flip(Plane plane) { for (double& n : plane.normal) n=-n; plane.distance=-plane.distance; return plane; }
size_t points(const Cell& cell) { size_t count=0; for (const auto& face:cell) count+=face.size(); return count; }
void bounded(const Cell& cell) { if (cell.size()>maxFaces || points(cell)>maxCellPoints) throw GeometryLimit{}; }
Point center(const Cell& cell) {
    Point result{}; size_t count=0;
    for (const auto& face:cell) for (const auto& point:face) { for (size_t a=0;a<3;++a) result[a]+=point[a]; ++count; }
    if (count) for (double& value:result) value/=count;
    return result;
}
double volume(const Cell& cell, Budget& budget) {
    budget.spend(points(cell));
    const Point origin=center(cell); double sum=0;
    for (const auto& face:cell) for (size_t i=1;i+1<face.size();++i)
        sum+=std::abs(dot(subtract(face[0],origin),cross(subtract(face[i],origin),subtract(face[i+1],origin))))/6;
    return sum;
}
Cell box(const Point& lo, const Point& hi) {
    std::array<Point,8> corners;
    for (size_t i=0;i<8;++i) for (size_t a=0;a<3;++a) corners[i][a]=(i&(1u<<a))?hi[a]:lo[a];
    Cell cell;
    for (const auto& indices : {std::array{0,2,6,4}, {1,5,7,3}, {0,4,5,1}, {2,3,7,6}, {0,1,3,2}, {4,6,7,5}}) {
        Face face; for (int i:indices) face.push_back(corners[i]); cell.push_back(std::move(face));
    }
    return cell;
}

// Clip a closed convex cell to n.p <= d. Cap vertices come from the actual
// crossing edges; no huge artificial base winding or BSP plane-pair assumptions.
Cell clip(const Cell& cell, const Plane& plane, Budget& budget) {
    Cell result; Face cap;
    size_t outputPoints=0;
    for (const auto& face:cell) {
        budget.spend(face.size());
        Face clipped;
        for (size_t i=0;i<face.size();++i) {
            const auto& a=face[i]; const auto& b=face[(i+1)%face.size()];
            const double da=distance(plane,a), db=distance(plane,b);
            if (da<=0) clipped.push_back(a);
            if ((da<0 && db>0) || (da>0 && db<0)) {
                const double t=da/(da-db);
                Point p; for (size_t axis=0;axis<3;++axis) p[axis]=a[axis]+t*(b[axis]-a[axis]);
                clipped.push_back(p); cap.push_back(p);
            }
            else if (da==0) cap.push_back(a);
        }
        if (clipped.size()>=3) {
            outputPoints+=clipped.size();
            if (outputPoints>maxCellPoints || result.size()==maxFaces) throw GeometryLimit{};
            result.push_back(std::move(clipped));
        }
    }
    budget.spend(uint64_t(cap.size())*cap.size());
    Face unique;
    for (const auto& point:cap) {
        if (std::none_of(unique.begin(),unique.end(),[&](const Point& other) {
            const auto delta=subtract(point,other); return dot(delta,delta)<=mergeEpsilon*mergeEpsilon;
        })) unique.push_back(point);
    }
    if (unique.size()>=3) {
        Point centroid{}; for (const auto& point:unique) for (size_t a=0;a<3;++a) centroid[a]+=point[a]/unique.size();
        size_t axis=0; for (size_t a=1;a<3;++a) if(std::abs(plane.normal[a])<std::abs(plane.normal[axis])) axis=a;
        Point reference{}; reference[axis]=1;
        Point u=cross(plane.normal,reference); const double length=std::sqrt(dot(u,u));
        for (double& value:u) value/=length;
        const Point v=cross(plane.normal,u);
        std::sort(unique.begin(),unique.end(),[&](const Point& a,const Point& b) {
            const auto da=subtract(a,centroid), db=subtract(b,centroid);
            const double aa=std::atan2(dot(da,v),dot(da,u)), ab=std::atan2(dot(db,v),dot(db,u));
            return aa!=ab ? aa<ab : a<b;
        });
        result.push_back(std::move(unique));
    }
    bounded(result);
    return result;
}
std::pair<double,double> range(const Cell& cell,const Plane& plane,Budget& budget) {
    budget.spend(points(cell));
    double lo=std::numeric_limits<double>::infinity(),hi=-lo;
    for (const auto& face:cell) for (const auto& point:face) {
        const double d=distance(plane,point); lo=std::min(lo,d); hi=std::max(hi,d);
    }
    return {lo,hi};
}

BrushCellEvidence analyzeBrush(size_t index, const BSPEvidence& evidence, const std::vector<Plane>& planes, Budget& budget) {
    BrushCellEvidence result;
    const auto& bounds=evidence.brushes[index];
    if (bounds.model!=0) return result;
    if (!evidence.hasWorldHead || !evidence.uniqueNodePaths) { result.status="world_tree_unavailable"; return result; }
    if (!bounds.axialEnclosureAvailable) { result.status="axial_enclosure_unavailable"; return result; }
    for(size_t a=0;a<3;++a) if(std::abs(bounds.mins[a])>maxCoordinate || std::abs(bounds.maxs[a])>maxCoordinate) {
        result.status="coordinate_limit"; return result;
    }
    const auto& brush=bspBrushes[index];
    if (brush.numSides>int(maxFaces)) throw GeometryLimit{};
    std::vector<Plane> brushPlanes;
    Cell initial=box(bounds.mins,bounds.maxs);
    for (int i=0;i<brush.numSides;++i) {
        const auto& plane=planes[bspBrushSides[brush.firstSide+i].planeNum];
        if (!plane.valid) { result.status="invalid_plane"; return result; }
        brushPlanes.push_back(plane);
        const auto [lo,hi]=range(initial,plane,budget);
        if (hi>0) initial=clip(initial,plane,budget);
        if (initial.empty()) { result.status="empty_or_degenerate"; return result; }
    }
    result.brushVolume=volume(initial,budget);
    if (!std::isfinite(result.brushVolume) || result.brushVolume<=1e-9) { result.status="empty_or_degenerate"; return result; }
    struct Frame { int node; Cell cell; size_t pathSize; Plane boundary; bool hasBoundary; };
    std::vector<Frame> stack;
    size_t pendingPoints=points(initial);
    stack.push_back({evidence.worldHead,std::move(initial),0,{},false});
    std::vector<Plane> path;
    while(!stack.empty()) {
        auto frame=std::move(stack.back()); stack.pop_back();
        pendingPoints-=points(frame.cell);
        path.resize(frame.pathSize);
        if (frame.hasBoundary) path.push_back(frame.boundary);
        budget.spend(1);
        if (frame.node<0) {
            ++result.leafFragments;
            result.fragmentVolume+=volume(frame.cell,budget);
            const Point point=center(frame.cell);
            double margin=std::numeric_limits<double>::infinity();
            budget.spend(brushPlanes.size()+path.size());
            for (const auto& plane:brushPlanes) margin=std::min(margin,-distance(plane,point));
            for (const auto& plane:path) margin=std::min(margin,-distance(plane,point));
            if (!std::isfinite(margin) || margin<witnessMargin) { ++result.uncertainFragments; continue; }
            const int leaf=int(-1-int64_t(frame.node)), cluster=bspLeafs[leaf].cluster;
            auto& witness=cluster>=0 ? result.open : result.opaque;
            if (!witness.available || margin>witness.clearance) witness={true,leaf,cluster,margin,point};
            if (cluster>=0) result.interiorClusters.push_back(cluster);
            continue;
        }
        const auto& node=bspNodes[frame.node]; const auto& plane=planes[node.planeNum];
        if (!plane.valid) { result.status="invalid_plane"; return result; }
        const auto [lo,hi]=range(frame.cell,plane,budget);
        const auto push=[&](int child,Cell cell,Plane boundary) {
            if (cell.empty()) return;
            pendingPoints+=points(cell);
            if (pendingPoints>maxPendingPoints) throw GeometryLimit{};
            stack.push_back({child,std::move(cell),path.size(),boundary,true});
        };
        if (hi<=0) push(node.children[1],std::move(frame.cell),plane);
        else if (lo>=0) push(node.children[0],std::move(frame.cell),flip(plane));
        else {
            // Push back first so traversal/witness tie-breaking is stable front-first.
            push(node.children[1],clip(frame.cell,plane,budget),plane);
            push(node.children[0],clip(frame.cell,flip(plane),budget),flip(plane));
        }
    }
    std::sort(result.interiorClusters.begin(),result.interiorClusters.end());
    result.interiorClusters.erase(std::unique(result.interiorClusters.begin(),result.interiorClusters.end()),result.interiorClusters.end());
    if(evidence.visibility.present) {
        budget.spend(uint64_t(result.interiorClusters.size())*result.interiorClusters.size());
        for(int a:result.interiorClusters) for(int b:result.interiorClusters) {
            ++result.testedPVSPairs;
            result.invisiblePVSPairs+=(bspVisBytes[8+size_t(a)*evidence.visibility.rowBytes+size_t(b)/8] & (1u<<(b%8)))==0;
        }
    }
    result.status=std::abs(result.brushVolume-result.fragmentVolume)<=std::max(1e-6,result.brushVolume*1e-6)
        ? "analyzed" : "volume_mismatch";
    return result;
}

struct Context {
    BSPEvidence& evidence;
    const std::vector<Plane>& planes;
    Budget& budget;
    std::atomic<size_t> next{0};
};
Context* active=nullptr; // One synchronous CLI analysis; workers touch separate output elements.
void worker(int) {
    for (size_t i=active->next.fetch_add(1);i<active->evidence.brushes.size();i=active->next.fetch_add(1)) {
        active->budget.spend(1);
        try { active->evidence.brushCells[i]=analyzeBrush(i,active->evidence,active->planes,active->budget); }
        catch(const GeometryLimit&) { active->evidence.brushCells[i].status="geometry_limit"; }
    }
}
}

void analyzeBSPBrushCells(BSPEvidence& evidence,uint64_t workLimit) {
    Budget budget{workLimit,evidence.workUsed};
    budget.spend(bspPlanes.size());
    std::vector<Plane> planes;
    planes.reserve(bspPlanes.size());
    for(const auto& source:bspPlanes) {
        const double length=std::hypot(double(source.normal()[0]),double(source.normal()[1]),double(source.normal()[2]));
        Plane plane;
        if (length>0) {
            for(size_t a=0;a<3;++a) plane.normal[a]=source.normal()[a]/length;
            plane.distance=source.dist()/length; plane.valid=std::isfinite(plane.distance);
        }
        planes.push_back(plane);
    }
    evidence.brushCellsRequested=true; evidence.brushCells.resize(evidence.brushes.size());
    Context context{evidence,planes,budget}; active=&context;
    // Bound simultaneous geometry scratch space while retaining the persistent
    // pool and dynamically distributing uneven brushes among active workers.
    const int tasks=int(std::min<size_t>(32,evidence.brushes.size()));
    RunThreadsOnIndividual(tasks,false,worker,"BrushCellEvidenceWorkers",1);
    active=nullptr; evidence.workUsed=budget.used.load();
}
}
