// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <vector>

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
