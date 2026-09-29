// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace q3mapx {
struct alignas(16) ColumnPlane { float x, y, z, distance; };
struct alignas(16) ColumnBrush {
    float minX, minY, maxX, maxY;
    uint32_t first, count, pad0 = 0, pad1 = 0;
};
struct ColumnScene {
    std::vector<ColumnPlane> planes;
    std::vector<ColumnBrush> brushes;
    std::vector<std::array<uint32_t,2>> cells;
    std::vector<uint32_t> references;
    unsigned grid = 1;
    float minX = 0, minY = 0, scaleX = 1, scaleY = 1;
    void buildIndex( float x0, float y0, float x1, float y1 );
    float sample( float x, float y, bool indexed = true ) const;
};
// Per-pixel deterministic random state: independent of worker scheduling.
inline float columnRandom( uint32_t& state ){
    state += 0x9e3779b9u;
    uint32_t value = state;
    value = (value ^ (value >> 16)) * 0x85ebca6bu;
    value = (value ^ (value >> 13)) * 0xc2b2ae35u;
    value ^= value >> 16;
    return float(value >> 8) * (1.0f / 16777216.0f);
}
}
