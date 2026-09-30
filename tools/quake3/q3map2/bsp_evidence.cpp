// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "bsp_evidence.h"
#include "bspfile_early.h"
#include <bit>
#include <map>
#include <stdexcept>

namespace q3mapx {
namespace {
struct Budget {
    uint64_t limit, used = 0;
    void spend(uint64_t amount) {
        if (amount > limit - used)
            throw std::runtime_error("BSP evidence work budget exceeded; no report was published");
        used += amount;
    }
};
size_t leafIndex(int child) { return size_t(-1 - int64_t(child)); }

std::array<float, 4> planeKey(const bspPlane_t& plane) {
    // Exact coefficients, accepting reversed orientation and signed zero. Do
    // not assume disk plane pairs are correct, nor merge nearby distinct cuts.
    float sign = 1;
    for (size_t axis = 0; axis < 3; ++axis) {
        const float n = plane.normal()[axis];
        if (n != 0) { sign = n < 0 ? -1.f : 1.f; break; }
    }
    return {sign * plane.normal()[0], sign * plane.normal()[1],
            sign * plane.normal()[2], sign * plane.dist()};
}

void addRegion(RegionEvidence& sum, const RegionEvidence& part) {
    sum.splitNodes += part.splitNodes;
    sum.leafPaths += part.leafPaths;
    sum.nonopaqueLeafPaths += part.nonopaqueLeafPaths;
    sum.brushReferences += part.brushReferences;
    sum.surfaceReferences += part.surfaceReferences;
    sum.indexedTriangleReferences += part.indexedTriangleReferences;
    sum.patchReferences += part.patchReferences;
    sum.deepestLeaf = std::max(sum.deepestLeaf, part.deepestLeaf);
}
}

BSPEvidence analyzeBSPEvidence(unsigned regionDepth, uint64_t workLimit) {
    if (regionDepth > 8) throw std::runtime_error("BSP evidence region depth exceeds 8");
    Budget budget{workLimit};
    const uint64_t records = uint64_t(bspBrushes.size()) + bspBrushSides.size() + bspModels.size()
        + bspNodes.size() + bspLeafs.size() + bspPlanes.size() + bspDrawSurfaces.size();
    if (records > 2'000'000) throw std::runtime_error("BSP evidence exceeds the two-million-record analysis limit");
    budget.spend(records);
    BSPEvidence result;
    result.brushes.resize(bspBrushes.size());

    // Difference arrays avoid expanding overlapping model spans quadratically.
    std::vector<int64_t> owners(bspBrushes.size() + 1), ownerSum(owners.size());
    for (size_t m = 0; m < bspModels.size(); ++m) {
        const auto& model = bspModels[m];
        const size_t first = size_t(model.firstBSPBrush), end = first + model.numBSPBrushes;
        ++owners[first]; --owners[end];
        ownerSum[first] += int64_t(m); ownerSum[end] -= int64_t(m);
    }
    int64_t count = 0, sum = 0;
    for (size_t b = 0; b < result.brushes.size(); ++b) {
        count += owners[b]; sum += ownerSum[b];
        result.brushes[b].model = count == 1 ? int(sum) : count == 0 ? -1 : -2;
    }

    std::map<std::array<float, 4>, size_t> groups;
    std::vector<size_t> planeGroups;
    planeGroups.reserve(bspPlanes.size());
    for (const auto& plane : bspPlanes) {
        const auto [entry, inserted] = groups.emplace(planeKey(plane), groups.size());
        (void)inserted;
        planeGroups.push_back(entry->second);
    }
    std::vector<unsigned> partitionUses(groups.size()), activePlanes(groups.size());
    std::vector<byte> seenNodes(bspNodes.size()), seenLeaves(bspLeafs.size());
    if (!bspEarlyModels.empty()) { result.worldHead = bspEarlyModels[0].headNode; result.hasWorldHead = true; }
    else if (!bspNodes.empty()) { result.worldHead = 0; result.hasWorldHead = true; }
    else if (bspLeafs.size() == 1) { result.worldHead = -1; result.hasWorldHead = true; }
    std::vector<int> pending;
    if (result.hasWorldHead) pending.push_back(result.worldHead);
    while (!pending.empty()) {
        const int index = pending.back(); pending.pop_back();
        budget.spend(1);
        if (index < 0) { seenLeaves[leafIndex(index)] = 1; continue; }
        if (seenNodes[index]) { result.uniqueNodePaths = false; continue; }
        seenNodes[index] = 1;
        ++partitionUses[planeGroups[bspNodes[index].planeNum]];
        for (int child : bspNodes[index].children) pending.push_back(child);
    }
    result.reachableNodes = std::count(seenNodes.begin(), seenNodes.end(), byte(1));
    result.reachableLeaves = std::count(seenLeaves.begin(), seenLeaves.end(), byte(1));

    // Each brush has its own flags even if adversarial brush spans overlap.
    std::vector<size_t> sideOffsets{0};
    for (const auto& brush : bspBrushes) {
        budget.spend(brush.numSides);
        if (uint64_t(sideOffsets.back()) + brush.numSides > 8'000'000)
            throw std::runtime_error("BSP evidence exceeds eight million expanded brush sides");
        sideOffsets.push_back(sideOffsets.back() + size_t(brush.numSides));
    }
    std::vector<byte> pathSides(sideOffsets.back());
    for (size_t b = 0; b < bspBrushes.size(); ++b) {
        const auto& brush = bspBrushes[b]; auto& evidence = result.brushes[b];
        evidence.mins.fill(-std::numeric_limits<double>::infinity());
        evidence.maxs.fill(std::numeric_limits<double>::infinity());
        for (int j = 0; j < brush.numSides; ++j) {
            const int side = brush.firstSide + j;
            const int plane = bspBrushSides[side].planeNum;
            if (partitionUses[planeGroups[plane]]) evidence.partitionSides.push_back(side);
            const auto& p = bspPlanes[plane];
            for (size_t axis = 0; axis < 3; ++axis) {
                if (p.normal()[axis] == 0 || p.normal()[(axis+1)%3] != 0 || p.normal()[(axis+2)%3] != 0) continue;
                const double bound = double(p.dist()) / p.normal()[axis];
                if (p.normal()[axis] > 0) evidence.maxs[axis] = std::min(evidence.maxs[axis], bound);
                else evidence.mins[axis] = std::max(evidence.mins[axis], bound);
            }
        }
        evidence.axialEnclosureAvailable = true;
        for (size_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(evidence.mins[axis]) || !std::isfinite(evidence.maxs[axis]) || evidence.mins[axis] > evidence.maxs[axis])
                evidence.axialEnclosureAvailable = false;
    }

    std::vector<RegionEvidence> leaves(bspLeafs.size());
    std::vector<int> clusters;
    for (size_t l = 0; l < bspLeafs.size(); ++l) {
        const auto& leaf = bspLeafs[l]; auto& counts = leaves[l];
        budget.spend(uint64_t(leaf.numBSPLeafBrushes) + leaf.numBSPLeafSurfaces);
        counts.leafPaths = 1; counts.nonopaqueLeafPaths = leaf.cluster >= 0;
        counts.brushReferences = leaf.numBSPLeafBrushes; counts.surfaceReferences = leaf.numBSPLeafSurfaces;
        if (leaf.cluster >= 0) clusters.push_back(leaf.cluster);
        for (int j = 0; j < leaf.numBSPLeafBrushes; ++j) {
            auto& brush = result.brushes[bspLeafBrushes[leaf.firstBSPLeafBrush + j]];
            ++brush.leafReferences;
            brush.nonopaqueReferences += leaf.cluster >= 0;
            brush.worldReferences += seenLeaves[l] != 0;
        }
        for (int j = 0; j < leaf.numBSPLeafSurfaces; ++j) {
            const auto& surface = bspDrawSurfaces[bspLeafSurfaces[leaf.firstBSPLeafSurface + j]];
            counts.indexedTriangleReferences += surface.numIndexes / 3;
            counts.patchReferences += surface.surfaceType == MST_PATCH;
        }
    }
    std::sort(clusters.begin(), clusters.end());
    result.visibility.referencedClusters = std::unique(clusters.begin(), clusters.end()) - clusters.begin();

    if (result.hasWorldHead && result.worldHead >= 0 && result.uniqueNodePaths) {
        struct Frame { int node; unsigned next, depth; bool selectedAncestor; RegionEvidence totals; };
        std::vector<Frame> stack;
        const auto push = [&](int node, unsigned depth, bool selectedAncestor) {
            ++activePlanes[planeGroups[bspNodes[node].planeNum]];
            RegionEvidence totals; totals.node = node; totals.depth = depth; totals.splitNodes = 1;
            stack.push_back({node, 0, depth, selectedAncestor, totals});
        };
        push(result.worldHead, 0, false);
        while (!stack.empty()) {
            auto& frame = stack.back();
            const auto& node = bspNodes[frame.node];
            const bool selected = !frame.selectedAncestor && (frame.depth == regionDepth || (node.children[0] < 0 && node.children[1] < 0));
            if (frame.next == 2) {
                const auto totals = frame.totals;
                if (selected) result.regions.push_back(totals);
                --activePlanes[planeGroups[node.planeNum]];
                stack.pop_back();
                if (!stack.empty()) addRegion(stack.back().totals, totals);
                continue;
            }
            const int child = node.children[frame.next++];
            if (child >= 0) { push(child, frame.depth + 1, frame.selectedAncestor || selected); continue; }
            const size_t l = leafIndex(child);
            auto totals = leaves[l]; totals.deepestLeaf = frame.depth + 1;
            addRegion(frame.totals, totals);
            const auto& leaf = bspLeafs[l];
            budget.spend(leaf.numBSPLeafBrushes);
            for (int j = 0; j < leaf.numBSPLeafBrushes; ++j) {
                const int b = bspLeafBrushes[leaf.firstBSPLeafBrush + j];
                const auto& brush = bspBrushes[b]; budget.spend(brush.numSides);
                for (int side = 0; side < brush.numSides; ++side)
                    if (activePlanes[planeGroups[bspBrushSides[brush.firstSide + side].planeNum]])
                        pathSides[sideOffsets[b] + side] = 1;
            }
        }
    }
    for (size_t b = 0; b < bspBrushes.size(); ++b)
        for (int j = 0; j < bspBrushes[b].numSides; ++j)
            if (pathSides[sideOffsets[b] + j]) result.brushes[b].leafPathSides.push_back(bspBrushes[b].firstSide + j);
    std::sort(result.regions.begin(), result.regions.end(), [](const auto& a, const auto& b) {
        return a.splitNodes != b.splitNodes ? a.splitNodes > b.splitNodes : a.node < b.node;
    });

    auto& vis = result.visibility;
    if (!bspVisBytes.empty()) {
        vis.present = true;
        int counts[2]; std::memcpy(counts, bspVisBytes.data(), sizeof(counts));
        vis.clusters = counts[0]; vis.rowBytes = counts[1];
        const size_t usedBytes = (size_t(vis.clusters) + 7) / 8;
        budget.spend(uint64_t(vis.clusters) * (usedBytes + 1));
        vis.minVisible = vis.clusters;
        for (int row = 0; row < vis.clusters; ++row) {
            const byte* data = bspVisBytes.data() + 8 + size_t(row) * vis.rowBytes;
            uint64_t visible = 0;
            for (size_t col = 0; col < usedBytes; ++col) {
                unsigned bits = data[col];
                if (col + 1 == usedBytes && vis.clusters % 8) bits &= (1u << (vis.clusters % 8)) - 1;
                visible += std::popcount(bits);
            }
            vis.visiblePairs += visible;
            vis.minVisible = std::min(vis.minVisible, visible); vis.maxVisible = std::max(vis.maxVisible, visible);
            vis.missingSelfBits += (data[size_t(row) / 8] & (1u << (row % 8))) == 0;
        }
    }
    result.workUsed = budget.used;
    return result;
}
}
