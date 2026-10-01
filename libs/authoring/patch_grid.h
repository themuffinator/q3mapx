// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cfenv>
#include <cmath>
#include <limits>

#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
#error "Patch control-field proofs require ordinary IEEE floating-point semantics"
#endif

namespace q3mapx::authoring {

// A binary32 affine relationship must not pass just because binary64 discarded
// a small residual. TwoSum recovers that residual; no tolerance is used.
inline bool exactControlSum( double a, double b, double& sum ) {
    sum = a + b;
    const double part = sum - a;
    const double residual = ( a - ( sum - part ) ) + ( b - part );
    return residual == 0;
}

// Can [a,b,c] and [c,d,e], each over half the domain, be the restrictions of
// one quadratic [a,m,e]? Then m=2b-a=2d-e and b+d=2c. Require these equations
// in exact arithmetic and require m to fit the original storage type.
// On rejection, leave the caller's output unchanged.
inline bool mergeQuadraticControls( const std::array<float, 5>& p, float& middle ) {
    static_assert( std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24 );
    static_assert( std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 );
    if ( std::fegetround() != FE_TONEAREST ) return false;
    // Some renderers enable denormals-are-zero outside the C rounding API.
    // A runtime volatile conversion detects that mode before it can hide data.
    const volatile float probe = std::numeric_limits<float>::denorm_min();
    if ( double( probe ) != 0x1p-149 ) return false;
    for ( float v : p ) if ( !std::isfinite( v ) ) return false;
    double left, right, sum;
    if ( !exactControlSum( 2.0 * p[1], -double( p[0] ), left )
      || !exactControlSum( 2.0 * p[3], -double( p[4] ), right ) || left != right
      || !exactControlSum( p[1], p[3], sum ) || sum != 2.0 * p[2]
      || std::fabs( left ) > std::numeric_limits<float>::max() ) return false;
    const float stored = static_cast<float>( left );
    if ( double( stored ) != left ) return false;
    middle = stored;
    return true;
}

inline bool mergeQuadraticControls( const std::array<unsigned char, 5>& p, unsigned char& middle ) {
    const int candidate = 2 * int( p[1] ) - p[0];
    if ( candidate < 0 || candidate > 255 || candidate != 2 * int( p[3] ) - p[4]
      || int( p[1] ) + p[3] != 2 * int( p[2] ) ) return false;
    middle = static_cast<unsigned char>( candidate );
    return true;
}

} // namespace q3mapx::authoring
