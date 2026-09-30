// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bsp_evidence.h"
#include "q3mapx/portal_graph.h"

namespace q3mapx {
struct PortalMeasure {
    bool planarConvex = false, bridge = false;
    const char* probe = "unavailable";
    std::array<double,3> center{};
    double area = 0, perimeter = 0, compactness = 0, width = 0;
};
struct PortalClusterEvidence {
    int region = -1, component = -1;
    uint64_t leaves = 0, degree = 0, worldSurfaces = 0, worldTriangles = 0;
    uint64_t visibleSurfaces = 0, visibleTriangles = 0, visiblePatches = 0;
};
struct PortalRegionEvidence {
    int node = -1; // Remainder, spanning clusters or unavailable world mapping.
    std::vector<int> clusters, associatedBrushSample;
    uint64_t passagePairs = 0, incident = 0, internal = 0, boundary = 0;
    uint64_t hints = 0, skies = 0, unknownFlags = 0, bridges = 0;
    uint64_t small = 0, slender = 0, parallelOpenings = 0, visibleInternalPairs = 0;
    uint64_t maxVisibleTriangles = 0, sumVisibleTriangles = 0;
};
struct PortalEvidence {
    bool worldMapping = false, pvsCosts = false;
    uint64_t unmappedClusters = 0, components = 0, bridges = 0;
    uint64_t passagePairs = 0, probeMatches = 0, probeDisagreements = 0, invalidGeometry = 0;
    std::vector<PortalMeasure> portals;
    std::vector<PortalClusterEvidence> clusters;
    std::vector<PortalRegionEvidence> regions; // BSP evidence frontier, then remainder.
    std::vector<int> rankedRegions;
};
// Observations only. Probe agreement is not proof of BSP/PRT correspondence;
// stored PVS is not an oracle for original structural/detail participation.
PortalEvidence analyzePortalEvidence(BSPEvidence& bsp, const PortalGraph& graph, uint64_t workLimit);
}
