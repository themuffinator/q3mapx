// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "bspfile_abstract.h"
#include "export_mesh.h"
#include <cmath>

namespace {
struct Patch {
    int control, controlWidth, controlHeight, vertex, index, width, height, steps;
};
struct Row { size_t patch; int y; };
std::vector<Patch> patches;
std::vector<Row> rows;

void exportRow(int rowIndex) {
    const auto& row=rows[rowIndex]; const auto& patch=patches[row.patch];
    const int y=row.y, segmentY=std::min(y/patch.steps,(patch.controlHeight-3)/2);
    const double v=double(y-segmentY*patch.steps)/patch.steps;
    const double by[3]={(1-v)*(1-v),2*v*(1-v),v*v}, dy[3]={-2*(1-v),2-4*v,2*v};
    for(int x=0;x<patch.width;++x) {
        const int segmentX=std::min(x/patch.steps,(patch.controlWidth-3)/2);
        const double u=double(x-segmentX*patch.steps)/patch.steps;
        const double bx[3]={(1-u)*(1-u),2*u*(1-u),u*u}, dx[3]={-2*(1-u),2-4*u,2*u};
        DoubleVector3 point(0), tangentU(0), tangentV(0), fallbackNormal(0);
        double st[2]{}, lightmap[MAX_LIGHTMAPS][2]{}, color[MAX_LIGHTMAPS][4]{};
        for(int j=0;j<3;++j) for(int i=0;i<3;++i) {
            const auto& control=bspDrawVerts[patch.control+(segmentY*2+j)*patch.controlWidth+segmentX*2+i];
            const double weight=bx[i]*by[j]; const DoubleVector3 xyz(control.xyz);
            point+=xyz*weight; tangentU+=xyz*(dx[i]*by[j]); tangentV+=xyz*(bx[i]*dy[j]);
            fallbackNormal+=DoubleVector3(control.normal)*weight;
            for(int a=0;a<2;++a) st[a]+=double(control.st[a])*weight;
            for(int slot=0;slot<MAX_LIGHTMAPS;++slot) {
                for(int a=0;a<2;++a) lightmap[slot][a]+=double(control.lightmap[slot][a])*weight;
                for(int a=0;a<4;++a) color[slot][a]+=double(control.color[slot][a])*weight;
            }
        }
        auto normal=vector3_cross(tangentU,tangentV);
        if(vector3_length_squared(normal)<1e-24) normal=fallbackNormal;
        if(vector3_length_squared(normal)<1e-24) normal=DoubleVector3(0,0,1);
        normal=vector3_normalised(normal);
        auto& vertex=bspDrawVerts[patch.vertex+y*patch.width+x];
        vertex.xyz=Vector3(point); vertex.normal=Vector3(normal); vertex.st=Vector2(st[0],st[1]);
        for(int slot=0;slot<MAX_LIGHTMAPS;++slot) {
            vertex.lightmap[slot]=Vector2(lightmap[slot][0],lightmap[slot][1]);
            for(int a=0;a<4;++a) vertex.color[slot][a]=byte(std::clamp(std::lround(color[slot][a]),0L,255L));
        }
        if(y+1<patch.height && x+1<patch.width) {
            // Compiler checkerboard diagonals and native clockwise winding.
            const int a=y*patch.width+x, corners[5]={a,a+patch.width,a+patch.width+1,a+1,a};
            const int r=(x+y)&1, index=patch.index+(y*(patch.width-1)+x)*6;
            const int triangle[6]={corners[r],corners[r+1],corners[r+2],corners[r],corners[r+2],corners[r+3]};
            std::copy_n(triangle,6,bspDrawIndexes.begin()+index);
        }
    }
}
}

void PrepareBSPMeshExport(int patchSteps) {
    if(patchSteps<1 || patchSteps>32) Error("Mesh patch steps must be between 1 and 32");
    ValidateBSPData();
    for(size_t i=0;i<entities.size();++i) if(i==0 || entities[i].valueForKey("model")[0]=='*') {
        const auto origin=entities[i].vectorForKey("origin");
        for(int j=0;j<3;++j) if(!std::isfinite(origin[j])) Error("Invalid BSP: non-finite mesh entity origin");
    }
    patches.clear(); rows.clear();
    uint64_t vertices=bspDrawVerts.size(), indices=bspDrawIndexes.size();
    // Preflight every expansion before allocation, dispatch or opening outputs.
    for(const auto& surface:bspDrawSurfaces) {
        if(lightmapsAsTexcoord && deluxemap && surface.lightmapNum[0]==INT_MAX)
            Error("Invalid BSP: deluxemap index overflows the supported range");
        if(surface.surfaceType!=MST_PATCH) continue;
        const int width=(surface.patchWidth-1)/2*patchSteps+1, height=(surface.patchHeight-1)/2*patchSteps+1;
        vertices+=uint64_t(width)*height; indices+=uint64_t(width-1)*(height-1)*6;
    }
    if(vertices>4*1024*1024 || indices>24*1024*1024)
        Error("Mesh export exceeds the 4-million-vertex or 24-million-index budget; lower -patchsteps");
    // Controls precede appended storage. Workers write disjoint rows into fixed
    // allocations, so scheduling cannot affect geometry or serialization order.
    size_t nextVertex=bspDrawVerts.size(), nextIndex=bspDrawIndexes.size();
    bspDrawVerts.resize(size_t(vertices)); bspDrawIndexes.resize(size_t(indices));
    for(auto& surface:bspDrawSurfaces) if(surface.surfaceType==MST_PATCH) {
        const int width=(surface.patchWidth-1)/2*patchSteps+1, height=(surface.patchHeight-1)/2*patchSteps+1;
        patches.push_back({surface.firstVert,surface.patchWidth,surface.patchHeight,int(nextVertex),int(nextIndex),width,height,patchSteps});
        for(int y=0;y<height;++y) rows.push_back({patches.size()-1,y});
        surface.surfaceType=MST_TRIANGLE_SOUP;
        surface.firstVert=int(nextVertex); surface.numVerts=width*height;
        surface.firstIndex=int(nextIndex); surface.numIndexes=(width-1)*(height-1)*6;
        nextVertex+=size_t(surface.numVerts); nextIndex+=size_t(surface.numIndexes);
    }
    if(!rows.empty()) {
        RunThreadsOnIndividual(int(rows.size()),false,exportRow,"MeshPatchTessellation",1);
        Sys_Printf("Mesh export tessellated %zu patches at %d steps per span\n",patches.size(),patchSteps);
    }
    patches.clear(); rows.clear();
    ValidateBSPData();
}
