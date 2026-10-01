// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ipatch.h"
#include <vector>

class Patch;
class Tokeniser;
class TokenWriter;
class XMLImporter;
class QWidget;
class Renderer;
class Matrix4;
class OpenGLRenderable;

bool Q3mapxPaint_importSettings( Patch&, Tokeniser& );
bool Q3mapxPaint_importControl( PatchControl&, int mode, Tokeniser& );
void Q3mapxPaint_exportSettings( const Patch&, TokenWriter& );
bool Q3mapxPaint_importXML( Patch&, const char* );
void Q3mapxPaint_exportXML( const Patch&, XMLImporter& );

// An independent snapshot: a stroke never holds pointers into an editable patch.
struct Q3mapxPaintData {
    std::size_t width = 0, height = 0;
    int mode = 0, subdivisions = 8;
    std::vector<PatchControl> controls;
    Q3mapxPaintData() = default;
    explicit Q3mapxPaintData( const Patch& );
    bool matches( const Patch& ) const;
    bool valid() const;
    std::array<unsigned char, 4> sample( double u, double v ) const;
};

struct Q3mapxPaintBrush {
    std::array<unsigned char, 4> color{255,255,255,255};
    bool rgb = false, alpha = true;
    double radius = 0.25, strength = 1, falloff = 1;
};

class Q3mapxPaintStroke {
    Q3mapxPaintData m_original, m_result;
    Q3mapxPaintBrush m_brush;
    std::vector<bool> m_mask;
    std::vector<double> m_coverage;
    double m_u = 0, m_v = 0;
    bool m_active = false, m_started = false;
public:
    bool begin( const Q3mapxPaintData&, const std::vector<bool>&, const Q3mapxPaintBrush& );
    void move( double u, double v );
    void end() { m_active = false; }
    bool active() const { return m_active; }
    const Q3mapxPaintData& result() const { return m_result; }
};

// Validates against the snapshot and makes one complete native undo operation.
bool Q3mapxPaint_apply( Patch&, const Q3mapxPaintData& before, const Q3mapxPaintData& after );
// Merge the first/last pair of spans only when all nine control channels are
// exactly representable by a single quadratic. Failure leaves output unchanged.
bool Q3mapxPaint_reduceRows( const Q3mapxPaintData&, bool column, bool first, Q3mapxPaintData& output );
QWidget* Q3mapxPaint_createButton();
QWidget* Q3mapxPaint_createPanel( QWidget* parent = nullptr );
void Q3mapxPaint_update();
bool Q3mapxPaint_previewSubmit( const Patch&, Renderer&, const Matrix4&, const OpenGLRenderable& );
