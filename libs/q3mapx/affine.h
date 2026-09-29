// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace q3mapx {
using Point2 = std::array<double, 2>;
using Affine2 = std::array<std::array<double, 3>, 2>;

// Solve on edge differences to avoid cancellation for translated geometry.
// A constant UV axis cannot be represented by finite Valve 220/Quake scale.
inline bool solveAffine( const std::array<Point2, 3>& xy, const std::array<Point2, 3>& uv, Affine2& result ){
	const double x1 = xy[1][0] - xy[0][0], y1 = xy[1][1] - xy[0][1];
	const double x2 = xy[2][0] - xy[0][0], y2 = xy[2][1] - xy[0][1];
	const double determinant = x1 * y2 - x2 * y1;
	const double scale = std::max( x1 * x1 + y1 * y1, x2 * x2 + y2 * y2 );
	if ( !std::isfinite( determinant ) || std::fabs( determinant ) <= scale * 1e-12 ) return false;
	Affine2 candidate{};
	for ( size_t axis = 0; axis < 2; ++axis ) {
		const double d1 = uv[1][axis] - uv[0][axis], d2 = uv[2][axis] - uv[0][axis];
		auto& row = candidate[axis];
		row[0] = ( d1 * y2 - d2 * y1 ) / determinant;
		row[1] = ( x1 * d2 - x2 * d1 ) / determinant;
		row[2] = uv[0][axis] - row[0] * xy[0][0] - row[1] * xy[0][1];
		for ( double value : row ) {
			if ( !std::isfinite( value ) || std::fabs( value ) > std::numeric_limits<float>::max() ) return false;
		}
		if ( std::hypot( row[0], row[1] ) < 1e-12 ) return false;
	}
	result = candidate;
	return true;
}
} // namespace q3mapx
