// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace q3mapx {
struct GroupBrushEvidence {
    int index = -1;
    bool opaque = true;
    const char* exclusion = nullptr;
    std::array<double, 3> mins{}, maxs{};
    std::vector<int> surfaces;
};
struct RecoveredGroup {
    std::vector<int> members, surfaces;
    std::array<double, 3> mins{}, maxs{};
    const char* status = "candidate";
    bool exported = false;
};
struct RecoveryGroupPlan {
    std::vector<RecoveredGroup> groups;
    std::vector<size_t> emissionOrder;
};
void spendGroupWork(uint64_t& used, uint64_t limit, uint64_t amount);
// Input brushes are in compiled record order. Shared render surfaces propose
// membership; only groups compatible with q3mapx's opaque/translucent insertion
// and collapse order are exported. This does not prove original authoring groups.
RecoveryGroupPlan planRecoveryGroups(const std::vector<GroupBrushEvidence>& brushes,
                                    uint64_t& work, uint64_t workLimit);
}
