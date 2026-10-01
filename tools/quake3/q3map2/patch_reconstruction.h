// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "patch_source.h"
#include <map>

namespace q3mapx {
struct TrianglePatchDecision {
    int model;
    std::vector<int> surfaces;
    size_t omittedSurfaces = 0, samples = 0, triangles = 0;
    const char* status;
    int width = 0, height = 0, subdivisions = 0;
    double positionError = 0, uvError = 0;
};
struct TrianglePatchRecovery {
    std::vector<PatchSource> patches;
    std::vector<TrianglePatchDecision> decisions;
    std::map<std::string,size_t> counts;
    size_t omittedDecisions = 0;
    uint64_t work = 0;
};
TrianglePatchRecovery ReconstructTrianglePatches( const std::vector<bool>& restored,
    const std::vector<const char*>& excluded, int firstChannel, uint64_t workLimit );
}
