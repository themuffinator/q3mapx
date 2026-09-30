// SPDX-License-Identifier: GPL-3.0-or-later
#include "recovery_groups.h"
#include <algorithm>
#include <map>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string_view>

namespace q3mapx {
void spendGroupWork(uint64_t& used, uint64_t limit, uint64_t amount) {
    if (used>limit || amount>limit-used) throw std::runtime_error("Group inference work budget exceeded; no output was published");
    used+=amount;
}

RecoveryGroupPlan planRecoveryGroups(const std::vector<GroupBrushEvidence>& brushes, uint64_t& work, uint64_t workLimit) {
    const auto spend=[&](uint64_t n) { spendGroupWork(work,workLimit,n); };
    std::vector<size_t> parent(brushes.size());
    std::iota(parent.begin(),parent.end(),0);
    const auto root=[&](size_t i) {
        while (parent[i]!=i) { spend(1); parent[i]=parent[parent[i]]; i=parent[i]; }
        return i;
    };
    std::map<int,size_t> firstOwner;
    for(size_t i=0;i<brushes.size();++i) {
        spend(1);
        for(int surface:brushes[i].surfaces) {
            spend(1);
            const auto [entry,inserted]=firstOwner.emplace(surface,i);
            if(!inserted) {
                const size_t a=root(i),b=root(entry->second);
                parent[std::max(a,b)]=std::min(a,b);
            }
        }
    }
    std::map<size_t,std::vector<size_t>> components;
    for(size_t i=0;i<brushes.size();++i) { spend(1); components[root(i)].push_back(i); }
    RecoveryGroupPlan plan;
    std::vector<int> membership(brushes.size(),-1);
    for(const auto& [key,members]:components) {
        if(members.size()<2) continue;
        RecoveredGroup group;
        group.mins=brushes[members[0]].mins; group.maxs=brushes[members[0]].maxs;
        for(size_t i:members) {
            spend(1+brushes[i].surfaces.size());
            membership[i]=int(plan.groups.size());
            group.members.push_back(brushes[i].index);
            group.surfaces.insert(group.surfaces.end(),brushes[i].surfaces.begin(),brushes[i].surfaces.end());
            if(brushes[i].exclusion) group.status="contains_excluded_brush";
            for(size_t a=0;a<3;++a) {
                group.mins[a]=std::min(group.mins[a],brushes[i].mins[a]);
                group.maxs[a]=std::max(group.maxs[a],brushes[i].maxs[a]);
            }
        }
        std::sort(group.surfaces.begin(),group.surfaces.end());
        group.surfaces.erase(std::unique(group.surfaces.begin(),group.surfaces.end()),group.surfaces.end());
        plan.groups.push_back(std::move(group));
    }

    // Collapse prepends opaque groups and appends translucent groups. A group
    // must be contiguous within each opacity class, within the opaque prefix
    // and/or translucent suffix. Removing a rejected mixed group can invalidate
    // another group's prefix/suffix, so prune to a fixed point.
    std::vector<int> opaque,translucent;
    for(size_t i=0;i<brushes.size();++i) (brushes[i].opaque?opaque:translucent).push_back(membership[i]);
    std::vector<bool> eligible(plan.groups.size());
    for(size_t i=0;i<eligible.size();++i) eligible[i]=plan.groups[i].status==std::string_view("candidate");
    for(const auto* order:{&opaque,&translucent}) {
        std::vector<bool> seen(plan.groups.size()); int previous=-1;
        for(int group:*order) {
            spend(1);
            if(group!=previous && group>=0) {
                if(seen[group] && eligible[group]) { eligible[group]=false; plan.groups[group].status="noncontiguous_compiled_order"; }
                seen[group]=true;
            }
            previous=group;
        }
    }
    bool changed;
    do {
        changed=false;
        const auto prune=[&](auto begin,auto end) {
            bool blocked=false;
            for(auto it=begin;it!=end;++it) {
                spend(1); const int group=*it;
                if(group<0 || !eligible[group]) blocked=true;
                else if(blocked) { eligible[group]=false; plan.groups[group].status="would_reorder_world_brushes"; changed=true; }
            }
        };
        prune(opaque.begin(),opaque.end()); prune(translucent.rbegin(),translucent.rend());
    } while(changed);

    std::vector<std::vector<size_t>> edges(plan.groups.size());
    std::vector<size_t> indegree(plan.groups.size());
    const auto constrain=[&](auto begin,auto end) {
        int previous=-1;
        for(auto it=begin;it!=end;++it) {
            spend(1); const int group=*it;
            if(group<0 || !eligible[group]) continue;
            if(previous>=0 && previous!=group) { edges[previous].push_back(size_t(group)); ++indegree[group]; }
            previous=group;
        }
    };
    constrain(opaque.rbegin(),opaque.rend()); constrain(translucent.begin(),translucent.end());
    std::priority_queue<size_t,std::vector<size_t>,std::greater<size_t>> ready;
    for(size_t i=0;i<eligible.size();++i) if(eligible[i] && !indegree[i]) ready.push(i);
    while(!ready.empty()) {
        const size_t group=ready.top(); ready.pop(); plan.emissionOrder.push_back(group);
        for(size_t to:edges[group]) { spend(1); if(!--indegree[to]) ready.push(to); }
    }
    if(plan.emissionOrder.size()!=size_t(std::count(eligible.begin(),eligible.end(),true))) {
        plan.emissionOrder.clear();
        for(size_t i=0;i<eligible.size();++i) if(eligible[i]) plan.groups[i].status="conflicting_opacity_order";
    }
    else for(size_t i:plan.emissionOrder) { plan.groups[i].exported=true; plan.groups[i].status="exported"; }
    return plan;
}
}
