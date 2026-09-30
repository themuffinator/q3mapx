// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace q3mapx {
// Position followed by 29 scalar interpolants. The native adapter can include
// normals, texture/lightmap coordinates and every light-style color channel.
// This core proves equality of those affine fields, not runtime shader behavior.
using ReductionVertex=std::array<float,32>;
using ReductionTriangle=std::array<uint32_t,3>;
struct ReductionLimits {
    uint64_t work=20'000'000;
    size_t vertices=65'536, triangles=131'072, historyFaces=524'288, ring=128;
};
struct ReductionEdit {
    uint32_t vertex;
    std::vector<uint32_t> removedFaces;
    // New faces have consecutive history IDs, following the input faces and
    // the added faces from preceding edits. This permits independent replay.
    std::vector<ReductionTriangle> addedFaces;
};
struct PlanarReduction {
    std::vector<ReductionTriangle> triangles;
    std::vector<ReductionEdit> edits;
    uint64_t workUsed=0, attemptedStars=0;
    uint64_t boundaryVertices=0, protectedVertices=0;
    uint64_t topologyRejected=0, planarRejected=0, attributeRejected=0, ringLimitRejected=0;
};
// Removes only interior vertices of simple, coplanar triangle fans whose scalar
// fields are exactly affine in binary32. Boundary edges/vertices stay intact;
// each accepted edit replaces n faces with n-2 faces without moving vertices.
// Inputs are immutable. Malformed data or a hard work/storage limit throws;
// callers must retain their original mesh rather than publish partial work.
// Shader deformation, dynamic lighting, fog, blending and material/profile
// equivalence require separate checks before applying this result to a BSP.
PlanarReduction reducePlanarMesh(std::span<const ReductionVertex> vertices,
    std::span<const ReductionTriangle> triangles,const ReductionLimits& limits={});
}
