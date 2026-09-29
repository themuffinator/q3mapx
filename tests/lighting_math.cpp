// SPDX-License-Identifier: GPL-3.0-or-later
#include <limits>
#include "lighting_math.h"
#include "light_sample_culling.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

// Imported math retained as a dynamically-sized oracle, including the closing
// vertex. This verifies the compact scratch buffer against the original math.
static float reference( const Vector3& point, const Vector3& normal, std::span<const Vector3> w ){
	if ( w.empty() ) return 0;
	std::vector<Vector3> dirs( w.size() + 1 );
	for ( size_t i = 0; i < w.size(); ++i ) {
		dirs[i] = w[i] - point;
		VectorNormalize( dirs[i] );
	}
	dirs[w.size()] = dirs[0];
	double total = 0;
	for ( size_t i = 0; i < w.size(); ++i ) {
		Vector3 triNormal = vector3_cross( dirs[i], dirs[i+1] );
		if ( VectorNormalize( triNormal ) < 0.0001f ) continue;
		const double angle = std::acos( std::clamp( vector3_dot( dirs[i], dirs[i+1] ), -1.0, 1.0 ) );
		total += vector3_dot( normal, triNormal ) * angle;
		if ( total > 6.3 || total < -6.3 ) return 0;
	}
	return total * c_inv_2pi;
}

static void require( bool condition, const char* message ){
	if ( !condition ) { std::cerr << message << '\n'; std::exit( 1 ); }
}

int main(){
	for ( size_t count : { 0u, 1u, 2u, 3u, 4u, 8u, 64u, 511u, 512u, 513u, 1024u } ) {
		for ( float offset : { 0.f, 10000.f, -10000.f } ) {
			std::vector<Vector3> polygon;
			for ( size_t i = 0; i < count; ++i ) {
				const double angle = c_2pi * i / count;
				polygon.emplace_back( offset + 64 * std::cos( angle ), offset + 64 * std::sin( angle ), 128 );
			}
			for ( const Vector3 point : { Vector3( offset, offset, 0 ), Vector3( offset + 128, offset, 16 ),
			                              Vector3( offset, offset, 128 ), Vector3( offset, offset, 256 ) } ) {
				for ( const Vector3 normal : { Vector3( 0, 0, 1 ), Vector3( 0, 0, -1 ), Vector3( 1, 0, 0 ) } ) {
					const float actual = q3mapx::polygonFormFactor( point, normal, polygon );
					require( std::isfinite( actual ), "Non-finite polygon lighting" );
					require( actual == reference( point, normal, polygon ), "Polygon lighting differs from reference" );
				}
			}
			if ( !polygon.empty() ) {
				polygon.push_back( polygon.front() );
				require( q3mapx::polygonFormFactor( polygon.front(), Vector3( 0, 0, 1 ), polygon )
				         == reference( polygon.front(), Vector3( 0, 0, 1 ), polygon ), "Degenerate edge differs" );
			}
		}
	}
	std::mt19937 random( 173 );
	std::uniform_real_distribution<float> uniform( -1, 1 );
	for ( float scale : { 64.f, 1000000.f, 1e20f } ) {
		constexpr int width = 19, height = 23;
		std::vector<Vector3> origins( width * height );
		std::vector<int> clusters( origins.size() );
		for ( size_t i = 0; i < origins.size(); ++i ) {
			origins[i] = Vector3( uniform(random), uniform(random), uniform(random) ) * scale;
			clusters[i] = i % 13 == 0 ? -1 : 0;
		}
		q3mapx::LightSampleTiles tiles;
		tiles.build( width, height, origins, clusters );
		int culled = 0;
		for ( int trial = 0; trial < 100; ++trial ) {
			const Vector3 light = Vector3( uniform(random), uniform(random), uniform(random) ) * ( scale * 3 );
			const float radius = std::abs( uniform(random) ) * scale;
			tiles.select( light, radius );
			for ( size_t i = 0; i < origins.size(); ++i ) {
				Vector3 displacement = light - origins[i];
				const float distance = VectorNormalize( displacement );
				const bool active = tiles.active( i % width, i / width );
				if ( clusters[i] >= 0 && distance < radius ) require( active, "Spatial culling lost a reachable sample" );
				culled += !active;
			}
		}
		require( culled > 0, "Spatial culling rejected no samples" );
		// Boundary/coincident points and float subtraction rounding at large offsets.
		std::fill( origins.begin(), origins.end(), Vector3( scale, 0, 0 ) );
		tiles.build( width, height, origins, clusters );
		tiles.select( Vector3( 0 ), scale );
		require( tiles.active( 1, 0 ), "Tangent sample culled" );
		tiles.select( Vector3( scale, 0, 0 ), 0 );
		require( tiles.active( 1, 0 ), "Coincident sample culled" );
	}
	std::cout << "Polygon lighting: exact reference/512-point boundary; conservative sample culling passed\n";
}
