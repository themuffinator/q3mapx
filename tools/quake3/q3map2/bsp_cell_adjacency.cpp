// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "bsp_evidence.h"
#include <stdexcept>

namespace q3mapx {
CellGraph analyzeBSPCellAdjacency(BSPEvidence& evidence,uint64_t workLimit) {
    if(!evidence.hasWorldHead || !evidence.uniqueNodePaths || bspModels.empty()) {
        CellGraph result; result.status="world_tree_unavailable"; result.workUsed=evidence.workUsed; return result;
    }
    std::vector<CellNode> nodes;
    nodes.reserve(bspNodes.size());
    for(const auto& node:bspNodes) {
        const auto& plane=bspPlanes[node.planeNum];
        nodes.push_back({{{plane.normal()[0],plane.normal()[1],plane.normal()[2]},plane.dist()},
                         {node.children[0],node.children[1]}});
    }
    std::vector<int> clusters;
    clusters.reserve(bspLeafs.size());
    for(const auto& leaf:bspLeafs) clusters.push_back(leaf.cluster);
    CellPoint lo,hi;
    for(size_t a=0;a<3;++a) {
        // This artificial enclosure is explicit in the report. Open cell faces
        // touching it make exterior completeness unknown, not a certified leak.
        lo[a]=double(bspModels[0].minmax.mins[a])-1;
        hi[a]=double(bspModels[0].minmax.maxs[a])+1;
    }
    CellLimits limits; limits.work=workLimit;
    auto result=reconstructCellGraph(nodes,clusters,evidence.worldHead,lo,hi,limits,evidence.workUsed);
    evidence.workUsed=result.workUsed;
    return result;
}
}
