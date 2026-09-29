// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <vector>
#include <cstddef>
#include "q3mapx/area_factors.h"

struct rawLightmap_t;
struct trace_t;
class Args;
namespace q3mapx {
struct LightFactorCache {
    std::vector<float> values;
    std::vector<int> offsets;
    std::vector<AreaFactorSample> samples;
    const trace_t* trace = nullptr;
    int nextLight = 0;
    void prepare(int light);
    const float* factor(int light, size_t sample) const {
        return offsets.empty() || offsets[light] < 0 ? nullptr : &values[size_t(offsets[light])+sample];
    }
};
void parseLightingGpuOptions(Args& args);
void beginLightingGpu();
bool lightingGpuEnabled();
void prepareLightingGpuPass();
LightFactorCache cacheLightFactors(int rawLightmapNum, const rawLightmap_t& lm, const trace_t& trace);
void finishLightingGpu();
}
