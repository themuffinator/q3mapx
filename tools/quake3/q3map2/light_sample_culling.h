// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <limits>
#include <span>
#include <vector>
#include "qmath.h"

namespace q3mapx {
// Conservative bounds of actual mapped sample positions, including nudged and
// curved surfaces. This only skips initial samples that the light's own distance
// test would reject; adaptive sampling, filters and material tracing stay intact.
class LightSampleTiles {
	struct Bounds {
		MinMax box;
		bool mapped = false, unbounded = false;
	};
	int columns_ = 0;
	std::vector<Bounds> bounds_;
	std::vector<unsigned char> active_;
public:
	static constexpr int tileSize = 8;
	void build( int width, int height, std::span<const Vector3> origins, std::span<const int> clusters ){
		columns_ = ( width + tileSize - 1 ) / tileSize;
		bounds_.assign( size_t( columns_ ) * ( ( height + tileSize - 1 ) / tileSize ), {} );
		active_.assign( bounds_.size(), 1 );
		for ( int y = 0; y < height; ++y ) for ( int x = 0; x < width; ++x ) {
			const size_t sample = size_t( y ) * width + x;
			if ( clusters[sample] < 0 ) continue;
			auto& bounds = bounds_[size_t( y / tileSize ) * columns_ + x / tileSize];
			bounds.mapped = true;
			const auto& p = origins[sample];
			if ( !std::isfinite( p.x() ) || !std::isfinite( p.y() ) || !std::isfinite( p.z() ) ) bounds.unbounded = true;
			else bounds.box.extend( p );
		}
	}
	void select( const Vector3& origin, float envelope ){
		const bool unlimited = !std::isfinite( envelope ) || envelope < 0
			|| !std::isfinite( origin.x() ) || !std::isfinite( origin.y() ) || !std::isfinite( origin.z() );
		for ( size_t i = 0; i < bounds_.size(); ++i ) {
			const auto& b = bounds_[i];
			if ( !b.mapped ) { active_[i] = 0; continue; }
			if ( unlimited || b.unbounded ) { active_[i] = 1; continue; }
			double distance2 = 0, scale = std::abs( double( envelope ) ) + 1;
			for ( int axis = 0; axis < 3; ++axis ) {
				const double p = origin[axis], lo = b.box.mins[axis], hi = b.box.maxs[axis];
				const double delta = std::max( { lo - p, p - hi, 0.0 } );
				distance2 += delta * delta;
				scale = std::max( { scale, std::abs( p ), std::abs( lo ), std::abs( hi ) } );
			}
			// Legacy subtraction, length and distance storage round to float. Expand
			// the double-precision bound to keep all samples within that uncertainty.
			const double radius = envelope + 32 * std::numeric_limits<float>::epsilon() * scale;
			active_[i] = distance2 <= radius * radius;
		}
	}
	bool active( int x, int y ) const {
		return active_[size_t( y / tileSize ) * columns_ + x / tileSize] != 0;
	}
};
}
