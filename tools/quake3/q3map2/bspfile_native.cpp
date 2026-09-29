// SPDX-License-Identifier: GPL-3.0-or-later
// Independent native readers based on format observations, not copied translators.
// See docs/GAME-COVERAGE.md for the pinned references and recovery boundaries.
#include "bspfile_abstract.h"
#include "bspfile_ibsp.h"
#include "bspfile_native.h"
#include "bspfile_early.h"
#include "bsp_formats.h"
#include <cmath>
#include "q3mapx/terrain.h"

std::vector<BSPRecoveryLoss> bspRecoveryLosses;
std::vector<int> bspNativeShaderSubdivisions;
std::vector<float> bspNativeSurfaceSubdivisions;
std::vector<BSPNativeStaticModel> bspNativeStaticModels;
std::vector<BSPNativeTerrain> bspNativeTerrain;
std::vector<std::string> bspNativeFenceMasks;
std::vector<std::array<float,8>> bspNativeSideEquations;
std::vector<int> bspNativeSideEquationIndices;
size_t bspNativeTerrainTriangles=0;
size_t bspNormalizedUnusedNativeEquations=0;
static std::vector<std::array<int,2>> leafTerrainRanges;
static std::vector<unsigned> terrainReferences;

void ResetBSPRecoveryMetadata() {
    ResetEarlyBSPRecovery();
    bspRecoveryLosses.clear();
    bspNativeShaderSubdivisions.clear();
    bspNativeSurfaceSubdivisions.clear();
    bspNativeStaticModels.clear(); bspNativeTerrain.clear(); bspNativeFenceMasks.clear();
    bspNativeSideEquations.clear(); bspNativeSideEquationIndices.clear();
    leafTerrainRanges.clear(); terrainReferences.clear(); bspNativeTerrainTriangles=0;
    bspNormalizedUnusedNativeEquations=0;
}

namespace {
float nativeFloat(const byte* data) {
    float value; std::memcpy(&value,data,4); value=LittleFloat(value);
    if(!std::isfinite(value)) Error("Invalid BSP: non-finite native extension value");
    return value;
}
unsigned nativeShort(const byte* data) { return unsigned(data[0]) | unsigned(data[1])<<8; }
std::string nativeString(const byte* data,size_t capacity) {
    const auto* end=static_cast<const byte*>(std::memchr(data,0,capacity));
    if(!end) Error("Invalid BSP: unterminated native extension string");
    return std::string(reinterpret_cast<const char*>(data),size_t(end-data));
}
void nativeRange(int first,int count,size_t capacity,const char* label) {
    if(first<0 || count<0 || size_t(first)>capacity || size_t(count)>capacity-size_t(first))
        Error("Invalid BSP: native %s range is out of bounds",label);
}
}

void LoadFAKKBSPFile(const char* filename) {
    const MemBuffer file=LoadFile(filename);
    const auto header=ReadBSPHeader(file,20,12);
    if (std::memcmp(header.ident,"FAKK",4) || header.version != g_game->bspVersion)
        Error("Invalid BSP: %s requires FAKK version %d (use -inspect to identify this file)",g_game->arg,g_game->bspVersion);
    const auto& formats=q3mapx::bspFormats();
    const auto format=std::find_if(formats.begin(),formats.end(),[&](const auto& f) {
        return strEqual(f.ident,"FAKK") && f.version==header.version;
    });
    if(format==formats.end()) Error("Unsupported FAKK version");
    const auto directory=q3mapx::inspectBSPDirectory({static_cast<const uint8_t*>(file.data()),file.size()},file.size(),*format);
    if(!directory.errors.empty()) Error("Invalid BSP: %s",directory.errors.front().c_str());

    // Canonical IBSP slots reference the original payload; only differing record
    // prefixes are converted. No temporary BSP or duplicated whole-file image.
    constexpr int nativeSlots[17]={14,0,1,9,8,7,6,13,11,10,4,5,12,3,2,16,15};
    bspHeader_t canonical{};
    for(size_t i=0;i<std::size(nativeSlots);++i) canonical.lumps[i]=header.lumps[nativeSlots[i]];
    LoadIBSPGeometry(canonical,file,76,108);
    bspAds.clear();
    const auto* bytes=static_cast<const byte*>(file.data());
    for(size_t i=0;i<bspShaders.size();++i)
        bspNativeShaderSubdivisions.push_back(q3mapx::bspLittleInt(bytes+header.lumps[0].offset+i*76+72));
    for(size_t i=0;i<bspDrawSurfaces.size();++i) {
        float value;
        std::memcpy(&value,bytes+header.lumps[3].offset+i*108+104,4);
        value=LittleFloat(value);
        if(!std::isfinite(value)) Error("Invalid BSP: non-finite native surface subdivision %zu",i);
        bspNativeSurfaceSubdivisions.push_back(value);
    }
    for(const int lump : {17,18,19}) if(header.lumps[lump].length) {
        bspRecoveryLosses.push_back({format->lumps[lump].name,size_t(header.lumps[lump].length),
            "Native baked-light extension is not reconstructed as source lighting."});
        Sys_Warning("Recovery omits native %s (%d bytes); MAP report records the loss\n",
                    format->lumps[lump].name,header.lumps[lump].length);
    }
    Sys_Printf("Native %s recovery: shader flags and subdivision metadata retained in MAP recovery JSON; native writing is disabled\n",g_game->arg);
}

void LoadMOHAABSPFile(const char* filename) {
    const MemBuffer file=LoadFile(filename);
    const auto header=ReadBSPHeader(file,28,12);
    if(std::memcmp(header.ident,"2015",4) || header.version!=19)
        Error("Invalid BSP: mohaa requires 2015 version 19 (use -inspect to identify this file)");
    const auto& formats=q3mapx::bspFormats();
    const auto& format=*std::find_if(formats.begin(),formats.end(),[](const auto& f){return strEqual(f.id,"mohaa19");});
    const auto directory=q3mapx::inspectBSPDirectory({static_cast<const uint8_t*>(file.data()),file.size()},file.size(),format);
    if(!directory.errors.empty()) Error("Invalid BSP: %s",directory.errors.front().c_str());
    // Native MOHAA has no fog lump and uses three packed lightgrid extensions.
    constexpr int nativeSlots[17]={14,0,1,9,8,7,6,13,12,11,4,5,-1,3,2,-1,15};
    bspHeader_t canonical{};
    for(size_t i=0;i<std::size(nativeSlots);++i) if(nativeSlots[i]>=0) canonical.lumps[i]=header.lumps[nativeSlots[i]];
    LoadIBSPGeometry(canonical,file,140,108,64,12);
    bspAds.clear();
    const auto* bytes=static_cast<const byte*>(file.data());
    const auto record=[&](int lump,size_t index,size_t stride) { return bytes+header.lumps[lump].offset+index*stride; };
    const auto integer=[](const byte* data){return q3mapx::bspLittleInt(data);};
    for(size_t i=0;i<bspShaders.size();++i) {
        const auto* p=record(0,i,140);
        bspNativeShaderSubdivisions.push_back(integer(p+72));
        bspNativeFenceMasks.push_back(nativeString(p+76,64));
    }
    for(size_t i=0;i<bspDrawSurfaces.size();++i)
        bspNativeSurfaceSubdivisions.push_back(nativeFloat(record(3,i,108)+104));
    const size_t equations=size_t(header.lumps[10].length)/32;
    for(size_t i=0;i<equations;++i) {
        std::array<float,8> values;
        for(size_t j=0;j<8;++j) values[j]=nativeFloat(record(10,i,32)+j*4);
        bspNativeSideEquations.push_back(values);
    }
    const bool hasFence=std::any_of(bspShaders.begin(),bspShaders.end(),[](const auto& shader) {
        return (LittleLong(shader.contentFlags)&0x2000)!=0;
    });
    for(size_t i=0;i<bspBrushSides.size();++i) {
        int equation=integer(record(11,i,12)+8);
        // Retail briefing/credits/void maps use zero despite no equations and
        // no fence contents. No point-trace fence equation can be consumed.
        if(equation==0 && equations==0 && !hasFence) { equation=-1; ++bspNormalizedUnusedNativeEquations; }
        if(equation!=-1) nativeRange(equation,1,equations,"side equation");
        bspNativeSideEquationIndices.push_back(equation);
    }
    if(bspNormalizedUnusedNativeEquations)
        Sys_Warning("Normalized %zu unused zero side-equation references with no fence contents or equation table\n",bspNormalizedUnusedNativeEquations);
    const size_t terrainCount=size_t(header.lumps[22].length)/388;
    if(terrainCount>16384) Error("Invalid BSP: native terrain exceeds the 16384-patch recovery limit");
    for(size_t i=0;i<terrainCount;++i) {
        const auto* p=record(22,i,388);
        BSPNativeTerrain t{};
        t.flags=p[0]; t.lightmapScale=p[1]; t.lightmapS=p[2]; t.lightmapT=p[3];
        for(size_t j=0;j<8;++j) t.corners[j]=nativeFloat(p+4+j*4);
        t.x=int(std::bit_cast<int8_t>(p[36])); t.y=int(std::bit_cast<int8_t>(p[37]));
        t.baseHeight=int(std::bit_cast<int16_t>(uint16_t(nativeShort(p+38))));
        t.shader=int(nativeShort(p+40)); t.lightmap=int(nativeShort(p+42));
        nativeRange(t.shader,1,bspShaders.size(),"terrain shader");
        if(t.lightmap!=65535) nativeRange(t.lightmap,1,bspLightBytes.size()/(128*128*3),"terrain lightmap");
        for(size_t j=0;j<4;++j) {
            const int neighbor=int(std::bit_cast<int16_t>(uint16_t(nativeShort(p+44+j*2))));
            if(neighbor!=-1) nativeRange(neighbor,1,terrainCount,"terrain neighbor");
        }
        for(size_t j=0;j<126;++j) t.variance[j]=uint16_t(nativeShort(p+52+j*2));
        std::copy_n(p+304,81,t.heights.begin());
        bspNativeTerrain.push_back(t);
    }
    for(size_t i=0;i<size_t(header.lumps[23].length)/2;++i) {
        const auto id=nativeShort(record(23,i,2));
        nativeRange(int(id),1,terrainCount,"terrain index"); terrainReferences.push_back(id);
    }
    const size_t modelCount=size_t(header.lumps[25].length)/164;
    for(size_t i=0;i<modelCount;++i) {
        const auto* p=record(25,i,164);
        BSPNativeStaticModel model;
        model.model=nativeString(p,128);
        for(size_t j=0;j<3;++j) { model.origin[j]=nativeFloat(p+128+j*4); model.angles[j]=nativeFloat(p+140+j*4); }
        model.scale=nativeFloat(p+152);
        const int first=integer(p+156), count=integer(p+160);
        if(first<0 || count<0 || uint64_t(first)>uint64_t(header.lumps[24].length)
            || uint64_t(count)*3>uint64_t(header.lumps[24].length)-uint64_t(first))
            Error("Invalid BSP: native static model vertex colors are out of bounds");
        bspNativeStaticModels.push_back(std::move(model));
    }
    const size_t modelIndices=size_t(header.lumps[26].length)/2;
    for(size_t i=0;i<modelIndices;++i) nativeRange(int(nativeShort(record(26,i,2))),1,modelCount,"static model index");
    for(size_t i=0;i<bspLeafs.size();++i) {
        const auto* p=record(8,i,64);
        const int first=integer(p+48), count=integer(p+52);
        nativeRange(first,count,terrainReferences.size(),"leaf terrain");
        nativeRange(integer(p+56),integer(p+60),modelIndices,"leaf static models");
        leafTerrainRanges.push_back({first,count});
    }
    for(const int lump:{16,17,18,19,20,21,24}) if(header.lumps[lump].length) {
        bspRecoveryLosses.push_back({format.lumps[lump].name,size_t(header.lumps[lump].length),
            "Native baked-light data is not reconstructed as source lighting."});
        Sys_Warning("Recovery omits native %s (%d bytes)\n",format.lumps[lump].name,header.lumps[lump].length);
    }
    Sys_Printf("Native MOHAA recovery: %zu terrain patches, %zu static-model placements, %zu fence equations\n",
               terrainCount,modelCount,equations);
}

void CompleteBSPNativeRecovery() {
    CompleteEarlyBSPRecovery();
    if(bspNativeTerrain.empty()) return;
    uint64_t referenceCount=0;
    for(size_t i=0;i<bspLeafs.size();++i)
        referenceCount+=uint64_t(bspLeafs[i].numBSPLeafSurfaces)+uint64_t(leafTerrainRanges[i][1]);
    if(referenceCount>16*1024*1024) Error("Invalid BSP: expanded native leaf references exceed the 16-million-entry recovery limit");
    // Called only after validation and endian conversion of all base geometry.
    auto& world=bspModels[0];
    const int insertion=world.firstBSPSurface+world.numBSPSurfaces;
    const int added=int(bspNativeTerrain.size());
    for(size_t i=1;i<bspModels.size();++i) {
        auto& model=bspModels[i];
        if(model.firstBSPSurface<insertion && model.firstBSPSurface+model.numBSPSurfaces>insertion)
            Error("Invalid BSP: submodel surface range straddles the native terrain insertion");
        if(model.firstBSPSurface>=insertion) model.firstBSPSurface+=added;
    }
    std::vector<bspDrawSurface_t> surfaces;
    for(const auto& terrain:bspNativeTerrain) {
        const auto triangles=q3mapx::mohaaTerrainTriangles(terrain.variance,(terrain.flags&128)!=0);
        bspDrawSurface_t surface{};
        surface.shaderNum=terrain.shader; surface.fogNum=-1; surface.surfaceType=MST_TRIANGLE_SOUP;
        surface.firstVert=int(bspDrawVerts.size()); surface.numVerts=81;
        surface.firstIndex=int(bspDrawIndexes.size()); surface.numIndexes=int(triangles.size());
        surface.lightmapStyles={LS_NORMAL,LS_NONE,LS_NONE,LS_NONE};
        surface.vertexStyles={LS_NORMAL,LS_NONE,LS_NONE,LS_NONE};
        surface.lightmapNum={terrain.lightmap==65535 ? -1 : terrain.lightmap,-1,-1,-1};
        for(int y=0;y<9;++y) for(int x=0;x<9;++x) {
            auto vertex=c_bspDrawVert_t0;
            const auto height=[&](int u,int v){return terrain.baseHeight+2*terrain.heights[std::clamp(v,0,8)*9+std::clamp(u,0,8)];};
            vertex.xyz=Vector3(terrain.x*64+x*64,terrain.y*64+y*64,height(x,y));
            const double u=x/8.0,v=y/8.0;
            for(int axis=0;axis<2;++axis)
                vertex.st[axis]=float((1-u)*(1-v)*terrain.corners[axis]+(1-u)*v*terrain.corners[2+axis]
                    +u*(1-v)*terrain.corners[4+axis]+u*v*terrain.corners[6+axis]);
            vertex.normal=vector3_normalised(Vector3(height(x-1,y)-height(x+1,y),height(x,y-1)-height(x,y+1),128));
            vertex.lightmap[0]=Vector2((terrain.lightmapS+0.5+u*8*std::max(1,terrain.lightmapScale))/128.0,
                                     (terrain.lightmapT+0.5+v*8*std::max(1,terrain.lightmapScale))/128.0);
            vertex.color[0]=Color4b(255); bspDrawVerts.push_back(vertex);
        }
        bspDrawIndexes.insert(bspDrawIndexes.end(),triangles.begin(),triangles.end());
        bspNativeTerrainTriangles+=triangles.size()/3; surfaces.push_back(surface);
    }
    bspDrawSurfaces.insert(bspDrawSurfaces.begin()+insertion,surfaces.begin(),surfaces.end());
    world.numBSPSurfaces+=added;
    std::vector<int> references;
    references.reserve(size_t(referenceCount));
    for(size_t i=0;i<bspLeafs.size();++i) {
        auto& leaf=bspLeafs[i];
        const int first=int(references.size());
        for(int j=0;j<leaf.numBSPLeafSurfaces;++j) {
            const int old=bspLeafSurfaces[leaf.firstBSPLeafSurface+j];
            references.push_back(old>=insertion ? old+added : old);
        }
        for(int j=0;j<leafTerrainRanges[i][1];++j)
            references.push_back(insertion+int(terrainReferences[leafTerrainRanges[i][0]+j]));
        leaf.firstBSPLeafSurface=first; leaf.numBSPLeafSurfaces=int(references.size())-first;
    }
    bspLeafSurfaces=std::move(references);
    const size_t repairedUVs=bspNormalizedUnusedLightmapPairs, repairedFogs=bspNormalizedUnusedFlareFogs;
    ValidateBSPData();
    bspNormalizedUnusedLightmapPairs+=repairedUVs; bspNormalizedUnusedFlareFogs+=repairedFogs;
    Sys_Printf("Recovered %zu terrain triangles; %zu removed by native hole flags. OBJ/ASE export includes the terrain.\n",
        bspNativeTerrainTriangles,bspNativeTerrain.size()*128-bspNativeTerrainTriangles);
}
