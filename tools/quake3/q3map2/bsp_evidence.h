// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace q3mapx {
// Indices refer to the validated, normalized BSP arrays, not necessarily native
// disk order. No source classifications or optimization proposals are implied.
struct BrushEvidence {
    int model = -1; // -1 unowned, -2 overlapping model ranges
    uint64_t leafReferences = 0, nonopaqueReferences = 0, worldReferences = 0;
    std::vector<int> partitionSides, leafPathSides;
    bool axialEnclosureAvailable = false;
    std::array<double, 3> mins{}, maxs{};
};
struct RegionEvidence {
    int node = -1;
    unsigned depth = 0, deepestLeaf = 0;
    uint64_t splitNodes = 0, leafPaths = 0, nonopaqueLeafPaths = 0;
    uint64_t brushReferences = 0, surfaceReferences = 0;
    uint64_t indexedTriangleReferences = 0, patchReferences = 0;
};
struct VisibilityEvidence {
    bool present = false;
    int clusters = 0, rowBytes = 0;
    uint64_t referencedClusters = 0, visiblePairs = 0, missingSelfBits = 0;
    uint64_t minVisible = 0, maxVisible = 0;
};
struct CellWitness {
    bool available = false;
    int leaf = -1, cluster = -1;
    double clearance = 0;
    std::array<double, 3> point{};
};
struct BrushCellEvidence {
    const char* status = "not_world_geometry";
    uint64_t leafFragments = 0, uncertainFragments = 0;
    double brushVolume = 0, fragmentVolume = 0;
    CellWitness open, opaque;
    std::vector<int> interiorClusters;
    uint64_t testedPVSPairs = 0, invisiblePVSPairs = 0;
};
struct BSPEvidence {
    int worldHead = 0;
    bool hasWorldHead = false, uniqueNodePaths = true;
    uint64_t reachableNodes = 0, reachableLeaves = 0, workUsed = 0;
    std::vector<BrushEvidence> brushes;
    std::vector<RegionEvidence> regions;
    VisibilityEvidence visibility;
    bool brushCellsRequested = false;
    std::vector<BrushCellEvidence> brushCells;
};
// Requires LoadBSPFile + ParseEntities. Throws on budget exhaustion; no partial
// result is returned. A shared node graph disables path/subtree observations.
BSPEvidence analyzeBSPEvidence(unsigned regionDepth, uint64_t workLimit);
// Convex brush interiors clipped through the world tree. Does not depend on
// stored brush/leaf references or interpret source detail/material semantics.
void analyzeBSPBrushCells(BSPEvidence& evidence, uint64_t workLimit);
}
