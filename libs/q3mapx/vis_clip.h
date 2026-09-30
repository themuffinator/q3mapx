// SPDX-License-Identifier: GPL-3.0-or-later
// Passage clipping derived from q3map2 visflow.cpp, Copyright (C) 1999-2007
// id Software, Inc. and contributors. See CONTRIBUTORS and COPYING.
#pragma once
#include "math/plane.h"
#include <array>
#include <cstddef>
#include <span>

namespace q3mapx {
struct PassageClipResult {
    bool visible;
    std::size_t overflows;
};

// Reusable, nonrecursive scratch. A convex input gains at most one vertex per
// cut, so input limit + separator limit covers the complete clipping sequence.
// Keep this off the native stack. Input must not alias these private buffers.
template<std::size_t Capacity>
class PassageClipper {
    static_assert(Capacity >= 3);
    enum Side : unsigned char { Front, Back, On };
    std::array<Vector3, Capacity> points_[2];
    std::array<float, Capacity + 1> distances_;
    std::array<Side, Capacity + 1> sides_;
public:
    PassageClipResult intersects(std::span<const Vector3> input,
                                 std::span<const Plane3f> planes, double epsilon) {
        if (input.size() > Capacity) return {!input.empty(), 1};
        std::size_t overflows = 0;
        unsigned buffer = 0;
        for (const auto& plane : planes) {
            if (input.empty()) break;
            unsigned front = 0, back = 0;
            for (std::size_t i = 0; i < input.size(); ++i) {
                const float d = distances_[i] = plane3_distance_to_point(plane, input[i]);
                sides_[i] = d > epsilon ? Front : d < -epsilon ? Back : On;
                front += sides_[i] == Front;
                back += sides_[i] == Back;
            }
            // Preserve the inherited epsilon and all-on-plane behavior.
            if (!back) continue;
            if (!front) return {false, overflows};
            distances_[input.size()] = distances_[0];
            sides_[input.size()] = sides_[0];
            auto& output = points_[buffer];
            std::size_t count = 0;
            bool overflow = false;
            const auto append = [&](const Vector3& p) {
                if (count == Capacity) overflow = true;
                else output[count++] = p;
            };
            for (std::size_t i = 0; i < input.size() && !overflow; ++i) {
                const Vector3& a = input[i];
                if (sides_[i] == On) { append(a); continue; }
                if (sides_[i] == Front) append(a);
                if (sides_[i + 1] == On || sides_[i + 1] == sides_[i]) continue;
                const Vector3& b = input[(i + 1) % input.size()];
                const float fraction = distances_[i] / (distances_[i] - distances_[i + 1]);
                Vector3 mid;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    if (plane.normal()[axis] == 1) mid[axis] = plane.dist();
                    else if (plane.normal()[axis] == -1) mid[axis] = -plane.dist();
                    else mid[axis] = a[axis] + fraction * (b[axis] - a[axis]);
                }
                append(mid);
            }
            if (overflow) {
                // Nonconvex/malformed input or numerical degeneracy can exceed
                // the convex bound. Discard the partial output, not input points:
                // skipping this cut can add visibility but cannot remove it.
                ++overflows;
                continue;
            }
            input = {output.data(), count};
            buffer ^= 1;
        }
        return {!input.empty(), overflows};
    }
};
}
