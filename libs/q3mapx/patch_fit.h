// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace q3mapx {
struct PatchFitVertex {
    std::array<double, 5> value{}; // XYZ, absolute ST
    std::array<uint8_t, 4> color{255,255,255,255};
    auto operator<=>( const PatchFitVertex& ) const = default;
};
struct PatchFit {
    const char* status = "unsupported_grid";
    int width = 0, height = 0, subdivisions = 0;
    double positionError = 0, uvError = 0;
    std::vector<PatchFitVertex> controls;
};
// One welded connected component. Missing/ambiguous data is rejected, never
// filled. firstChannel=0/3/4 requests RGBA/alpha/no stored channels. Work is a
// shared bounded count of input visits and evaluated channel samples.
PatchFit fitTrianglePatch( std::span<const PatchFitVertex> vertices,
    std::span<const std::array<int,3>> triangles, int firstChannel,
    uint64_t& work, uint64_t workLimit );
}
