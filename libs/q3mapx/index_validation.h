// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace q3mapx {
// Validate relative triangle indices without expanding shared surface ranges.
// The immutable source must outlive this object. Short/disjoint workloads scan
// directly. Repeated long scans trigger a linear-size, blocked maximum tree.
// Invalid queries return the first offending source offset, preserving ordered
// diagnostics even when several surfaces share indices with different limits.
class IndexRangeValidator {
public:
    explicit IndexRangeValidator(std::span<const int> indices)
        : indices_(indices), directRemaining_(2 * uint64_t(indices.size())) {
        static_assert(std::numeric_limits<int>::digits == 31);
    }

    std::optional<size_t> firstInvalid(size_t first, size_t count, int vertexCount) {
        if (first > indices_.size() || count > indices_.size() - first)
            throw std::out_of_range("Index validation span exceeds its source");
        if (vertexCount < 0) throw std::invalid_argument("Negative vertex count in index validation");
        const auto limit = unsigned(vertexCount);
        const size_t end = first + count;
        // Small slices cannot benefit from a full block lookup, and their work
        // is already bounded per surface. They never force a cache allocation.
        if (count < 2 * blockSize) return scan(first, end, limit);
        if (tree_.empty()) {
            if (count <= directRemaining_) {
                directRemaining_ -= count;
                return scan(first, end, limit);
            }
            build();
        }
        const size_t beginFull = first + (blockSize - first % blockSize) % blockSize;
        const size_t endFull = end - end % blockSize;
        if (auto bad = scan(first, beginFull, limit)) return bad;
        size_t left = beginFull / blockSize, right = endFull / blockSize;
        if (left < right && maximum(left, right) >= limit) {
            // Only a failing range takes this logarithmic search. Querying
            // prefixes locates the earliest bad block without rescanning a
            // potentially enormous range to reconstruct the old diagnostic.
            while (right - left > 1) {
                const size_t middle = left + (right - left) / 2;
                if (maximum(left, middle) >= limit) right = middle;
                else left = middle;
            }
            return scan(left * blockSize, (left + 1) * blockSize, limit);
        }
        return scan(endFull, end, limit);
    }

    // Deterministic work/storage evidence for regression tests and profiling.
    uint64_t sourceValuesExamined() const { return examined_; }
    size_t auxiliaryBytes() const { return tree_.capacity() * sizeof(unsigned); }

private:
    static constexpr size_t blockSize = 64;
    std::span<const int> indices_;
    uint64_t directRemaining_, examined_ = 0;
    size_t blocks_ = 0;
    std::vector<unsigned> tree_;

    std::optional<size_t> scan(size_t first, size_t end, unsigned limit) {
        for (size_t i = first; i < end; ++i) {
            // All legal limits are nonnegative signed ints. Casting negative
            // indices above INT_MAX lets one maximum detect either violation.
            if (unsigned(indices_[i]) >= limit) {
                examined_ += i - first + 1;
                return i;
            }
        }
        examined_ += end - first;
        return std::nullopt;
    }

    void build() {
        blocks_ = indices_.size() / blockSize + (indices_.size() % blockSize != 0);
        tree_.resize(2 * blocks_);
        for (size_t block = 0; block < blocks_; ++block) {
            const size_t begin = block * blockSize, end = std::min(indices_.size(), begin + blockSize);
            unsigned value = 0;
            for (size_t i = begin; i < end; ++i) value = std::max(value, unsigned(indices_[i]));
            tree_[blocks_ + block] = value;
        }
        for (size_t node = blocks_ - 1; node != 0; --node)
            tree_[node] = std::max(tree_[node * 2], tree_[node * 2 + 1]);
        examined_ += indices_.size();
    }

    unsigned maximum(size_t first, size_t end) const {
        unsigned value = 0;
        for (first += blocks_, end += blocks_; first < end; first /= 2, end /= 2) {
            if (first & 1) value = std::max(value, tree_[first++]);
            if (end & 1) value = std::max(value, tree_[--end]);
        }
        return value;
    }
};
}
