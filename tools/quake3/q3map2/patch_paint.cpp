// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "authoring/patch_paint.h"

// Evaluate the source Bezier field once in double precision. Repeated byte
// averaging darkens gradients, and geometric collinearity says nothing about
// the color field. Painted meshes deliberately bypass both legacy operations.
mesh_t TessellatedPaintMesh( const mesh_view_t in, int segments ) {
    if ( !q3mapx::authoring::paintMeshFits( in.width, in.height, segments ) )
        Error( "Painted patch exceeds the %d-vertex tessellation budget", q3mapx::authoring::maxPaintVertices );
    const int spansX = ( in.width - 1 ) / 2, spansY = ( in.height - 1 ) / 2;
    mesh_t result( spansX * segments + 1, spansY * segments + 1 );
    for ( int y = 0; y < result.height; ++y ) {
        const int spanY = std::min( y / segments, spansY - 1 );
        const double v = double( y - spanY * segments ) / segments;
        const double by[] = { ( 1 - v ) * ( 1 - v ), 2 * v * ( 1 - v ), v * v };
        for ( int x = 0; x < result.width; ++x ) {
            const int spanX = std::min( x / segments, spansX - 1 );
            const double u = double( x - spanX * segments ) / segments;
            const double bx[] = { ( 1 - u ) * ( 1 - u ), 2 * u * ( 1 - u ), u * u };
            double xyz[3]{}, st[2]{}, normal[3]{}, rgba[4]{};
            for ( int j = 0; j < 3; ++j ) for ( int i = 0; i < 3; ++i ) {
                const double weight = bx[i] * by[j];
                const bspDrawVert_t& control = in[spanY * 2 + j][spanX * 2 + i];
                for ( int k = 0; k < 3; ++k ) {
                    xyz[k] += weight * control.xyz[k];
                    normal[k] += weight * control.normal[k];
                }
                for ( int k = 0; k < 2; ++k ) st[k] += weight * control.st[k];
                for ( int k = 0; k < 4; ++k ) rgba[k] += weight * control.color[0][k];
            }
            bspDrawVert_t& out = result[y][x];
            out = c_bspDrawVert_t0;
            for ( int k = 0; k < 3; ++k ) { out.xyz[k] = xyz[k]; out.normal[k] = normal[k]; }
            for ( int k = 0; k < 2; ++k ) out.st[k] = st[k];
            if ( VectorNormalize( out.normal ) == 0 ) out.normal = in[spanY * 2][spanX * 2].normal;
            Color4b color;
            for ( int k = 0; k < 4; ++k ) color[k] = std::clamp( int( std::floor( rgba[k] + 0.5 ) ), 0, 255 );
            out.color.fill( color );
        }
    }
    return result;
}
