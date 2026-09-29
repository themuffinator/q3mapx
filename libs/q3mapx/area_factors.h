// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "compute.h"
#include <array>
#include <memory>
#include <span>

namespace q3mapx {
// Shared CPU/OpenCL layout. No native float3: its OpenCL size/alignment is 16.
struct alignas(16) AreaFactorSample {
    std::array<float,4> origin; // w=0 means unmapped, w=1 means mapped
    std::array<float,4> normal;
};
struct alignas(16) AreaFactorLight {
    std::array<float,4> origin; // w=distance envelope
    std::array<float,4> plane; // normal and distance
    std::array<uint32_t,4> winding; // first vertex, count, reserved, reserved
};
using AreaFactorVertex = std::array<float,4>;

// Reuses a device context/program across batches. Calls must be serialized.
// Only the polygon integral is computed here; visibility and material tracing
// remain with the compiler. Result layout is light-major, then sample index.
class AreaFactorComputer {
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit AreaFactorComputer(std::unique_ptr<Impl>);
    friend std::unique_ptr<AreaFactorComputer> createAreaFactorComputer(int, ComputeReport&);
public:
    ~AreaFactorComputer();
    bool compute(std::span<const AreaFactorSample> samples, std::span<const AreaFactorLight> lights,
                 std::span<const AreaFactorVertex> vertices, std::vector<float>& output, ComputeReport& report);
};
std::unique_ptr<AreaFactorComputer> createAreaFactorComputer(int deviceIndex, ComputeReport& report);
}
