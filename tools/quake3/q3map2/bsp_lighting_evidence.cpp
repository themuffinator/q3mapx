// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "bsp_lighting_evidence.h"
#include "bspfile_ibsp.h"
#include "bspfile_rbsp.h"
#include <atomic>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace q3mapx {
namespace {
using Point = std::array<double, 3>;
struct Budget {
    uint64_t limit;
    std::atomic<uint64_t> used;
    const char* message;
    void spend(uint64_t amount) {
        const auto before = used.fetch_add(amount, std::memory_order_relaxed);
        if (before > limit || amount > limit - before) throw std::runtime_error(message);
    }
};
Point difference(const Vector3& a, const Vector3& b) {
    return {double(a[0])-b[0], double(a[1])-b[1], double(a[2])-b[2]};
}
double length(const Point& a) { return std::hypot(a[0], a[1], a[2]); }
bool sameMapping(const LightmapObservation& a, const LightmapObservation& b) {
    for (size_t i=0; i<3; ++i) {
        if (std::abs(a.position[i]-b.position[i]) > 1e-4 + 1e-9*std::max(std::abs(a.position[i]),std::abs(b.position[i]))) return false;
        if (std::abs(a.normal[i]-b.normal[i]) > 1e-5) return false;
    }
    return true;
}

void sampleSurface(size_t index, LightingEvidence& result, Budget& work, Budget& samples) {
    work.spend(1);
    const auto& surface = bspDrawSurfaces[index];
    auto& target = result.surfaces[index];
    const int size = result.pageSize, stride = int(result.options.stride);
    // One dense lookup per active worker, reused across styles. Never combine
    // observations across surfaces: coincident charts may refer to other models.
    std::vector<int> lookup;
    for (int slot=0; slot<MAX_LIGHTMAPS; ++slot) {
        auto& output = target.slots[slot];
        if (surface.lightmapStyles[slot] >= LS_UNUSED) continue;
        const int page = surface.lightmapNum[slot];
        if (page < 0) { output.status = "no_internal_page"; continue; }
        if (result.atlasStatus != std::string_view("available") || uint64_t(page) >= result.pages) {
            output.status = "invalid_or_unavailable_page"; continue;
        }
        if (surface.surfaceType == MST_PATCH) { output.status = "patch_parameterization_pending"; continue; }
        if (surface.surfaceType != MST_PLANAR && surface.surfaceType != MST_TRIANGLE_SOUP) {
            output.status = "unsupported_surface_type"; continue;
        }
        output.status = surface.numIndexes ? "analyzed" : "no_indexed_triangles";
        work.spend(uint64_t(size)*size);
        lookup.assign(size_t(size)*size, -1);
        for (int triangle=0; triangle<surface.numIndexes/3; ++triangle) {
            work.spend(1);
            std::array<const bspDrawVert_t*,3> v;
            std::array<std::array<double,2>,3> uv;
            for (int corner=0; corner<3; ++corner) {
                v[corner] = &bspDrawVerts[surface.firstVert + bspDrawIndexes[surface.firstIndex+triangle*3+corner]];
                for (int a=0; a<2; ++a) uv[corner][a] = double(v[corner]->lightmap[slot][a])*size;
            }
            const auto a = difference(v[1]->xyz,v[0]->xyz), b = difference(v[2]->xyz,v[0]->xyz);
            if (length({a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}) == 0) {
                ++output.degenerateGeometryTriangles; continue;
            }
            const double ax=uv[1][0]-uv[0][0], ay=uv[1][1]-uv[0][1];
            const double bx=uv[2][0]-uv[0][0], by=uv[2][1]-uv[0][1];
            const double det=ax*by-ay*bx, scale=std::max({std::abs(ax),std::abs(ay),std::abs(bx),std::abs(by)});
            if (std::abs(det) <= 1e-12*scale*scale) { ++output.degenerateUVTriangles; continue; }
            std::array<int,2> lo, hi;
            for (int axis=0; axis<2; ++axis) {
                const double low=std::min({uv[0][axis],uv[1][axis],uv[2][axis]})-0.5;
                const double high=std::max({uv[0][axis],uv[1][axis],uv[2][axis]})-0.5;
                // Clamp in floating point before any integer conversion; finite
                // binary32 UVs can still be far outside the atlas.
                lo[axis]=int(std::ceil(std::clamp(low-1e-8,0.0,double(size))));
                hi[axis]=int(std::floor(std::clamp(high+1e-8,-1.0,double(size-1))));
                lo[axis]=((lo[axis]+stride-1)/stride)*stride;
            }
            for (int y=lo[1]; y<=hi[1]; y+=stride) for (int x=lo[0]; x<=hi[0]; x+=stride) {
                work.spend(1); ++output.candidateTexels;
                const double dx=x+0.5-uv[0][0], dy=y+0.5-uv[0][1];
                const double w1=(dx*by-dy*bx)/det, w2=(ax*dy-ay*dx)/det;
                const std::array<double,3> weights{1-w1-w2,w1,w2};
                if (*std::min_element(weights.begin(),weights.end()) < -1e-9) continue;
                LightmapObservation observation;
                observation.x=x; observation.y=y; observation.firstTriangle=triangle; observation.triangleHits=1;
                observation.boundary=*std::min_element(weights.begin(),weights.end()) <= 1e-9;
                for (int axis=0; axis<3; ++axis) for (int corner=0; corner<3; ++corner) {
                    observation.position[axis]+=weights[corner]*v[corner]->xyz[axis];
                    observation.normal[axis]+=weights[corner]*v[corner]->normal[axis];
                }
                const double magnitude=length(observation.normal);
                if (magnitude > 0) for (auto& value:observation.normal) value/=magnitude;
                auto& entry=lookup[size_t(y)*size+x];
                if (entry < 0) {
                    samples.spend(1);
                    entry=int(output.observations.size()); output.observations.push_back(observation);
                }
                else {
                    auto& existing=output.observations[entry];
                    ++existing.triangleHits;
                    existing.ambiguous |= !sameMapping(existing,observation);
                    existing.boundary |= observation.boundary;
                }
            }
        }
        // Stable atlas order independent of worker scheduling. Triangle IDs
        // retain the native order; no claim of invariance to BSP re-triangulation.
        std::sort(output.observations.begin(),output.observations.end(),[](const auto& a,const auto& b) {
            return a.y!=b.y ? a.y<b.y : a.x<b.x;
        });
    }
}

bool parsePitch(const char* value, Point& pitch) {
    std::string_view text(value);
    for (double& component:pitch) {
        const auto begin=text.find_first_not_of(" \t\r\n");
        if (begin==text.npos) return false;
        text.remove_prefix(begin);
        const size_t end=text.find_first_of(" \t\r\n");
        auto token=text.substr(0,end);
        text.remove_prefix(token.size());
        if (!token.empty() && token.front()=='+') token.remove_prefix(1);
        const auto parsed=std::from_chars(token.data(),token.data()+token.size(),component);
        if (parsed.ec!=std::errc{} || parsed.ptr!=token.data()+token.size() || !std::isfinite(component) || component<=0) return false;
    }
    return text.find_first_not_of(" \t\r\n")==text.npos;
}
void locateGrid(LightingGridEvidence& grid) {
    if (bspGridPoints.empty()) return;
    grid.status="world_bounds_unavailable";
    if (bspModels.empty() || entities.empty() || !entities[0].classname_is("worldspawn")) return;
    const char* value=entities[0].valueForKey("gridsize");
    grid.storedPitch=*value!=0;
    if (grid.storedPitch && !parsePitch(value,grid.pitch)) { grid.status="invalid_stored_pitch"; return; }
    uint64_t total=1;
    for (size_t a=0; a<3; ++a) {
        const double lo=std::ceil(double(bspModels[0].minmax.mins[a])/grid.pitch[a]);
        const double hi=std::floor(double(bspModels[0].minmax.maxs[a])/grid.pitch[a]);
        const double count=hi-lo+1;
        grid.origin[a]=lo*grid.pitch[a];
        if (!std::isfinite(count) || !std::isfinite(grid.origin[a]) || count<1 || count>double(bspGridPoints.size())) {
            grid.status="layout_count_mismatch"; return;
        }
        grid.dimensions[a]=uint64_t(count);
        if (grid.dimensions[a]>bspGridPoints.size()/total) { grid.status="layout_count_mismatch"; return; }
        total*=grid.dimensions[a];
    }
    if (total!=bspGridPoints.size()) { grid.status="layout_count_mismatch"; return; }
    grid.status="conventional_layout_count_matches";
    grid.positionAvailable=true;
}
struct Context { LightingEvidence& result; Budget& work; Budget& samples; std::atomic<size_t> next{0}; };
Context* active=nullptr;
struct ActiveGuard { ~ActiveGuard() { active=nullptr; } };
void worker(int) {
    for (size_t i=active->next.fetch_add(1);i<active->result.surfaces.size();i=active->next.fetch_add(1))
        sampleSurface(i,active->result,active->work,active->samples);
}
}

LightingEvidence analyzeBSPLighting(BSPEvidence& evidence,uint64_t workLimit,LightingEvidenceOptions options) {
    LightingEvidence result; result.options=options;
    if (g_game->load!=LoadIBSPFile && g_game->load!=LoadRBSPFile) return result;
    if (!options.stride || options.stride>1024 || !options.maxObservations || options.maxObservations>200'000)
        throw std::runtime_error("Invalid lighting evidence sampling limits");
    if (bspDrawSurfaces.size()>200'000 || bspDrawVerts.size()>2'000'000 || bspGridPoints.size()>2'000'000)
        throw std::runtime_error("Lighting evidence exceeds source record limits; no report was published");
    Budget work{workLimit,evidence.workUsed,"BSP lighting work budget exceeded; no report was published"};
    Budget samples{options.maxObservations,0,"BSP lighting observation limit exceeded; increase -lighting-stride or -lighting-max-samples; no report was published"};
    result.status="observations_only";
    result.pageSize=g_game->lightmapSize;
    if (result.pageSize<=0 || result.pageSize>512) throw std::runtime_error("Unsupported lighting evidence atlas dimensions");
    const uint64_t pageBytes=uint64_t(result.pageSize)*result.pageSize*3;
    result.pages=bspLightBytes.size()/pageBytes;
    result.atlasStatus=bspLightBytes.empty()?"absent":bspLightBytes.size()%pageBytes?"invalid_lump_length":"available";
    work.spend(bspDrawSurfaces.size()+bspModels.size());
    result.surfaces.resize(bspDrawSurfaces.size());
    // Prefix ownership avoids walking overlapping model ranges quadratically.
    std::vector<int64_t> owners(bspDrawSurfaces.size()+1), ownerSum(owners.size());
    for (size_t i=0; i<bspModels.size(); ++i) {
        const auto& model=bspModels[i];
        const size_t first=model.firstBSPSurface,end=first+model.numBSPSurfaces;
        ++owners[first]; --owners[end]; ownerSum[first]+=int64_t(i); ownerSum[end]-=int64_t(i);
    }
    int64_t count=0,sum=0;
    std::vector<bool> referenced(result.pages,false);
    for (size_t i=0; i<result.surfaces.size(); ++i) {
        count+=owners[i]; sum+=ownerSum[i];
        auto& target=result.surfaces[i]; const auto& source=bspDrawSurfaces[i];
        target.model=count==1?int(sum):count?-2:-1;
        for (int slot=0; slot<MAX_LIGHTMAPS; ++slot) {
            if (source.vertexStyles[slot]<LS_UNUSED) target.vertexObservations+=(uint64_t(source.numVerts)+options.stride-1)/options.stride;
            if (source.lightmapStyles[slot]<LS_UNUSED && source.lightmapNum[slot]>=0 && uint64_t(source.lightmapNum[slot])<result.pages)
                referenced[source.lightmapNum[slot]]=true;
        }
        work.spend(target.vertexObservations); samples.spend(target.vertexObservations);
    }
    result.referencedPages=std::count(referenced.begin(),referenced.end(),true);
    locateGrid(result.grid);
    for (size_t i=0; i<bspGridPoints.size(); i+=options.stride) {
        work.spend(1);
        for (int slot=0; slot<MAX_LIGHTMAPS; ++slot) if (bspGridPoints[i].styles[slot]<LS_UNUSED) ++result.grid.observations;
    }
    samples.spend(result.grid.observations);
    Context context{result,work,samples}; active=&context; ActiveGuard guard;
    RunThreadsOnIndividual(int(std::min<size_t>(32,result.surfaces.size())),false,worker,"LightingEvidenceWorkers",1);
    result.observations=samples.used.load(); evidence.workUsed=work.used.load();
    return result;
}
}
