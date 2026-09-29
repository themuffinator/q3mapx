// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/lightgrid.h"
#include <iostream>
#include <random>
#include <string>

struct Point {
    std::array<std::array<std::uint8_t, 3>, 4> ambient{}, directed{};
    std::array<std::uint8_t, 4> styles{};
    std::array<std::uint8_t, 2> latLong{};
    bool operator==(const Point&) const = default;
};

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

// Independent expression of the inherited ordered scan, deliberately without
// the optimized match helper, selected dimensions, buckets or hash table.
static q3mapx::PackedLightGrid<Point> reference(const std::vector<Point>& input) {
    q3mapx::PackedLightGrid<Point> result;
    for (const auto& point : input) {
        std::size_t i = 0;
        for (; i < result.points.size(); ++i) {
            const auto& other = result.points[i];
            if (point.styles != other.styles) continue;
            bool same = true;
            for (unsigned d = 0; d < 2; ++d) {
                const int difference = std::abs(point.latLong[d] - other.latLong[d]);
                same &= difference <= 4 || difference >= 251;
            }
            for (unsigned style = 0; style < 4; ++style)
                for (unsigned channel = 0; channel < 3; ++channel)
                    same &= std::abs(point.ambient[style][channel] - other.ambient[style][channel]) <= 4
                        && std::abs(point.directed[style][channel] - other.directed[style][channel]) <= 4;
            if (same) break;
        }
        if (i == result.points.size()) result.points.push_back(point);
        result.indices.push_back(std::uint16_t(i));
    }
    return result;
}

static void compare(const std::vector<Point>& points) {
    const auto expected = reference(points);
    const auto actual = q3mapx::packLightGrid<Point>(points);
    require(actual.points == expected.points, "Dictionary order/value differs from NRC");
    require(actual.indices == expected.indices, "First matching dictionary index differs from NRC");
    for (const auto id : actual.indices) require(id < actual.points.size(), "Invalid grid reference");
}

int main() {
    for (unsigned count : {0u, 1u, 255u, 256u, 257u, 10000u}) compare(std::vector<Point>(count));
    // Approximate equality is not transitive: 4 matches 0 and 8, but must take 0.
    for (unsigned channel = 0; channel < 24; ++channel) {
        std::vector<Point> points(400);
        auto& colors0 = channel < 12 ? points[0].ambient : points[0].directed;
        auto& colors1 = channel < 12 ? points[1].ambient : points[1].directed;
        colors0[(channel % 12) / 3][channel % 3] = 8;
        colors1[(channel % 12) / 3][channel % 3] = 0;
        for (unsigned i = 2; i < points.size(); ++i) {
            auto& colors = channel < 12 ? points[i].ambient : points[i].directed;
            colors[(channel % 12) / 3][channel % 3] = i % 17;
        }
        compare(points);
    }
    // Exhaust all direction values and pairs near both sides of the seam.
    for (unsigned axis = 0; axis < 2; ++axis)
        for (unsigned start : {0u, 1u, 4u, 5u, 7u, 8u, 127u, 247u, 250u, 251u, 254u, 255u}) {
            std::vector<Point> points(513);
            points[0].latLong[axis] = start;
            for (unsigned i = 1; i < points.size(); ++i) points[i].latLong[axis] = (i - 1) % 256;
            compare(points);
        }
    std::mt19937 random(0x51475249);
    for (unsigned mode = 0; mode < 5; ++mode) {
        std::vector<Point> points(3500);
        for (auto& point : points) {
            for (unsigned i = 0; i < 4; ++i) {
                point.styles[i] = mode == 0 ? random() % 4 : 0;
                for (unsigned c = 0; c < 3; ++c) {
                    point.ambient[i][c] = mode == 2 ? 0 : random() % 256;
                    point.directed[i][c] = mode == 3 ? 0 : random() % 256;
                }
            }
            point.latLong = {std::uint8_t(random()), std::uint8_t(random())};
            if (mode == 4) point.ambient = point.directed = {};
        }
        // Insert near matches and exact repeats with earlier and later neighbors.
        for (unsigned i = 300; i < points.size(); i += 3) {
            points[i] = points[i / 3];
            points[i].directed[3][2] ^= 3;
        }
        compare(points);
    }
    std::vector<Point> boundary(q3mapx::maxRavenGridPoints);
    for (unsigned i = 0; i < boundary.size(); ++i) {
        boundary[i].styles[0] = i & 255;
        boundary[i].styles[1] = i >> 8;
    }
    auto packed = q3mapx::packLightGrid<Point>(boundary);
    require(packed.points.size() == 65535 && packed.indices.back() == 65534, "Dictionary boundary failed");
    boundary.push_back(boundary.front());
    packed = q3mapx::packLightGrid<Point>(boundary);
    require(packed.indices.back() == 0, "A full dictionary must still accept matching points");
    boundary.back().styles = {255, 255, 0, 0};
    bool failed = false;
    try { (void)q3mapx::packLightGrid<Point>(boundary); }
    catch (const std::length_error&) { failed = true; }
    require(failed, "Dictionary overflow was not rejected");
    require(q3mapx::packLightGrid<Point>({}).indices.empty(), "Failure affected a later invocation");
    std::cout << "Raven grid first-match parity, circular seams, style channels and capacity passed\n";
}
