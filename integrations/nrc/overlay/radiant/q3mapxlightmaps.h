// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "authoring/surface.h"
#include "iglrender.h"
#include "irender.h"
#include "renderable.h"
#include "math/vector.h"
#include "iscriplib.h"
#include "stringio.h"
#include <vector>

class QWidget;
class Winding;
class PatchTesselation;

inline bool Q3mapxLightmaps_import( Tokeniser& tokeniser, int& sampleSize ) {
    if ( !Tokeniser_parseToken( tokeniser, q3mapx::authoring::sampleSizeKey ) ) return false;
    const char* value = tokeniser.getToken();
    if ( value == nullptr || !q3mapx::authoring::parseSampleSize( value, sampleSize ) ) {
        Tokeniser_unexpectedError( tokeniser, value, "lightmap sample size: decimal integer in 0..16384 (0 inherits)" );
        return false;
    }
    return true;
}

inline void Q3mapxLightmaps_export( TokenWriter& writer, int sampleSize ) {
    writer.writeToken( q3mapx::authoring::sampleSizeKey );
    writer.writeInteger( sampleSize );
}

inline int Q3mapxLightmaps_xmlSampleSize( const char* text ) {
    if ( text == nullptr || *text == '\0' ) return 0;
    int value;
    if ( q3mapx::authoring::parseSampleSize( text, value ) ) return value;
    globalErrorStream() << "Invalid q3mapxSampleSize1 XML attribute: " << text << '\n';
    return 0;
}

// This is a requested-spacing guide, not a reconstruction of the compiler's
// packed charts. Cache exact input geometry, and bound the generated line work.
class Q3mapxLightmapGrid final : public OpenGLRenderable {
    std::vector<DoubleVector3> m_triangles;
    std::vector<Vector3> m_lines;
    int m_sampleSize = 0;
    mutable Shader* m_state = nullptr;
    bool m_limited = false;
    double m_area = 0;
public:
    Q3mapxLightmapGrid() = default;
    Q3mapxLightmapGrid( const Q3mapxLightmapGrid& ) = delete;
    Q3mapxLightmapGrid& operator=( const Q3mapxLightmapGrid& ) = delete;
    ~Q3mapxLightmapGrid();
    void update( std::vector<DoubleVector3> triangles, int sampleSize );
    void update( const Winding& winding, int sampleSize );
    void update( const PatchTesselation& tessellation, int sampleSize );
    void render( RenderStateFlags state ) const override;
    void submit( Renderer& renderer, const Matrix4& localToWorld ) const;
    const std::vector<Vector3>& lines() const { return m_lines; }
    bool limited() const { return m_limited; }
    double area() const { return m_area; }
};

bool Q3mapxLightmaps_previewEnabled();
QWidget* Q3mapxLightmaps_create();
void Q3mapxLightmaps_update();
