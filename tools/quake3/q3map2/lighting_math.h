// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "qmath.h"
#include "math/pi.h"
#include <span>

namespace q3mapx {
// Exact NRC precision, winding order and degenerate-edge handling. Scratch space
// includes the closing vertex, even at and beyond the old 512-point boundary.
float polygonFormFactor( const Vector3& point, const Vector3& normal,
                         std::span<const Vector3> winding );
}
