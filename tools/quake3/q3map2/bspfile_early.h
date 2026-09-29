// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <vector>

struct BSPEarlyModel {
    std::array<float,3> origin;
    int headNode, declaredFirstSurface, declaredSurfaceCount;
};
extern int bspEarlyVersion;
extern std::vector<BSPEarlyModel> bspEarlyModels;
extern std::vector<int> bspEarlyBrushContents, bspEarlySideFlags;
extern std::vector<int> bspEarlySurfaceSources, bspEarlyBrushSources;
void ResetEarlyBSPRecovery();
void LoadEarlyBSPFile(const char* filename);
void CompleteEarlyBSPRecovery();

