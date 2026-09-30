// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace q3mapx {
inline constexpr double cellVertexMergeDistance = 1e-7, cellMinimumArea = 1e-10;
inline constexpr double cellCoordinateLimit = 1e7;
inline constexpr double cellVolumeAbsoluteTolerance = 1e-6, cellVolumeRelativeTolerance = 1e-8;
using CellPoint = std::array<double,3>;
struct CellPlane { CellPoint normal; double distance; };
struct CellNode { CellPlane plane; std::array<int,2> children; };
struct CellLimits {
    uint64_t work = 50'000'000;
    size_t cells = 250'000, faces = 1'000'000, points = 4'000'000;
    size_t cellFaces = 1024, cellPoints = 8192, pendingPoints = 262144;
};
struct ReconstructedCell {
    int leaf = -1, cluster = -1;
    CellPoint center{}, mins{}, maxs{};
    double volume = 0;
    unsigned faces = 0;
};
struct CellInterface {
    size_t front = 0, back = 0;
    int node = -1;
    double area = 0;
    std::vector<CellPoint> points;
};
struct CellGraph {
    const char* status = "unavailable";
    CellPoint mins{}, maxs{};
    uint64_t workUsed = 0, enclosedOpenFaces = 0, degenerateFragments = 0;
    double enclosureVolume = 0, cellVolume = 0;
    std::vector<ReconstructedCell> cells;
    std::vector<CellInterface> interfaces; // At least one endpoint has cluster >= 0.
};
// Head/children use native BSP encoding: nodes >= 0, leaf i as -1-i.
// Validates reachable references and rejects shared internal nodes/cycles.
// Repeated leaf references produce distinct geometric path cells. No original
// PRT flags, author classifications, VIS correctness or source pairing is implied.
// Output coordinates are bounded to 1e7. Degeneracy counts and numerical limits
// must be considered even when the summed volume passes its consistency check.
CellGraph reconstructCellGraph(std::span<const CellNode> nodes, std::span<const int> clusters,
    int head, CellPoint mins, CellPoint maxs, const CellLimits& limits = {}, uint64_t used = 0);
}
