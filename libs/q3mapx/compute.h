// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "columns.h"
#include <string>

namespace q3mapx {
struct ComputeDevice {
    int index;
    std::string name, vendor, version;
    uint64_t memoryBytes;
    unsigned computeUnits;
    bool unifiedMemory;
};
struct ComputeReport {
    bool usedGPU = false;
    std::string device, reason;
    double setupSeconds = 0, transferSeconds = 0, kernelSeconds = 0, totalSeconds = 0;
};
struct ColumnImage {
    unsigned width, height, samples;
    float minX, minY, sizeX, sizeY, sizeZ;
    const float* offsets; // samples x float2, or nullptr for deterministic random samples
    uint32_t seed = 0;
};
std::vector<ComputeDevice> computeDevices( std::string& reason );
bool computeColumns( const ColumnScene& scene, const ColumnImage& image, int deviceIndex,
                     float* output, ComputeReport& report );
}
