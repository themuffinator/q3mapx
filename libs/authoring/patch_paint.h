// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace q3mapx::authoring {

inline constexpr char paintedPatchDefinition[] = "q3mapxPatchDef2";
inline constexpr char paintBindingKey[] = "_q3mapx_patchPaint1";
inline constexpr int maxPaintVertices = 65536;

// Zero denotes ordinary legacy/density-only patches. Alpha leaves vertex RGB to
// LIGHT; material stores RGB as material data, independent of vertex lighting.
enum PatchPaintMode { noPaint = 0, alphaPaint = 1, materialPaint = 2 };

inline bool parsePaintMode( std::string_view text, int& value ) {
    if ( text == "lighting" ) value = alphaPaint;
    else if ( text == "material" ) value = materialPaint;
    else return false;
    return true;
}

inline bool parsePaintByte( std::string_view text, int& value ) {
    if ( text.empty() || text.size() > 3 ) return false;
    int n = 0;
    for ( const char c : text ) {
        if ( c < '0' || c > '9' ) return false;
        n = n * 10 + c - '0';
    }
    if ( n > 255 ) return false;
    value = n;
    return true;
}

inline bool parsePaintSubdivisions( std::string_view text, int& value ) {
    if ( !parsePaintByte( text, value ) ) return false;
    return value >= 1 && value <= 32 && ( value & ( value - 1 ) ) == 0;
}

// The inherited geometric tessellator uses at most 16 segments per quadratic
// span. Validate the worst-case dimensions before any BSP-stage output is made.
inline bool paintMeshFits( int width, int height, int segments ) {
    if ( width < 3 || width > 31 || height < 3 || height > 31
      || !( width & 1 ) || !( height & 1 ) || segments < 1 || segments > 32 ) return false;
    const int n = segments < 16 ? 16 : segments;
    return ( ( width - 1 ) / 2 * n + 1 ) * ( ( height - 1 ) / 2 * n + 1 ) <= maxPaintVertices;
}

} // namespace q3mapx::authoring
