// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace q3mapx {
struct PortalLimits {
    int clusters = 16384, portals = 65536, faces = 262144;
    int pointsPerWinding = 512, windingsPerCluster = 1024;
    size_t points = 8'000'000, bytes = 256 * 1024 * 1024;
};
struct PortalPolygon {
    int front = -1, back = -1, flags = 0; // Faces have no back cluster.
    std::vector<std::array<float, 3>> points;
};
struct PortalGraph {
    int clusters = 0;
    size_t pointCount = 0;
    std::vector<PortalPolygon> portals, faces;
};
// Syntax/count/index/finite-coordinate validation. Geometric convexity and
// correspondence with a particular BSP are separate analysis responsibilities.
PortalGraph parsePortalGraph(std::string_view text, const PortalLimits& limits = {});
PortalGraph readPortalGraph(const std::filesystem::path& path, const PortalLimits& limits = {});
}
