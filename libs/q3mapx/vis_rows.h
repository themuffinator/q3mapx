// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace q3mapx {

// Resolve the final merge forest once, retaining original runtime cluster IDs.
// Groups and their members have deterministic ascending-index order.
struct VisRowGroup {
    int representative;
    std::uint32_t first, count;
};

class VisRowGroups {
public:
    static constexpr std::size_t maxClusters = 16384;
    explicit VisRowGroups(std::span<const int> parent) {
        if(parent.size()>maxClusters) throw std::length_error("Too many VIS row clusters");
        const int n=int(parent.size());
        for(int p:parent) if(p < -1 || p >= n) throw std::invalid_argument("Invalid VIS merge parent");
        std::vector<int> roots(n,-1), path;
        std::vector<bool> visiting(n);
        for(int i=0;i<n;++i) {
            if(roots[i]>=0) continue;
            path.clear();
            int at=i;
            while(roots[at]<0) {
                if(visiting[at]) throw std::invalid_argument("Cyclic VIS merge parents");
                visiting[at]=true;
                path.push_back(at);
                if(parent[at]<0) { roots[at]=at; break; }
                at=parent[at];
            }
            for(int member:path) roots[member]=roots[at];
        }
        std::vector<int> rootGroup(n,-1);
        for(int i=0;i<n;++i) if(roots[i]==i) {
            rootGroup[i]=int(groups_.size());
            groups_.push_back({i,0,0});
        }
        groupOf_.resize(n);
        for(int i=0;i<n;++i) ++groups_[groupOf_[i]=rootGroup[roots[i]]].count;
        std::uint32_t first=0;
        for(auto& group:groups_) { group.first=first; first+=group.count; }
        members_.resize(n);
        std::vector<std::uint32_t> used(groups_.size());
        for(int i=0;i<n;++i) {
            const int g=groupOf_[i];
            members_[groups_[g].first+used[g]++]=i;
        }
    }

    std::span<const VisRowGroup> groups() const { return groups_; }
    std::span<const int> members(int group) const {
        const auto& g=groups_.at(group);
        return std::span(members_).subspan(g.first,g.count);
    }
    int groupOf(int cluster) const { return groupOf_.at(cluster); }
    std::size_t clusters() const { return members_.size(); }

    // Expand only set portal bits, and each destination group at most once.
    // Padding bits in the last portal word are ignored, as in legacy assembly.
    // All arrays are immutable across calls except the caller-owned output row.
    int expand(int sourceGroup, std::span<const std::uint64_t> portalMask,
               std::span<const int> portalGroups, std::span<std::uint64_t> row) const {
        if(row.size()!=(clusters()+63)/64 || portalMask.size()!=(portalGroups.size()+63)/64)
            throw std::invalid_argument("Invalid VIS row or portal mask width");
        std::fill(row.begin(),row.end(),0);
        int visible=0;
        const auto add=[&](int index) {
            const auto& group=groups_.at(index);
            const auto bit=std::uint64_t{1}<<(group.representative%64);
            if(row[group.representative/64]&bit) return;
            for(int cluster:members(index)) row[cluster/64]|=std::uint64_t{1}<<(cluster%64);
            visible+=int(group.count);
        };
        add(sourceGroup);
        for(std::size_t i=0;i<portalMask.size();++i) {
            auto bits=portalMask[i];
            while(bits) {
                const auto portal=i*64+std::countr_zero(bits);
                if(portal>=portalGroups.size()) break;
                add(portalGroups[portal]);
                bits&=bits-1;
            }
        }
        return visible;
    }

private:
    std::vector<VisRowGroup> groups_;
    std::vector<int> groupOf_,members_;
};

} // namespace q3mapx
