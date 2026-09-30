// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "arguments.h"
#include "bsp_formats.h"
#include "bspfile_abstract.h"
#include "q3mapx/atomic_file.h"
#include "q3mapx/job_pool.h"
#include "q3mapx/planar_reduction.h"
#include "q3mapx/exact_predicates.h"
#include "q3mapx/quake3_materials.h"
#include "rapidjson/prettywriter.h"
#include <glib.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
constexpr size_t inputLimit=512*1024*1024;
std::vector<uint8_t> readSource(const fs::path& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream || stream.tellg()<144 || uint64_t(stream.tellg())>inputLimit)
        throw std::runtime_error("Cannot read BSP or input exceeds 512 MiB");
    std::vector<uint8_t> data(size_t(stream.tellg())); stream.seekg(0);
    if(!stream.read(reinterpret_cast<char*>(data.data()),data.size()) || stream.peek()!=EOF)
        throw std::runtime_error("BSP changed or failed while being read");
    return data;
}
std::string sha(std::span<const uint8_t> bytes) {
    char* value=g_compute_checksum_for_data(G_CHECKSUM_SHA256,bytes.data(),bytes.size());
    if(!value) throw std::runtime_error("Cannot compute input identity");
    std::string result(value); g_free(value); return result;
}
fs::path pathOf(const char* value) {
    return fs::absolute(fs::path(reinterpret_cast<const char8_t*>(value))).lexically_normal();
}
void separate(const fs::path& source,const fs::path& output) {
    std::error_code ec;
    if(fs::weakly_canonical(source)==fs::weakly_canonical(output) || fs::equivalent(source,output,ec))
        throw std::runtime_error("Geometry output must not replace its source or another output");
}
struct Script { std::string name,hash; int occurrence; size_t bytes; };
struct Materials {
    std::map<std::string,q3mapx::ReductionMaterial> entries;
    std::vector<Script> scripts;
};
Materials loadMaterials() {
    Materials result;
    auto files=vfsListShaderFiles(g_game->shaderPath);
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b) {
        return q3mapx::materialKey(a.c_str())<q3mapx::materialKey(b.c_str());
    });
    if(files.size()>4096) throw std::runtime_error("Material file limit exceeded");
    size_t bytes=0;
    for(const auto& file:files) {
        const std::string name=std::string(g_game->shaderPath)+"/"+file.c_str();
        const int count=vfsGetFileCount(name.c_str());
        if(count<=0 || count>128) throw std::runtime_error("Material inventory changed or has excessive duplicates");
        for(int occurrence=0;occurrence<count;++occurrence) {
            auto data=vfsLoadFile(name.c_str(),occurrence,false,std::min(size_t(4*1024*1024),size_t(64*1024*1024)-bytes));
            if(!data) throw std::runtime_error("Cannot read bounded material script: "+name);
            bytes+=data.size();
            result.scripts.push_back({name,sha({static_cast<const uint8_t*>(data.data()),data.size()}),occurrence,data.size()});
            for(auto material:q3mapx::scanReductionMaterials({static_cast<const char*>(data.data()),data.size()})) {
                auto [it,inserted]=result.entries.emplace(material.name,material);
                if(!inserted) it->second.reason="duplicate_material_definition";
                if(result.entries.size()>65536) throw std::runtime_error("Material definition limit exceeded");
            }
        }
    }
    return result;
}
struct Surface {
    std::string material,reason;
    size_t before=0,after=0;
    size_t firstIndex=0;
    q3mapx::PlanarReduction reduction;
};
void allocateIndices(std::vector<Surface>& surfaces) {
    std::vector<std::pair<size_t,size_t>> retained,free;
    std::vector<size_t> changed;
    for(size_t i=0;i<surfaces.size();++i) {
        const auto& s=bspDrawSurfaces[i]; surfaces[i].firstIndex=s.firstIndex;
        if(surfaces[i].after<surfaces[i].before) changed.push_back(i);
        else if(s.numIndexes) retained.emplace_back(s.firstIndex,size_t(s.firstIndex)+s.numIndexes);
    }
    std::sort(retained.begin(),retained.end());
    size_t end=0;
    for(auto [begin,last]:retained) {
        if(begin>end) free.emplace_back(end,begin);
        end=std::max(end,last);
    }
    if(end<bspDrawIndexes.size()) free.emplace_back(end,bspDrawIndexes.size());
    std::sort(changed.begin(),changed.end(),[&](size_t a,size_t b) {
        return surfaces[a].after==surfaces[b].after?a<b:surfaces[a].after>surfaces[b].after;
    });
    // Native emitters share identical index subsequences across surfaces. Reuse
    // only slots not referenced by an unchanged surface; never patch an alias.
    for(size_t i:changed) {
        const size_t needed=surfaces[i].after*3;
        auto block=std::find_if(free.begin(),free.end(),[&](const auto& interval) { return interval.second-interval.first>=needed; });
        if(block==free.end()) throw std::runtime_error("Insufficient contiguous unreferenced index storage; outputs preserved");
        surfaces[i].firstIndex=block->first; block->first+=needed;
    }
}
std::string mappingGuard(std::span<const q3mapx::ReductionVertex> vertices,
                         std::span<const q3mapx::ReductionTriangle> triangles,
                         uint64_t& remaining,uint64_t& used) {
    const auto spend=[&](uint64_t n) {
        if(n>remaining) throw std::runtime_error("Geometry mapping work budget exceeded");
        remaining-=n; used+=n;
    };
    q3mapx::ReductionTriangle basis{}; int sign=0;
    for(const auto& tri:triangles) {
        spend(16);
        const auto point=[&](uint32_t i) { return std::array{vertices[i][0],vertices[i][1]}; };
        const int orientation=q3mapx::orient2Exact(point(tri[0]),point(tri[1]),point(tri[2]));
        if(!sign && orientation) { sign=orientation; basis=tri; }
        else if(orientation && orientation!=sign) return "mixed_surface_winding";
    }
    if(!sign) return "no_positive_area";
    // Global affine UV fields make any pre-existing coplanar overlap use the
    // same samples regardless of within-surface face order. The local removal
    // proof alone does not establish this for discontinuous overlapping charts.
    for(size_t field:{size_t(6),size_t(7),size_t(8),size_t(9)}) {
        const auto point=[&](size_t i) { return std::array{vertices[i][0],vertices[i][1],vertices[i][field]}; };
        for(size_t i=0;i<vertices.size();++i) {
            spend(64);
            if(q3mapx::orient3Exact(point(basis[0]),point(basis[1]),point(basis[2]),point(i)))
                return "non_affine_surface_mapping";
        }
    }
    return {};
}
void integer(std::vector<uint8_t>& data,size_t offset,uint32_t value) {
    if(offset>data.size() || data.size()-offset<4) throw std::runtime_error("Invalid geometry output offset");
    for(unsigned i=0;i<4;++i) data[offset+i]=uint8_t(value>>(8*i));
}
class ReportStream {
public:
    using Ch=char;
    explicit ReportStream(FILE* file):file_(file) {}
    void Put(char c) {
        if(bytes_==64*1024*1024) throw std::runtime_error("Geometry report exceeds 64 MiB");
        if(used_==buffer_.size()) Flush();
        buffer_[used_++]=c; ++bytes_;
    }
    void Flush() { q3mapx::writeOutput(file_,buffer_.data(),used_); used_=0; }
    size_t Tell() const { return bytes_; }
private:
    FILE* file_; std::array<char,65536> buffer_{}; size_t used_=0,bytes_=0;
};
void report(FILE* file,const std::string& inputHash,const std::string& outputHash,bool applied,
            uint64_t budget,const Materials& materials,const std::vector<Surface>& surfaces) {
    ReportStream stream(file); rapidjson::PrettyWriter<ReportStream> w(stream);
    const auto num=[&](const char* name,uint64_t value) { w.Key(name); w.Uint64(value); };
    const auto text=[&](const char* name,const std::string& value) { w.Key(name); w.String(value.c_str()); };
    w.StartObject(); num("schema_version",1); text("mode",applied?"write":"analyze");
    text("profile","quake3"); text("renderer_profile","quake3e-gl");
    text("contract","quake3e_gl_opaque_horizontal_nomarks_nodlight_v1");
    text("source_sha256",inputHash); text("result_sha256",outputHash); num("max_work",budget);
    uint64_t before=0,after=0,work=0,changed=0;
    for(const auto& surface:surfaces) {
        before+=surface.before; after+=surface.after; work+=surface.reduction.workUsed;
        changed+=surface.after<surface.before;
    }
    num("active_triangles_before",before); num("active_triangles_after",after);
    num("changed_surfaces",changed); num("work_used",work);
    text("storage","Original vertices, lump offsets/lengths and file size retained; reduced surfaces use unreferenced index slots");
    text("runtime_requirement","Use the analyzed shader assets and Quake3e OpenGL rendering with nodlight honored in every stage iterator; original Quake III specialized iterators, shader remapping and other renderer extensions are outside this contract");
    w.Key("scripts"); w.StartArray();
    for(const auto& script:materials.scripts) {
        w.StartObject(); text("name",script.name); num("occurrence",script.occurrence);
        text("sha256",script.hash); num("bytes",script.bytes); w.EndObject();
    }
    w.EndArray(); w.Key("surfaces"); w.StartArray();
    for(size_t i=0;i<surfaces.size();++i) {
        const auto& s=surfaces[i]; w.StartObject(); num("surface",i); text("material",s.material);
        text("status",s.reason.empty()?(s.after<s.before?"reduced":"unchanged"):"protected");
        text("reason",s.reason); num("triangles_before",s.before); num("triangles_after",s.after);
        num("removed_vertices",s.reduction.edits.size()); num("work_used",s.reduction.workUsed);
        num("first_index_before",bspDrawSurfaces[i].firstIndex); num("first_index_after",s.firstIndex);
        num("boundary_vertices",s.reduction.boundaryVertices); num("protected_vertices",s.reduction.protectedVertices);
        num("topology_rejections",s.reduction.topologyRejected); num("planarity_rejections",s.reduction.planarRejected);
        num("attribute_rejections",s.reduction.attributeRejected); num("ring_limit_rejections",s.reduction.ringLimitRejected);
        w.EndObject();
    }
    w.EndArray(); w.EndObject(); stream.Put('\n'); stream.Flush();
}
}

int GeometryOptimizeMain(Args& args) try {
    const char* outputName=nullptr; const char* reportName=nullptr; const char* renderer=nullptr;
    uint64_t budget=50'000'000;
    if(args.takeArg("-o")) outputName=args.takeNext();
    if(args.takeArg("-report")) reportName=args.takeNext();
    if(args.takeArg("-renderer")) renderer=args.takeNext();
    if(renderer && std::string_view(renderer)!="quake3e-gl") throw std::runtime_error("Only the quake3e-gl geometry renderer profile is supported");
    if(outputName && !renderer) throw std::runtime_error("Geometry publication requires -renderer quake3e-gl; other renderer contracts are not qualified");
    if(args.takeArg("-max-work")) budget=ParseIntegerOption("-max-work",args.takeNext(),1,1'000'000'000);
    std::set<std::string> excludedMaterials; std::set<size_t> excludedSurfaces;
    while(args.takeArg("-exclude-shader")) excludedMaterials.insert(q3mapx::materialKey(args.takeNext()));
    while(args.takeArg("-exclude-surface")) excludedSurfaces.insert(ParseIntegerOption("-exclude-surface",args.takeNext(),0,INT_MAX));
    if(args.size()!=1 || args.getVector().front()[0]=='-')
        throw std::runtime_error("Usage: q3mapx -game quake3 -optimize-geometry [-renderer quake3e-gl -o result.bsp] [-report file.json] [-max-work N] [-exclude-shader name] [-exclude-surface N] final.bsp");
    if(std::string_view(g_game->arg)!="quake3") throw std::runtime_error("Geometry optimization currently requires the native quake3 profile");
    const char* inputName=args.takeFront(); const auto source=pathOf(inputName);
    auto reportPath=source; reportPath.replace_extension(".geometry.json");
    if(reportName) reportPath=pathOf(reportName);
    if(q3mapx::materialKey(reportPath.extension().string())!=".json") throw std::runtime_error("Geometry report must have a .json extension");
    separate(source,reportPath);
    fs::path outputPath;
    if(outputName) {
        outputPath=pathOf(outputName);
        if(q3mapx::materialKey(outputPath.extension().string())!=".bsp") throw std::runtime_error("Geometry output must have a .bsp extension");
        separate(source,outputPath); separate(reportPath,outputPath);
    }
    auto data=readSource(source);
    if(std::memcmp(data.data(),"IBSP",4) || q3mapx::bspLittleInt(data.data()+4)!=46)
        throw std::runtime_error("Geometry optimization requires IBSP version 46, including with -force");
    const auto directory=q3mapx::inspectBSPDirectory(data,data.size(),q3mapx::bspFormats().front());
    if(!directory.errors.empty()) throw std::runtime_error(directory.errors.front());
    size_t end=144;
    for(const auto& lump:directory.lumps) if(lump.length) end=std::max(end,size_t(lump.offset)+lump.length);
    if(data.size()>(end+3)/4*4 || std::any_of(data.begin()+end,data.end(),[](uint8_t b) { return b!=0; }))
        throw std::runtime_error("Trailing BSP extension data is not supported for geometry optimization");
    const std::string inputHash=sha(data);
    LoadBSPFile(inputName);
    if(bspModels.empty() || bspDrawSurfaces.size()>100000) throw std::runtime_error("Missing world model or surface limit exceeded");
    if(!excludedSurfaces.empty() && *excludedSurfaces.rbegin()>=bspDrawSurfaces.size())
        throw std::runtime_error("Excluded surface is outside the BSP");
    const auto materials=loadMaterials();
    std::vector<Surface> surfaces(bspDrawSurfaces.size());
    std::vector<int> otherOwners(surfaces.size()+1);
    for(size_t i=1;i<bspModels.size();++i) {
        const auto& m=bspModels[i]; ++otherOwners[m.firstBSPSurface]; --otherOwners[m.firstBSPSurface+m.numBSPSurfaces];
    }
    std::partial_sum(otherOwners.begin(),otherOwners.end(),otherOwners.begin());
    std::vector<size_t> candidates; uint64_t weight=0;
    const auto& world=bspModels.front();
    for(size_t i=0;i<surfaces.size();++i) {
        auto& record=surfaces[i]; const auto& s=bspDrawSurfaces[i];
        record.material=q3mapx::materialKey(bspShaders[s.shaderNum].shader);
        record.before=record.after=size_t(s.numIndexes)/3;
        const auto material=materials.entries.find(record.material);
        if(excludedSurfaces.contains(i) || excludedMaterials.contains(record.material)) record.reason="user_excluded";
        else if(s.surfaceType!=MST_PLANAR && s.surfaceType!=MST_TRIANGLE_SOUP) record.reason="unsupported_surface_type";
        else if(i<size_t(world.firstBSPSurface) || i>=size_t(world.firstBSPSurface+world.numBSPSurfaces) || otherOwners[i]) record.reason="non_world_or_shared_owner";
        else if(s.fogNum!=-1) record.reason="fog";
        else if(material==materials.entries.end()) record.reason="missing_explicit_material";
        else if(!material->second.reason.empty()) record.reason=material->second.reason;
        else if(!material->second.marksDisabled || !(bspShaders[s.shaderNum].surfaceFlags&0x20)) record.reason="mark_fragment_topology";
        // Even a horizontal field incurs triangle-dependent finite precision in
        // projected/per-pixel dynamic-light passes. Preserve authored lighting:
        // only surfaces already disabling it in both assets and BSP can qualify.
        else if(!material->second.dynamicLightsDisabled || !(bspShaders[s.shaderNum].surfaceFlags&0x20000)) record.reason="dynamic_light_rasterization";
        else if(material->second.lightmap && (s.lightmapNum[0]<0 || bspLightBytes.empty())) record.reason="missing_baked_lightmap";
        else if(s.numVerts>65536 || s.numIndexes/3>131072) record.reason="surface_geometry_limit";
        else if(s.numIndexes<12) record.reason="no_interior_reduction";
        else {
            const auto& first=bspDrawVerts[s.firstVert];
            if(first.normal[0]!=0 || first.normal[1]!=0 || std::abs(first.normal[2])!=1) record.reason="non_horizontal_normal";
            for(int j=0;j<s.numVerts && record.reason.empty();++j) {
                const auto& v=bspDrawVerts[s.firstVert+j];
                if(v.xyz[2]!=first.xyz[2]) record.reason="non_horizontal_geometry";
                else for(size_t c=0;c<3;++c) if(v.normal[c]!=first.normal[c]) record.reason="varying_normals";
                for(size_t c=0;c<4 && record.reason.empty();++c) if(v.color[0][c]!=first.color[0][c]) record.reason="quantized_vertex_color";
            }
        }
        if(record.reason.empty()) { candidates.push_back(i); weight+=record.before; }
    }
    if(!candidates.empty()) {
        q3mapx::JobPool pool(unsigned(std::min(size_t(numthreads),candidates.size())));
        pool.parallelFor(candidates.size(),[&](size_t item) {
            const size_t i=candidates[item]; const auto& s=bspDrawSurfaces[i];
            q3mapx::ReductionLimits limits; limits.work=std::min(limits.work,budget*surfaces[i].before/weight);
            std::vector<q3mapx::ReductionVertex> vertices(s.numVerts);
            for(size_t j=0;j<vertices.size();++j) {
                const auto& v=bspDrawVerts[s.firstVert+j]; auto& output=vertices[j];
                for(size_t c=0;c<3;++c) { output[c]=v.xyz[c]; output[3+c]=v.normal[c]; }
                for(size_t c=0;c<2;++c) { output[6+c]=v.st[c]; output[8+c]=v.lightmap[0][c]; }
                for(size_t c=0;c<4;++c) output[16+c]=v.color[0][c];
            }
            std::vector<q3mapx::ReductionTriangle> triangles(s.numIndexes/3);
            for(size_t j=0;j<triangles.size();++j) for(size_t c=0;c<3;++c) triangles[j][c]=bspDrawIndexes[s.firstIndex+j*3+c];
            uint64_t guardWork=0;
            surfaces[i].reason=mappingGuard(vertices,triangles,limits.work,guardWork);
            if(!surfaces[i].reason.empty()) { surfaces[i].reduction.workUsed=guardWork; return; }
            surfaces[i].reduction=q3mapx::reducePlanarMesh(vertices,triangles,limits);
            surfaces[i].reduction.workUsed+=guardWork;
            surfaces[i].after=surfaces[i].reduction.triangles.size();
        },1);
    }
    allocateIndices(surfaces);
    uint64_t removed=0,changed=0;
    for(size_t i=0;i<surfaces.size();++i) if(surfaces[i].after<surfaces[i].before) {
        const auto& output=surfaces[i].reduction.triangles;
        integer(data,size_t(directory.lumps[13].offset)+i*104+20,uint32_t(surfaces[i].firstIndex));
        integer(data,size_t(directory.lumps[13].offset)+i*104+24,uint32_t(output.size()*3));
        for(size_t j=0;j<output.size();++j) for(size_t c=0;c<3;++c)
            integer(data,size_t(directory.lumps[11].offset)+(surfaces[i].firstIndex+j*3+c)*4,output[j][c]);
        removed+=surfaces[i].before-surfaces[i].after; ++changed;
    }
    const std::string outputHash=sha(data);
    if(sha(readSource(source))!=inputHash) throw std::runtime_error("BSP changed during geometry analysis; outputs preserved");
    // Re-read every shader source: output depends on the exact inventory and
    // definitions, not only the shaderlist used by the original compiler.
    const auto afterMaterials=loadMaterials();
    if(afterMaterials.scripts.size()!=materials.scripts.size()) throw std::runtime_error("Material inventory changed during analysis");
    for(size_t i=0;i<materials.scripts.size();++i)
        if(materials.scripts[i].name!=afterMaterials.scripts[i].name || materials.scripts[i].hash!=afterMaterials.scripts[i].hash)
            throw std::runtime_error("Material source changed during analysis");
    q3mapx::OutputFiles outputs;
    if(outputName) q3mapx::writeOutput(outputs.open(outputPath),data.data(),data.size());
    report(outputs.open(reportPath),inputHash,outputHash,outputName!=nullptr,budget,materials,surfaces);
    outputs.commit();
    Sys_Printf("Geometry %s: %llu fewer active triangles across %llu surfaces; vertices and BSP size unchanged\n",
        outputName?"output":"proposal",(unsigned long long)removed,(unsigned long long)changed);
    Sys_Printf("Wrote %s\n",reportPath.string().c_str());
    if(outputName) Sys_Printf("Wrote %s\n",outputPath.string().c_str());
    return 0;
}
catch(const std::exception& error) { Sys_FPrintf(SYS_ERR,"Geometry optimization: %s\n",error.what()); return 1; }
