// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "patch_reconstruction.h"
#include "q3mapx/patch_fit.h"
#include "authoring/patch_paint.h"
#include <numeric>

namespace q3mapx {
TrianglePatchRecovery ReconstructTrianglePatches( const std::vector<bool>& restored,
    const std::vector<const char*>& excluded, int firstChannel, uint64_t workLimit ) {
    TrianglePatchRecovery result;
    size_t recoveredControls=0;
    const auto record = [&]( TrianglePatchDecision decision ) {
        ++result.counts[decision.status];
        if ( result.decisions.size()==10'000 ) { ++result.omittedDecisions; return; }
        if ( decision.surfaces.size()>64 ) { decision.omittedSurfaces=decision.surfaces.size()-64; decision.surfaces.resize(64); }
        result.decisions.push_back(std::move(decision));
    };
    const auto spend = [&]( size_t n ) {
        if ( n>workLimit-std::min(result.work,workLimit) ) { result.work=workLimit; return false; }
        result.work+=n; return true;
    };
    for ( size_t model=0; model<bspModels.size(); ++model ) {
        const auto& m=bspModels[model];
        std::map<std::array<int,10>,std::vector<int>> groups;
        for ( int s=m.firstBSPSurface; s<m.firstBSPSurface+m.numBSPSurfaces; ++s ) {
            const auto& ds=bspDrawSurfaces[s];
            if ( (!restored.empty() && restored[s]) || (ds.surfaceType!=MST_PLANAR && ds.surfaceType!=MST_TRIANGLE_SOUP) || !ds.numIndexes ) continue;
            if ( ds.numIndexes%3 ) { record({int(model),{s},0,size_t(ds.numVerts),size_t(ds.numIndexes/3),"incomplete_triangle"}); continue; }
            if ( excluded[s] || !spend(1) ) { record({int(model),{s},0,size_t(ds.numVerts),size_t(ds.numIndexes/3),excluded[s]?excluded[s]:"work_limit"}); continue; }
            std::array<int,10> key{ds.shaderNum,ds.fogNum};
            for ( int i=0; i<4; ++i ) { key[2+i]=ds.vertexStyles[i]; key[6+i]=ds.lightmapStyles[i]; }
            groups[key].push_back(s);
        }
        for ( const auto& [key,surfaces]:groups ) {
            size_t count=0,indexCount=0;
            for ( int s:surfaces ) { count+=bspDrawSurfaces[s].numVerts; indexCount+=bspDrawSurfaces[s].numIndexes; }
            if ( count>262144 || indexCount>1572864 || !spend(count+indexCount) ) {
                record({int(model),surfaces,0,count,indexCount/3,result.work==workLimit?"work_limit":"group_sample_limit"}); continue;
            }
            std::map<PatchFitVertex,int> welded;
            std::vector<PatchFitVertex> vertices;
            struct Triangle { std::array<int,3> verts; int surface; };
            std::vector<Triangle> triangles;
            for ( int s:surfaces ) {
                const auto& ds=bspDrawSurfaces[s];
                std::vector<int> local;
                for ( int v=0; v<ds.numVerts; ++v ) {
                    const auto& in=bspDrawVerts[ds.firstVert+v]; PatchFitVertex value;
                    for ( int a=0; a<3; ++a ) value.value[a]=in.xyz[a];
                    for ( int a=0; a<2; ++a ) value.value[3+a]=in.st[a];
                    for ( int c=firstChannel; c<4; ++c ) value.color[c]=in.color[0][c];
                    const auto [it,inserted]=welded.emplace(value,int(vertices.size()));
                    if ( inserted ) vertices.push_back(value);
                    local.push_back(it->second);
                }
                for ( int i=0; i<ds.numIndexes; i+=3 ) {
                    std::array<int,3> ids;
                    for ( int j=0; j<3; ++j ) ids[j]=local[bspDrawIndexes[ds.firstIndex+i+j]];
                    triangles.push_back({ids,s});
                }
            }
            std::vector<int> parent(vertices.size()),size(vertices.size(),1); std::iota(parent.begin(),parent.end(),0);
            const auto root=[&](int v) { while(parent[v]!=v) { parent[v]=parent[parent[v]]; v=parent[v]; } return v; };
            for ( const auto& tri:triangles ) for ( int j=1; j<3; ++j ) {
                int a=root(tri.verts[0]),b=root(tri.verts[j]);
                if ( a==b ) continue;
                if ( size[a]<size[b] ) std::swap(a,b);
                parent[b]=a; size[a]+=size[b];
            }
            std::map<int,std::vector<int>> components;
            for ( size_t i=0; i<triangles.size(); ++i ) components[root(triangles[i].verts[0])].push_back(i);
            for ( const auto& [id,component]:components ) {
                std::map<int,int> local;
                std::vector<PatchFitVertex> points;
                std::vector<std::array<int,3>> faces;
                std::vector<int> support;
                for ( int t:component ) {
                    std::array<int,3> face;
                    for ( int j=0; j<3; ++j ) {
                        const int v=triangles[t].verts[j]; const auto [it,inserted]=local.emplace(v,points.size());
                        if ( inserted ) points.push_back(vertices[v]);
                        face[j]=it->second;
                    }
                    faces.push_back(face); support.push_back(triangles[t].surface);
                }
                std::sort(support.begin(),support.end()); support.erase(std::unique(support.begin(),support.end()),support.end());
                auto fit=fitTrianglePatch(points,faces,firstChannel,result.work,workLimit);
                if ( !fit.controls.empty() ) {
                    if ( result.patches.size()>=10'000 || fit.controls.size()>1'000'000-recoveredControls ) {
                        fit.controls.clear(); fit.status="recovered_control_limit";
                    }
                    else {
                        recoveredControls+=fit.controls.size();
                        auto& patch=result.patches.emplace_back();
                        patch.model=model; patch.entity=patch.primitive=-1;
                        patch.width=fit.width; patch.height=fit.height; patch.subdivisions=fit.subdivisions; patch.sampleSize=0;
                        patch.mode=firstChannel==0?authoring::materialPaint:authoring::alphaPaint;
                        patch.shader=bspShaders[key[0]].shader; patch.surfaces=support;
                        for ( const auto& v:fit.controls ) {
                            auto& control=patch.controls.emplace_back(c_bspDrawVert_t0);
                            for ( int a=0; a<3; ++a ) control.xyz[a]=v.value[a];
                            for ( int a=0; a<2; ++a ) control.st[a]=v.value[a+3];
                            Color4b color; for ( int c=0; c<4; ++c ) color[c]=v.color[c]; control.color.fill(color);
                        }
                    }
                }
                record({int(model),std::move(support),0,points.size(),faces.size(),fit.status,fit.width,fit.height,fit.subdivisions,fit.positionError,fit.uvError});
            }
        }
    }
    return result;
}
}
