// SPDX-License-Identifier: GPL-3.0-or-later
// Independent IBSP 43/44/45 recovery from documented record observations.
// References and differences from engine translation: docs/GAME-COVERAGE.md.
#include "bspfile_abstract.h"
#include "bspfile_ibsp.h"
#include "bspfile_early.h"
#include "bsp_formats.h"
#include <cmath>
#include <map>
#include <tuple>

int bspEarlyVersion=0;
std::vector<BSPEarlyModel> bspEarlyModels;
std::vector<int> bspEarlyBrushContents, bspEarlySideFlags;
std::vector<int> bspEarlySurfaceSources, bspEarlyBrushSources;

void ResetEarlyBSPRecovery() {
    bspEarlyVersion=0; bspEarlyModels.clear();
    bspEarlyBrushContents.clear(); bspEarlySideFlags.clear();
    bspEarlySurfaceSources.clear(); bspEarlyBrushSources.clear();
}

namespace {
int integer(const byte* p) { return q3mapx::bspLittleInt(p); }
float real(const byte* p) {
    const float f=std::bit_cast<float>(integer(p));
    if(!std::isfinite(f)) Error("Invalid BSP: non-finite early model or surface value");
    return f;
}
void range(int first,int count,size_t capacity,const char* name) {
    if(first<0 || count<0 || size_t(first)>capacity || size_t(count)>capacity-size_t(first))
        Error("Invalid BSP: early %s range is out of bounds",name);
}
std::string name(const byte* p) {
    const auto* end=static_cast<const byte*>(std::memchr(p,0,64));
    if(!end) Error("Invalid BSP: unterminated early shader name");
    return {reinterpret_cast<const char*>(p),size_t(end-p)};
}
}

void LoadEarlyBSPFile(const char* filename) {
    const MemBuffer file=LoadFile(filename);
    const int version=g_game->bspVersion;
    const auto header=ReadBSPHeader(file,version==45 ? 17 : version==44 ? 15 : 14);
    if(std::memcmp(header.ident,"IBSP",4) || header.version!=version)
        Error("Invalid BSP: %s requires IBSP version %d (use -inspect to identify this file)",g_game->arg,version);
    const auto& formats=q3mapx::bspFormats();
    const auto format=std::find_if(formats.begin(),formats.end(),[&](const auto& f) {
        return strEqual(f.ident,"IBSP") && f.version==version;
    });
    if(format==formats.end()) Error("Unsupported early BSP version");
    const auto directory=q3mapx::inspectBSPDirectory({static_cast<const uint8_t*>(file.data()),file.size()},file.size(),*format);
    if(!directory.errors.empty()) Error("Invalid BSP: %s",directory.errors.front().c_str());
    const bool early=version!=45;
    const int planes=early?1:2, models=early?6:7, fogs=early?13:12;
    const auto* bytes=static_cast<const byte*>(file.data());
    const auto record=[&](int lump,size_t i,size_t stride){return bytes+header.lumps[lump].offset+i*stride;};
    const auto count=[&](int lump,size_t stride){return size_t(header.lumps[lump].length)/stride;};
    if(count(models,early?48:56)>65536 || (early && (count(12,version==43?156:164)>1048576 || count(7,12)>1048576)))
        Error("Invalid BSP: early recovery exceeds the model or geometry limit");

    bspHeader_t canonical=header;
    if(early) {
        // Common records are read directly. Missing and incompatible records
        // are populated below, without constructing another whole BSP image.
        constexpr int slots[17]={0,-1,-1,2,3,4,5,-1,-1,-1,11,-1,-1,-1,9,-1,10};
        canonical={};
        for(size_t i=0;i<std::size(slots);++i) if(slots[i]>=0) canonical.lumps[i]=header.lumps[slots[i]];
        if(version==44) canonical.lumps[11]=header.lumps[14];
    } else {
        canonical.lumps[2]={}; canonical.lumps[7]={}; canonical.lumps[12]={};
    }
    LoadIBSPGeometry(canonical,file);
    bspAds.clear(); bspEarlyVersion=version;
    for(size_t i=0;i<count(planes,20);++i) {
        bspPlane_t plane;
        std::memcpy(&plane,record(planes,i,20),16); // retain little endian until the common swap
        bspPlanes.push_back(plane);
    }
    for(size_t i=0;i<count(models,early?48:56);++i) {
        const auto* p=record(models,i,early?48:56);
        bspModel_t model{};
        for(int j=0;j<3;++j) {
            model.minmax.mins[j]=LittleFloat(real(p+j*4));
            model.minmax.maxs[j]=LittleFloat(real(p+12+j*4));
        }
        BSPEarlyModel metadata{};
        for(int j=0;j<3;++j) metadata.origin[j]=real(p+24+j*4);
        metadata.headNode=integer(p+36);
        metadata.declaredFirstSurface=integer(p+40); metadata.declaredSurfaceCount=integer(p+44);
        bspEarlyModels.push_back(metadata);
        if(!early) {
            model.firstBSPSurface=LittleLong(integer(p+40)); model.numBSPSurfaces=LittleLong(integer(p+44));
            model.firstBSPBrush=LittleLong(integer(p+48)); model.numBSPBrushes=LittleLong(integer(p+52));
        }
        // 43/44 ranges are derived from validated model trees after the common swap.
        bspModels.push_back(model);
    }
    for(size_t i=0;i<count(fogs,68);++i) {
        const auto* p=record(fogs,i,68);
        bspFog_t fog{}; const auto shader=name(p);
        std::memcpy(fog.shader,shader.c_str(),shader.size()+1);
        fog.brushNum=LittleLong(integer(p+64)); fog.visibleSide=LittleLong(-1);
        bspFogs.push_back(fog);
    }
    if(!early) return;

    std::map<std::tuple<std::string,int,int>,int> shaders;
    const auto shader=[&](const std::string& texture,int flags,int contents) {
        const auto key=std::make_tuple(texture,flags,contents);
        if(const auto it=shaders.find(key);it!=shaders.end()) return it->second;
        if(bspShaders.size()>=1048576) Error("Invalid BSP: early recovery exceeds the shader limit");
        bspShader_t out{}; std::memcpy(out.shader,texture.c_str(),texture.size()+1);
        out.surfaceFlags=LittleLong(flags); out.contentFlags=LittleLong(contents);
        const int id=int(bspShaders.size()); bspShaders.push_back(out); shaders.emplace(key,id); return id;
    };
    // Brush records have contents and side records have flags, but no names.
    // Visible face materials are inferred by decompilation's spatial matcher.
    for(size_t i=0;i<count(7,12);++i) {
        const auto* p=record(7,i,12); const int contents=integer(p+8);
        bspBrushes.push_back({LittleLong(integer(p)),LittleLong(integer(p+4)),LittleLong(shader("textures/common/caulk",0,contents))});
        bspEarlyBrushContents.push_back(contents);
    }
    for(size_t i=0;i<count(8,8);++i) {
        const auto* p=record(8,i,8); const int flags=integer(p+4);
        bspBrushSides.push_back({LittleLong(integer(p)),LittleLong(shader("textures/common/caulk",flags,0)),LittleLong(-1)});
        bspEarlySideFlags.push_back(flags);
    }
    for(size_t i=0;i<count(12,version==43?156:164);++i) {
        const auto* p=record(12,i,version==43?156:164);
        const auto texture=name(p);
        const int first=integer(p+72), vertices=integer(p+76), patchOffset=version==43?80:88;
        const int width=integer(p+patchOffset), height=integer(p+patchOffset+4), lm=patchOffset+8;
        range(first,vertices,bspDrawVerts.size(),"surface vertices");
        bspDrawSurface_t surface{};
        const auto type=(width || height) ? MST_PATCH : (version==44 && integer(p+84)!=0) ? MST_TRIANGLE_SOUP : MST_PLANAR;
        const int contents=version==44 && type==MST_PATCH && texture=="textures/sfx/powerupshit" ? 0 : 1;
        surface.shaderNum=LittleLong(shader(texture,0,contents)); surface.fogNum=LittleLong(integer(p+64));
        surface.surfaceType=bspSurfaceType_t(LittleLong(type));
        surface.firstVert=LittleLong(first); surface.numVerts=LittleLong(vertices);
        surface.patchWidth=LittleLong(width); surface.patchHeight=LittleLong(height);
        surface.lightmapStyles={LS_NORMAL,LS_NONE,LS_NONE,LS_NONE}; surface.vertexStyles={LS_NORMAL,LS_NONE,LS_NONE,LS_NONE};
        surface.lightmapNum={LittleLong(integer(p+lm)),LittleLong(-1),LittleLong(-1),LittleLong(-1)};
        surface.lightmapX[0]=LittleLong(integer(p+lm+4)); surface.lightmapY[0]=LittleLong(integer(p+lm+8));
        surface.lightmapWidth=LittleLong(integer(p+lm+12)); surface.lightmapHeight=LittleLong(integer(p+lm+16));
        for(int j=0;j<3;++j) {
            surface.lightmapOrigin[j]=LittleFloat(real(p+lm+20+j*4));
            for(int k=0;k<3;++k) surface.lightmapVecs[j][k]=LittleFloat(real(p+lm+32+j*12+k*4));
        }
        if(type==MST_TRIANGLE_SOUP) {
            surface.firstIndex=LittleLong(integer(p+80)); surface.numIndexes=LittleLong(integer(p+84));
        } else if(type==MST_PLANAR && vertices>=3) {
            const uint64_t added=uint64_t(vertices-2)*3;
            if(added+bspDrawIndexes.size()>16*1024*1024) Error("Invalid BSP: early planar fans exceed the 16-million-index recovery limit");
            surface.firstIndex=LittleLong(int(bspDrawIndexes.size())); surface.numIndexes=LittleLong(int(added));
            for(int j=1;j+1<vertices;++j) for(int v:{0,j,j+1}) bspDrawIndexes.push_back(LittleLong(v));
        }
        bspDrawSurfaces.push_back(surface);
    }
}

void CompleteEarlyBSPRecovery() {
    if(!bspEarlyVersion) return;
    for(const auto& model:bspEarlyModels) {
        if(model.headNode>=0) range(model.headNode,1,bspNodes.size(),"model head node");
        else range(int(-1-int64_t(model.headNode)),1,bspLeafs.size(),"model head leaf");
    }
    if(bspEarlyVersion==45) return;
    // All graph edges, leaf spans and references have already been validated.
    // Global node ownership bounds work even for shared DAGs and many submodels.
    std::vector<int> surfaceOwner(bspDrawSurfaces.size()), brushOwner(bspBrushes.size());
    std::vector<int> nodeOwner(bspNodes.size()), leafSeen(bspLeafs.size()), stack;
    const auto claim=[](int& owner,int model) {
        if(owner && owner!=model) Error("Invalid BSP: early geometry is shared by distinct brush models");
        owner=model;
    };
    for(size_t m=1;m<bspModels.size();++m) {
        stack.push_back(bspEarlyModels[m].headNode);
        while(!stack.empty()) {
            const int node=stack.back(); stack.pop_back();
            if(node>=0) {
                if(nodeOwner[node]==int(m)) continue;
                claim(nodeOwner[node],int(m));
                for(int child:bspNodes[node].children) stack.push_back(child);
            } else {
                const size_t id=size_t(-1-int64_t(node));
                if(leafSeen[id]==int(m)) continue;
                leafSeen[id]=int(m); const auto& leaf=bspLeafs[id];
                for(int j=0;j<leaf.numBSPLeafSurfaces;++j) claim(surfaceOwner[bspLeafSurfaces[leaf.firstBSPLeafSurface+j]],int(m));
                for(int j=0;j<leaf.numBSPLeafBrushes;++j) claim(brushOwner[bspLeafBrushes[leaf.firstBSPLeafBrush+j]],int(m));
            }
        }
    }
    // Stable counting order is linear in geometry+models, rather than scanning
    // every surface/brush again for every model.
    const auto reorder=[&](auto& geometry,const auto& owner,auto first,auto count,std::vector<int>& sources) {
        for(int m:owner) ++(bspModels[m].*count);
        std::vector<int> next(bspModels.size()); int total=0;
        for(size_t m=0;m<bspModels.size();++m) { next[m]=total; bspModels[m].*first=total; total+=bspModels[m].*count; }
        auto ordered=geometry; std::vector<int> remap(geometry.size()); sources.resize(geometry.size());
        for(size_t i=0;i<geometry.size();++i) {
            const int dest=next[owner[i]]++; ordered[dest]=geometry[i]; remap[i]=dest; sources[dest]=int(i);
        }
        geometry=std::move(ordered); return remap;
    };
    const auto surfaces=reorder(bspDrawSurfaces,surfaceOwner,&bspModel_t::firstBSPSurface,&bspModel_t::numBSPSurfaces,bspEarlySurfaceSources);
    const auto brushes=reorder(bspBrushes,brushOwner,&bspModel_t::firstBSPBrush,&bspModel_t::numBSPBrushes,bspEarlyBrushSources);
    for(int& id:bspLeafSurfaces) id=surfaces[id];
    for(int& id:bspLeafBrushes) id=brushes[id];
    for(auto& fog:bspFogs) if(fog.brushNum>=0) fog.brushNum=brushes[fog.brushNum];
    const size_t repairedUVs=bspNormalizedUnusedLightmapPairs, repairedFogs=bspNormalizedUnusedFlareFogs;
    ValidateBSPData();
    bspNormalizedUnusedLightmapPairs+=repairedUVs; bspNormalizedUnusedFlareFogs+=repairedFogs;
    Sys_Printf("Early BSP recovery: derived %zu model ranges from native trees; visible brush materials require spatial inference\n",bspModels.size());
}

