// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "math/vector.h"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace q3mapx {
struct PointFitOptions {
    bool enabled=false,explicitBounds=false,allowImplicitMaterials=false;
    Vector3 mins{0},maxs{0};
    float spacing=64,maxIntensity=1000,minImprovement=1,maxRMSE=2;
    unsigned maxCandidates=1024,maxLights=4,refinementSteps=6,style=0,blockSize=4;
    uint64_t maxWork=50'000'000;
};
struct PointFitReceiver {
    size_t sample;
    int surface,cluster,slot,page,x,y;
    Vector3 origin,normal,baseline;
    Vector3b observed;
    float brightness;
    bool withheld=false;
};
struct PointFitLight {
    Vector3 origin{0},energy{0}; // Native linear RGB intensity, before pointScale.
};
struct PointFitMetrics {
    uint64_t samples=0;
    double mae=0,rmse=0,maximum=0;
};
struct PointFitAlternative { PointFitLight light; double trainingRMSE; };
struct PointFitResult {
    const char* status="not_run";
    bool accepted=false;
    uint64_t work=0,gridPoints=0,usableCandidates=0,positionsTested=0;
    uint64_t unknownCandidates=0,encodingFailures=0;
    std::array<PointFitMetrics,2> baseline,trial;
    std::vector<PointFitLight> lights;
    std::vector<PointFitReceiver> receivers;
    std::vector<Vector3b> prediction;
    std::map<std::string,uint64_t> exclusions;
    std::vector<PointFitAlternative> trainingAlternatives;
    std::vector<unsigned char> subsampling;
};
PointFitResult fitPointLights(const PointFitOptions& options,std::vector<PointFitReceiver> receivers);
}
