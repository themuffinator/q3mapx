// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace q3mapx::authoring {

inline constexpr char brushDefinition[] = "q3mapxBrushDef1";
inline constexpr char patchDefinition[] = "q3mapxPatchDef1";
inline constexpr char sampleSizeKey[] = "lightmapSampleSize";
inline constexpr int maxSampleSize = 16384;

// Version 1 stores whole world units per luxel. Zero inherits existing settings.
// Keep this small, locale independent grammar identical in the editor/compiler.
inline bool parseSampleSize( std::string_view text, int& result ) {
    if ( text.empty() ) return false;
    int value = 0;
    for ( const char c : text ) {
        if ( c < '0' || c > '9' ) return false;
        value = value * 10 + ( c - '0' );
        if ( value > maxSampleSize ) return false;
    }
    result = value;
    return true;
}

} // namespace q3mapx::authoring
