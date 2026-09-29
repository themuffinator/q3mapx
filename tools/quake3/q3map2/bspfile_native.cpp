// SPDX-License-Identifier: GPL-3.0-or-later
// Independent native readers based on format observations, not copied translators.
// See docs/GAME-COVERAGE.md for the pinned references and recovery boundaries.
#include "bspfile_abstract.h"
#include "bspfile_ibsp.h"
#include "bspfile_native.h"
#include "bsp_formats.h"
#include <cmath>

std::vector<BSPRecoveryLoss> bspRecoveryLosses;
std::vector<int> bspNativeShaderSubdivisions;
std::vector<float> bspNativeSurfaceSubdivisions;

void ResetBSPRecoveryMetadata() {
    bspRecoveryLosses.clear();
    bspNativeShaderSubdivisions.clear();
    bspNativeSurfaceSubdivisions.clear();
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
