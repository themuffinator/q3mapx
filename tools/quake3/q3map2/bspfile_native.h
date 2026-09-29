// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <vector>
#include <array>
#include <cstdint>
#include <string>

struct BSPRecoveryLoss {
    const char* feature;
    size_t bytes;
    const char* reason;
};
extern std::vector<BSPRecoveryLoss> bspRecoveryLosses;
extern std::vector<int> bspNativeShaderSubdivisions;
extern std::vector<float> bspNativeSurfaceSubdivisions;
void ResetBSPRecoveryMetadata();
void LoadFAKKBSPFile(const char* filename);

struct BSPNativeStaticModel {
    std::string model;
    std::array<float,3> origin, angles;
    float scale;
};
struct BSPNativeTerrain {
    int flags, x, y, baseHeight, shader, lightmap;
    int lightmapScale, lightmapS, lightmapT;
    std::array<float,8> corners;
    std::array<uint16_t,126> variance;
    std::array<uint8_t,81> heights;
};
extern std::vector<BSPNativeStaticModel> bspNativeStaticModels;
extern std::vector<BSPNativeTerrain> bspNativeTerrain;
extern std::vector<std::string> bspNativeFenceMasks;
extern std::vector<std::array<float,8>> bspNativeSideEquations;
extern std::vector<int> bspNativeSideEquationIndices;
extern size_t bspNativeTerrainTriangles;
extern size_t bspNormalizedUnusedNativeEquations;
void LoadMOHAABSPFile(const char* filename);
void CompleteBSPNativeRecovery();
