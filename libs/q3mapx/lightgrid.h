// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace q3mapx {
inline constexpr std::size_t maxRavenGridPoints = 65535;
inline constexpr std::size_t maxRavenGridArray = 0x100000;

template<class Point> struct PackedLightGrid {
    std::vector<Point> points;
    std::vector<std::uint16_t> indices;
};

namespace lightgrid_detail {
template<class Point> int component(const Point& point, unsigned axis) {
    if (axis < 12) return point.ambient[axis / 3][axis % 3];
    if (axis < 24) return point.directed[(axis - 12) / 3][axis % 3];
    return point.latLong[axis - 24];
}

template<class Point> unsigned bucket(const Point& point, unsigned axis) {
    const unsigned value = component(point, axis);
    // NRC treats 0 and 255 as the same direction, with a period of 255.
    return (axis >= 24 && value == 255 ? 0 : value) >> 3;
}

template<class Point> std::uint32_t styles(const Point& point) {
    std::uint32_t key = 0;
    for (unsigned i = 0; i < 4; ++i) key |= std::uint32_t(point.styles[i]) << (i * 8);
    return key;
}

template<class Point> bool matches(const Point& a, const Point& b) {
    if (a.styles != b.styles) return false;
    for (unsigned i = 0; i < 2; ++i) {
        const int delta = std::abs(int(a.latLong[i]) - int(b.latLong[i]));
        if (delta > 4 && delta < 251) return false;
    }
    for (unsigned i = 0; i < 24; ++i)
        if (std::abs(component(a, i) - component(b, i)) > 4) return false;
    return true;
}

// Select independent, discriminating dimensions using a bounded deterministic
// sample. Minimizing joint bucket collisions avoids choosing three identical
// channels on grayscale grids. This changes candidates, never match semantics.
template<class Point> std::array<unsigned, 3> selectAxes(std::span<const Point> input) {
    const std::size_t count = std::min(input.size(), std::size_t(4096));
    std::vector<std::array<unsigned char, 26>> samples(count);
    for (std::size_t i = 0; i < count; ++i)
        for (unsigned axis = 0; axis < 26; ++axis)
            samples[i][axis] = bucket(input[i * input.size() / count], axis);
    std::vector<unsigned> prefixes(count, 0);
    std::vector<unsigned> histogram(32768);
    std::array<unsigned, 3> axes{};
    for (unsigned dimension = 0; dimension < axes.size(); ++dimension) {
        std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
        for (unsigned axis = 0; axis < 26; ++axis) {
            if (std::find(axes.begin(), axes.begin() + dimension, axis) != axes.begin() + dimension) continue;
            std::fill_n(histogram.begin(), 1u << (5 * (dimension + 1)), 0);
            std::uint64_t collisions = 0;
            for (std::size_t i = 0; i < count; ++i) {
                auto& hits = histogram[(prefixes[i] << 5) | samples[i][axis]];
                collisions += 2 * hits++ + 1;
            }
            if (collisions < best) { best = collisions; axes[dimension] = axis; }
        }
        for (std::size_t i = 0; i < count; ++i)
            prefixes[i] = (prefixes[i] << 5) | samples[i][axes[dimension]];
    }
    return axes;
}

struct Buckets {
    std::array<unsigned, 3> values{};
    unsigned count = 0;
};
inline Buckets neighbors(int value, bool circular) {
    Buckets result;
    for (int delta = -4; delta <= 4; ++delta) {
        int candidate = value + delta;
        if (circular) candidate = (candidate + 255) % 255;
        else if (candidate < 0 || candidate > 255) continue;
        const unsigned cell = unsigned(candidate) >> 3;
        if (std::find(result.values.begin(), result.values.begin() + result.count, cell)
                == result.values.begin() + result.count)
            result.values[result.count++] = cell;
    }
    return result;
}
} // namespace lightgrid_detail

// Preserve NRC's insertion order and earliest approximate match exactly. A
// failed capacity check publishes no partial result and never emits an invalid
// 16-bit reference. The index has at most one entry per dictionary point.
template<class Point> PackedLightGrid<Point> packLightGrid(std::span<const Point> input,
    std::size_t capacity = maxRavenGridPoints) {
    using namespace lightgrid_detail;
    if (capacity == 0 || capacity > maxRavenGridPoints)
        throw std::invalid_argument("Invalid Raven lightgrid dictionary capacity");
    if (input.size() > maxRavenGridArray)
        throw std::length_error("Raven lightgrid exceeds 1048576 sample references; increase the map gridsize");
    PackedLightGrid<Point> result;
    result.points.reserve(std::min(input.size(), capacity));
    result.indices.reserve(input.size());
    const bool indexed = input.size() > 256;
    const auto axes = indexed ? selectAxes(input) : std::array<unsigned, 3>{};
    std::unordered_map<std::uint64_t, std::vector<std::uint16_t>> cells;
    if (indexed) cells.reserve(std::min(input.size(), capacity));

    for (const Point& point : input) {
        std::size_t first = result.points.size();
        const std::uint64_t prefix = std::uint64_t(styles(point)) << 15;
        if (indexed) {
            const auto a = neighbors(component(point, axes[0]), axes[0] >= 24);
            const auto b = neighbors(component(point, axes[1]), axes[1] >= 24);
            const auto c = neighbors(component(point, axes[2]), axes[2] >= 24);
            for (unsigned i = 0; i < a.count; ++i)
                for (unsigned j = 0; j < b.count; ++j)
                    for (unsigned k = 0; k < c.count; ++k) {
                        const auto cell = cells.find(prefix | (a.values[i] << 10) | (b.values[j] << 5) | c.values[k]);
                        if (cell == cells.end()) continue;
                        for (const auto id : cell->second) {
                            if (id >= first) break; // Each list is in insertion order.
                            if (matches(point, result.points[id])) { first = id; break; }
                        }
                    }
        }
        else {
            for (std::size_t i = 0; i < first; ++i)
                if (matches(point, result.points[i])) { first = i; break; }
        }
        if (first == result.points.size()) {
            if (first == capacity)
                throw std::length_error("Raven lightgrid needs more than 65535 distinct samples; increase the map gridsize");
            result.points.push_back(point);
            if (indexed)
                cells[prefix | (bucket(point, axes[0]) << 10) | (bucket(point, axes[1]) << 5)
                    | bucket(point, axes[2])].push_back(std::uint16_t(first));
        }
        result.indices.push_back(std::uint16_t(first));
    }
    return result;
}
} // namespace q3mapx
