// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>

namespace q3mapx {
// Exact signs for finite binary32 inputs, with no geometric epsilon. Throws on
// nonfinite inputs. Requires ordinary round-to-nearest IEEE binary64 arithmetic
// and a correctly rounded fma; builds using fast-math are deliberately rejected.
int orient2Exact(const std::array<float,2>& a,const std::array<float,2>& b,
                 const std::array<float,2>& c);
int orient3Exact(const std::array<float,3>& a,const std::array<float,3>& b,
                 const std::array<float,3>& c,const std::array<float,3>& d);
}
