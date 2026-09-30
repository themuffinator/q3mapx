// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bsp_evidence.h"
#include <array>
#include <vector>

namespace q3mapx {
struct LightingEvidenceOptions {
    unsigned stride = 1;
    uint64_t maxObservations = 50'000;
};
struct LightmapObservation {
    int x = 0, y = 0, firstTriangle = 0;
    uint64_t triangleHits = 0;
    bool ambiguous = false, boundary = false;
    std::array<double, 3> position{}, normal{};
};
struct LightmapSlotEvidence {
    const char* status = "unused_style";
    uint64_t degenerateUVTriangles = 0, degenerateGeometryTriangles = 0;
    uint64_t candidateTexels = 0;
    std::vector<LightmapObservation> observations;
};
struct LightingSurfaceEvidence {
    int model = -1; // -1 unowned, -2 overlapping model ranges
    uint64_t vertexObservations = 0;
    std::array<LightmapSlotEvidence, 4> slots;
};
struct LightingGridEvidence {
    const char* status = "absent";
    bool positionAvailable = false, storedPitch = false;
    std::array<double, 3> pitch{64, 64, 128}, origin{};
    std::array<uint64_t, 3> dimensions{};
    uint64_t observations = 0;
};
struct LightingEvidence {
    const char* status = "unsupported_native_adapter";
    const char* atlasStatus = "absent";
    LightingEvidenceOptions options;
    int pageSize = 0;
    uint64_t pages = 0, referencedPages = 0, observations = 0;
    std::vector<LightingSurfaceEvidence> surfaces;
    LightingGridEvidence grid;
};
// Requires validated native IBSP/RBSP data and parsed entities. Encoded bytes
// remain encoded; no original lights, shader behavior or bake settings inferred.
LightingEvidence analyzeBSPLighting(BSPEvidence& evidence, uint64_t workLimit,
                                   LightingEvidenceOptions options);
}
