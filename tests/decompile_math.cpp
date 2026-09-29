// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/affine.h"
#include <cstdlib>
#include <iostream>

static void require( bool condition, const char* message ){
	if ( !condition ) { std::cerr << message << '\n'; std::exit( 1 ); }
}

int main(){
	using namespace q3mapx;
	for ( double offset : { 0.0, 1e3, 1e7, -1e7 } ) {
		const std::array<Point2, 3> xy{{ { offset, offset }, { offset + 16, offset + 32 }, { offset + 48, offset - 8 } }};
		std::array<Point2, 3> uv;
		for ( size_t i = 0; i < 3; ++i ) uv[i] = { 0.125 * xy[i][0] - 0.75 * xy[i][1] + 7,
		    -0.375 * xy[i][0] + 0.0625 * xy[i][1] - 13 };
		Affine2 matrix;
		require( solveAffine( xy, uv, matrix ), "Translated affine solve failed" );
		require( std::fabs( matrix[0][0] - 0.125 ) < 1e-10, "Wrong U gradient" );
		require( std::fabs( matrix[1][1] - 0.0625 ) < 1e-10, "Wrong V gradient" );
		require( std::fabs( matrix[0][2] - 7 ) < 1e-7 && std::fabs( matrix[1][2] + 13 ) < 1e-7, "Wrong translation" );
	}
	Affine2 output{{ { 9, 9, 9 }, { 9, 9, 9 } }};
	const std::array<Point2, 3> uv{{ {0, 0}, {1, 0}, {0, 1} }};
	require( !solveAffine( {{{0, 0}, {1, 1}, {2, 2}}}, uv, output ), "Collinear geometry accepted" );
	require( output[0][0] == 9, "Failure partially modified output" );
	require( !solveAffine( {{{0, 0}, {1, 1}, {2, 2 + 1e-14}}}, uv, output ), "Ill-conditioned geometry accepted" );
	require( !solveAffine( uv, {{{0, 0}, {0, 0}, {0, 1}}}, output ), "Constant UV axis accepted" );
	require( !solveAffine( uv, {{{0, 0}, {INFINITY, 0}, {0, 1}}}, output ), "Infinite UV accepted" );
	std::cout << "Affine reconstruction: translation, degeneracy and finite output passed\n";
}
