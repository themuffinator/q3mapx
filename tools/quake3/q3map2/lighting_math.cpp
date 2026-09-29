// SPDX-License-Identifier: GPL-3.0-or-later
#include <limits>
#include "lighting_math.h"
#include <array>
#include <vector>

float q3mapx::polygonFormFactor( const Vector3& point, const Vector3& normal,
                                std::span<const Vector3> winding ){
	if ( winding.size() < 3 ) return 0;
	// Area lights are usually triangles/quads. Keep their normalizations together
	// for register scheduling, without a 6 KiB stack probe on every sample.
	std::array<Vector3, 17> small;
	std::vector<Vector3> large;
	Vector3* dirs = small.data();
	if ( winding.size() >= small.size() ) {
		large.resize( winding.size() + 1 );
		dirs = large.data();
	}
	for ( size_t i = 0; i < winding.size(); ++i ) {
		dirs[i] = winding[i] - point;
		VectorNormalize( dirs[i] );
	}
	dirs[winding.size()] = dirs[0];
	double total = 0;
	for ( size_t i = 0; i < winding.size(); ++i ) {
		Vector3 triNormal = vector3_cross( dirs[i], dirs[i+1] );
		if ( VectorNormalize( triNormal ) < 0.0001f ) continue;
		const double angle = std::acos( std::clamp( vector3_dot( dirs[i], dirs[i+1] ), -1.0, 1.0 ) );
		total += vector3_dot( normal, triNormal ) * angle;
		if ( total > 6.3 || total < -6.3 ) return 0;
	}
	return total * c_inv_2pi;
}
