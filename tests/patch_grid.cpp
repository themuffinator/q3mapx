// SPDX-License-Identifier: GPL-3.0-or-later
#include "authoring/patch_grid.h"
#include <bit>
#include <cstdint>
#include <iostream>
#include <string>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif

int main() {
    using q3mapx::authoring::mergeQuadraticControls;
    const std::array<float,5> exact{0, 1, 2, 3, 4};
    for ( int rounding : {FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO} ) {
        if ( std::fesetround( rounding ) != 0 ) return 1;
        float unchanged = 17;
        if ( mergeQuadraticControls( exact, unchanged ) || unchanged != 17 ) return 2;
    }
    if ( std::fesetround( FE_TONEAREST ) != 0 ) return 3;
#if defined(__SSE__)
    const unsigned state = _mm_getcsr();
    _mm_setcsr( state | 0x40 ); // Denormals-are-zero must not turn proof into approximation.
    float unchanged = 17;
    const bool unsafe = mergeQuadraticControls( exact, unchanged );
    _mm_setcsr( state );
    if ( unsafe || unchanged != 17 ) return 9;
#endif
    std::string kind;
    while ( std::cin >> kind ) {
        std::array<std::uint32_t,5> bits;
        for ( auto& v : bits ) if ( !(std::cin >> v) ) return 4;
        if ( kind == "float" ) {
            std::array<float,5> values;
            for ( int i = 0; i < 5; ++i ) values[i] = std::bit_cast<float>(bits[i]);
            float result = 17;
            const bool good = mergeQuadraticControls( values, result );
            if ( !good && result != 17 ) return 5;
            std::cout << good << ' ' << std::bit_cast<std::uint32_t>(result) << '\n';
        }
        else if ( kind == "byte" ) {
            std::array<unsigned char,5> values;
            for ( int i = 0; i < 5; ++i ) {
                if ( bits[i] > 255 ) return 6;
                values[i] = bits[i];
            }
            unsigned char result = 17;
            const bool good = mergeQuadraticControls( values, result );
            if ( !good && result != 17 ) return 7;
            std::cout << good << ' ' << unsigned(result) << '\n';
        }
        else return 8;
    }
}
