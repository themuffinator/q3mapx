// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapxpaint.h"
#include "patch.h"
#include "brush.h"
#include "iundo.h"
#include "scenelib.h"
#include "authoring/patch_grid.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <locale>
#include <memory>
#include <sstream>

namespace {
using namespace q3mapx::authoring;
unsigned char rounded( double value ) {
    return static_cast<unsigned char>( std::clamp( std::floor( value + 0.5 ), 0.0, 255.0 ) );
}
bool sameControl( const PatchControl& a, const PatchControl& b ) {
    return a.m_vertex == b.m_vertex && a.m_texcoord == b.m_texcoord && a.m_color == b.m_color;
}
bool finiteControl( const PatchControl& p ) {
    for ( int i = 0; i < 3; ++i ) if ( !std::isfinite( p.m_vertex[i] ) ) return false;
    for ( int i = 0; i < 2; ++i ) if ( !std::isfinite( p.m_texcoord[i] ) ) return false;
    return true;
}
bool paintError( const char* expected ) {
    globalErrorStream() << "Invalid q3mapx patch paint: expected " << expected << '\n';
    return false;
}
}

bool Q3mapxPaint_importSettings( Patch& patch, Tokeniser& reader ) {
    reader.nextLine();
    if ( !Tokeniser_parseToken( reader, "vertexRGB" ) ) return false;
    const char* token = reader.getToken();
    int mode, subdivisions;
    if ( !token || !parsePaintMode( token, mode ) ) return paintError( "vertexRGB lighting or material" );
    reader.nextLine();
    if ( !Tokeniser_parseToken( reader, "paintSubdivisions" ) ) return false;
    token = reader.getToken();
    if ( !token || !parsePaintSubdivisions( token, subdivisions ) ) return paintError( "paintSubdivisions 1, 2, 4, 8, 16 or 32" );
    if ( !patch.setPaintSettings( mode, subdivisions ) ) return paintError( "paint grid within compiler limits" );
    return true;
}

bool Q3mapxPaint_importControl( PatchControl& p, int mode, Tokeniser& reader ) {
    if ( !finiteControl( p ) ) return paintError( "finite position and texture coordinates" );
    for ( unsigned char& c : p.m_color ) {
        const char* token = reader.getToken();
        int value;
        if ( !token || !parsePaintByte( token, value ) ) return paintError( "RGBA decimal bytes in 0..255" );
        c = value;
    }
    if ( mode == alphaPaint && ( p.m_color[0] != 255 || p.m_color[1] != 255 || p.m_color[2] != 255 ) )
        return paintError( "white RGB in lighting mode" );
    return true;
}

void Q3mapxPaint_exportSettings( const Patch& patch, TokenWriter& writer ) {
    if ( patch.paintMode() == noPaint ) return;
    writer.writeToken( "vertexRGB" );
    writer.writeToken( patch.paintMode() == materialPaint ? "material" : "lighting" );
    writer.nextLine();
    writer.writeToken( "paintSubdivisions" );
    writer.writeInteger( patch.paintSubdivisions() );
    writer.nextLine();
}

bool Q3mapxPaint_importXML( Patch& patch, const char* text ) {
    // XMLImporter cannot return a document failure. Leave the existing patch
    // untouched on invalid clipboard paint and issue a diagnostic.
    Q3mapxPaintData data( patch );
    std::istringstream stream( text );
    stream.imbue( std::locale::classic() );
    std::string token;
    if ( !( stream >> token ) || !parsePaintMode( token, data.mode )
      || !( stream >> token ) || !parsePaintSubdivisions( token, data.subdivisions ) )
        return paintError( "valid XML paint mode and subdivisions; paint was not imported" );
    for ( PatchControl& p : data.controls ) for ( auto& c : p.m_color ) {
        int value;
        if ( !( stream >> token ) || !parsePaintByte( token, value ) )
            return paintError( "complete XML RGBA matrix; paint was not imported" );
        c = value;
    }
    if ( stream >> token || !data.valid() || !patch.setPaintSettings( data.mode, data.subdivisions ) )
        return paintError( "valid XML paint field; paint was not imported" );
    std::copy( data.controls.begin(), data.controls.end(), patch.begin() );
    patch.controlPointsChanged();
    return true;
}

void Q3mapxPaint_exportXML( const Patch& patch, XMLImporter& importer ) {
    if ( patch.paintMode() == noPaint ) return;
    const StaticElement element( "q3mapxPaint2" );
    importer.pushElement( element );
    importer << ( patch.paintMode() == materialPaint ? "material " : "lighting " ) << patch.paintSubdivisions() << ' ';
    for ( const PatchControl& p : patch ) for ( auto c : p.m_color ) importer << int( c ) << ' ';
    importer.popElement( element.name() );
}

Q3mapxPaintData::Q3mapxPaintData( const Patch& p ) :
    width( p.getWidth() ), height( p.getHeight() ), mode( p.paintMode() ), subdivisions( p.paintSubdivisions() ),
    controls( p.begin(), p.end() ) {}

bool Q3mapxPaintData::matches( const Patch& p ) const {
    return width == p.getWidth() && height == p.getHeight() && mode == p.paintMode() && subdivisions == p.paintSubdivisions()
        && controls.size() == width * height && std::equal( controls.begin(), controls.end(), p.begin(), sameControl );
}

bool Q3mapxPaintData::valid() const {
    if ( width < 3 || width > 31 || height < 3 || height > 31
      || mode < noPaint || mode > materialPaint || subdivisions < 1 || subdivisions > 32
      || ( subdivisions & ( subdivisions - 1 ) ) || !paintMeshFits( width, height, subdivisions )
      || controls.size() != width * height ) return false;
    for ( const PatchControl& p : controls ) {
        if ( !finiteControl( p ) ) return false;
        if ( mode != materialPaint && ( p.m_color[0] != 255 || p.m_color[1] != 255 || p.m_color[2] != 255 ) ) return false;
        if ( mode == noPaint && p.m_color[3] != 255 ) return false;
    }
    return true;
}

std::array<unsigned char,4> Q3mapxPaintData::sample( double u, double v ) const {
    if ( width < 3 || height < 3 || controls.size() != width * height || !std::isfinite( u ) || !std::isfinite( v ) ) return {255,255,255,255};
    const double x = std::clamp( u, 0.0, 1.0 ) * ( ( width - 1 ) / 2 );
    const double y = std::clamp( v, 0.0, 1.0 ) * ( ( height - 1 ) / 2 );
    const std::size_t c = std::min( std::size_t( x ), ( width - 3 ) / 2 );
    const std::size_t r = std::min( std::size_t( y ), ( height - 3 ) / 2 );
    const double a = x - c, b = y - r;
    const double wx[] = { (1-a)*(1-a), 2*a*(1-a), a*a };
    const double wy[] = { (1-b)*(1-b), 2*b*(1-b), b*b };
    std::array<unsigned char,4> color;
    for ( int channel = 0; channel < 4; ++channel ) {
        double value = 0;
        for ( int j = 0; j < 3; ++j ) for ( int i = 0; i < 3; ++i )
            value += wx[i] * wy[j] * controls[(2*r+j)*width+2*c+i].m_color[channel];
        color[channel] = rounded( value );
    }
    return color;
}

bool Q3mapxPaintStroke::begin( const Q3mapxPaintData& data, const std::vector<bool>& mask, const Q3mapxPaintBrush& brush ) {
    m_active = false;
    if ( !data.valid() || mask.size() != data.controls.size() || !( brush.rgb || brush.alpha )
      || !std::isfinite( brush.radius ) || brush.radius <= 0 || brush.radius > 2
      || !std::isfinite( brush.strength ) || brush.strength <= 0 || brush.strength > 1
      || !std::isfinite( brush.falloff ) || brush.falloff < 0 || brush.falloff > 8 ) return false;
    m_original = m_result = data;
    m_mask = mask;
    m_brush = brush;
    m_coverage.assign( data.controls.size(), 0 );
    m_started = false;
    m_active = true;
    return true;
}

void Q3mapxPaintStroke::move( double u, double v ) {
    if ( !m_active || !std::isfinite( u ) || !std::isfinite( v ) ) return;
    u = std::clamp( u, 0.0, 1.0 ); v = std::clamp( v, 0.0, 1.0 );
    if ( !m_started ) { m_u = u; m_v = v; m_started = true; }
    const double dx = u-m_u, dy = v-m_v, length2 = dx*dx+dy*dy;
    for ( std::size_t i = 0; i < m_mask.size(); ++i ) {
        if ( !m_mask[i] ) continue;
        const double x = double( i % m_original.width ) / ( m_original.width - 1 );
        const double y = double( i / m_original.width ) / ( m_original.height - 1 );
        const double t = length2 == 0 ? 0 : std::clamp( ((x-m_u)*dx+(y-m_v)*dy)/length2, 0.0, 1.0 );
        const double distance = std::hypot( x-m_u-t*dx, y-m_v-t*dy );
        if ( distance >= m_brush.radius ) continue;
        const double influence = m_brush.strength * std::pow( 1-distance/m_brush.radius, m_brush.falloff );
        if ( influence <= m_coverage[i] ) continue;
        m_coverage[i] = influence;
        for ( int c = 0; c < 4; ++c ) if ( c == 3 ? m_brush.alpha : m_brush.rgb )
            m_result.controls[i].m_color[c] = rounded( m_original.controls[i].m_color[c] * (1-influence) + m_brush.color[c] * influence );
        if ( m_brush.rgb ) m_result.mode = materialPaint; // explicit white is meaningful material data
        else if ( m_result.controls[i].m_color[3] != 255 ) m_result.mode = std::max( m_result.mode, int( alphaPaint ) );
    }
    m_u = u; m_v = v;
}

bool Q3mapxPaint_apply( Patch& patch, const Q3mapxPaintData& before, const Q3mapxPaintData& after ) {
    if ( !before.matches( patch ) || !after.valid() || patch.m_patchDef3
      || before.width != after.width || before.height != after.height ) return false;
    // Painting must never alter geometry or texture coordinates.
    for ( std::size_t i = 0; i < before.controls.size(); ++i )
        if ( before.controls[i].m_vertex != after.controls[i].m_vertex || before.controls[i].m_texcoord != after.controls[i].m_texcoord ) return false;
    if ( after.matches( patch ) ) return true;
    UndoableCommand command( "q3mapx patch paint" );
    patch.undoSave();
    if ( !patch.setPaintSettings( after.mode, after.subdivisions ) ) return false;
    std::copy( after.controls.begin(), after.controls.end(), patch.begin() );
    patch.controlPointsChanged();
    Patch_textureChanged();
    SceneChangeNotify();
    return true;
}

bool Q3mapxPaint_reduceRows( const Q3mapxPaintData& source, bool column, bool first, Q3mapxPaintData& output ) {
    if ( !source.valid() ) return false;
    const std::size_t length = column ? source.width : source.height;
    if ( length < 5 ) return false;
    const std::size_t start = first ? 0 : length - 5;
    const std::size_t lines = column ? source.height : source.width;
    Q3mapxPaintData result = source;
    ( column ? result.width : result.height ) -= 2;
    result.controls.resize( result.width * result.height );
    const auto index = [column]( const Q3mapxPaintData& data, std::size_t line, std::size_t point ) {
        return column ? line * data.width + point : point * data.width + line;
    };
    for ( std::size_t line = 0; line < lines; ++line ) {
        PatchControl merged = source.controls[index( source, line, start + 1 )];
        std::array<float, 5> values;
        for ( int channel = 0; channel < 5; ++channel ) {
            for ( std::size_t i = 0; i < values.size(); ++i ) {
                const auto& point = source.controls[index( source, line, start + i )];
                values[i] = channel < 3 ? point.m_vertex[channel] : point.m_texcoord[channel-3];
            }
            float& target = channel < 3 ? merged.m_vertex[channel] : merged.m_texcoord[channel-3];
            if ( !mergeQuadraticControls( values, target ) ) return false;
        }
        for ( int channel = 0; channel < 4; ++channel ) {
            std::array<unsigned char, 5> colors;
            for ( std::size_t i = 0; i < colors.size(); ++i )
                colors[i] = source.controls[index( source, line, start+i )].m_color[channel];
            if ( !mergeQuadraticControls( colors, merged.m_color[channel] ) ) return false;
        }
        for ( std::size_t i = 0; i < length-2; ++i )
            result.controls[index( result, line, i )] = i == start+1 ? merged
                : source.controls[index( source, line, i <= start ? i : i+2 )];
    }
    output = std::move( result );
    return true;
}

namespace {
struct Target {
    PatchInstance* instance = nullptr;
    std::vector<bool> mask;
    Target() {
        if ( Brush::m_type != eBrushTypeQuake3 && Brush::m_type != eBrushTypeQuake3BP && Brush::m_type != eBrushTypeQuake3Valve220 ) return;
        unsigned count = 0;
        Scene_forEachVisibleSelectedPatchInstance( [&]( PatchInstance& candidate ){ ++count; instance = &candidate; } );
        if ( count != 1 || instance->getPatch().m_patchDef3 ) { instance = nullptr; return; }
        const Patch& patch = instance->getPatch();
        if ( !Q3mapxPaintData( patch ).valid() ) { instance = nullptr; return; }
        const bool components = GlobalSelectionSystem().Mode() == SelectionSystem::eComponent;
        mask.resize( patch.getWidth()*patch.getHeight() );
        for ( std::size_t i = 0; i < mask.size(); ++i ) mask[i] = !components || instance->paintControlSelected( i );
    }
};

class Canvas final : public QWidget {
    Q3mapxPaintData m_data;
    Q3mapxPaintStroke m_stroke;
    std::vector<bool> m_mask;
    // Keeping the node alive prevents pointer reuse across a deleted/replaced map.
    std::unique_ptr<NodeSmartReference> m_node;
    QImage m_image;
    QRectF field() const { return QRectF( 22, 14, width()-44, height()-38 ); }
    void moveStroke( const QPointF& pos ) {
        const auto r = field();
        m_stroke.move( (pos.x()-r.left())/r.width(), (pos.y()-r.top())/r.height() );
        refreshImage();
    }
    void refreshImage() {
        const auto& data = m_stroke.active() ? m_stroke.result() : m_data;
        if ( !m_node ) { m_image = QImage(); update(); return; }
        m_image = QImage( 256, 256, QImage::Format_RGB32 );
        for ( int y = 0; y < 256; ++y ) {
            auto* row = reinterpret_cast<QRgb*>( m_image.scanLine( y ) );
            for ( int x = 0; x < 256; ++x ) {
                const auto c = data.sample( double(x)/255, double(y)/255 );
                const int bg = ((x/16+y/16)&1) ? 180 : 230;
                row[x] = qRgb( (c[0]*c[3]+bg*(255-c[3])+127)/255,
                               (c[1]*c[3]+bg*(255-c[3])+127)/255,
                               (c[2]*c[3]+bg*(255-c[3])+127)/255 );
            }
        }
        update();
    }
protected:
    void paintEvent( QPaintEvent* ) override {
        QPainter painter( this );
        painter.fillRect( rect(), palette().window() );
        if ( !m_node ) { painter.drawText( rect(), Qt::AlignCenter, "Select one Quake 3 patch to paint" ); return; }
        const auto r = field();
        painter.drawImage( r, m_image );
        painter.setRenderHint( QPainter::Antialiasing );
        const auto& data = m_stroke.active() ? m_stroke.result() : m_data;
        for ( std::size_t i = 0; i < data.controls.size(); ++i ) {
            const QPointF pos( r.left()+r.width()*(i%data.width)/(data.width-1), r.top()+r.height()*(i/data.width)/(data.height-1) );
            const auto c = data.controls[i].m_color;
            painter.setPen( QPen( m_mask[i] ? Qt::black : Qt::gray, m_mask[i] ? 2 : 1 ) );
            painter.setBrush( QColor( c[0], c[1], c[2] ) );
            painter.drawEllipse( pos, m_mask[i] ? 4 : 2, m_mask[i] ? 4 : 2 );
        }
        painter.setPen( palette().text().color() );
        painter.drawText( QRectF( 0, height()-22, width(), 20 ), Qt::AlignCenter, "u \u2192    Patch parameter space    v \u2193" );
    }
    void mousePressEvent( QMouseEvent* event ) override {
        if ( event->button() != Qt::LeftButton || !field().contains( event->localPos() ) ) return;
        refresh();
        if ( m_node && m_stroke.begin( m_data, m_mask, brush ) ) moveStroke( event->localPos() );
    }
    void mouseMoveEvent( QMouseEvent* event ) override {
        if ( m_stroke.active() ) moveStroke( event->localPos() );
    }
    void mouseReleaseEvent( QMouseEvent* event ) override {
        if ( event->button() != Qt::LeftButton || !m_stroke.active() ) return;
        moveStroke( event->localPos() );
        const auto after = m_stroke.result();
        m_stroke.end();
        commit( after );
    }
    void hideEvent( QHideEvent* ) override { m_stroke.end(); refreshImage(); }
    void focusOutEvent( QFocusEvent* event ) override {
        m_stroke.end(); refreshImage(); QWidget::focusOutEvent( event );
    }
    void keyPressEvent( QKeyEvent* event ) override {
        if ( event->key() == Qt::Key_Escape ) { m_stroke.end(); refreshImage(); event->accept(); }
        else QWidget::keyPressEvent( event );
    }
public:
    Q3mapxPaintBrush brush;
    explicit Canvas( QWidget* parent ) : QWidget( parent ) {
        setObjectName( "q3mapxPaintCanvas" );
        setMinimumSize( 320, 280 );
        setFocusPolicy( Qt::StrongFocus );
        setToolTip( "Drag to paint the patch control field. Escape cancels a stroke.\n"
                    "Vertex component selection masks controls. Preview shows raw RGBA over a checker, before shader and lighting effects." );
    }
    const Q3mapxPaintData& data() const { return m_data; }
    bool hasTarget() const { return bool( m_node ); }
    std::size_t maskedCount() const { return std::count( m_mask.begin(), m_mask.end(), true ); }
    void refresh() {
        Target target;
        if ( !target.instance ) {
            m_stroke.end(); m_node.reset(); m_mask.clear(); m_data = {}; refreshImage(); return;
        }
        scene::Node& node = target.instance->path().top().get();
        if ( m_node && &m_node->get() == &node && m_data.matches( target.instance->getPatch() ) && m_mask == target.mask ) return;
        m_stroke.end();
        m_node = std::make_unique<NodeSmartReference>( node );
        m_data = Q3mapxPaintData( target.instance->getPatch() );
        m_mask = std::move( target.mask );
        refreshImage();
    }
    bool commit( const Q3mapxPaintData& after ) {
        Target target;
        bool applied = false;
        if ( m_node && target.instance && &m_node->get() == &target.instance->path().top().get() && m_mask == target.mask )
            applied = Q3mapxPaint_apply( target.instance->getPatch(), m_data, after );
        refresh(); refreshImage();
        return applied;
    }
    void fill( bool reset ) {
        refresh();
        if ( !m_node ) return;
        auto after = m_data;
        bool any = false;
        for ( std::size_t i = 0; i < m_mask.size(); ++i ) if ( m_mask[i] ) {
            any = true;
            for ( int c = 0; c < 4; ++c ) if ( c == 3 ? brush.alpha : brush.rgb ) after.controls[i].m_color[c] = reset ? 255 : brush.color[c];
        }
        if ( !any || !( brush.rgb || brush.alpha ) ) return;
        if ( !reset ) {
            if ( brush.rgb ) after.mode = materialPaint;
            else if ( std::any_of( after.controls.begin(), after.controls.end(), []( const PatchControl& p ){ return p.m_color[3] != 255; } ) )
                after.mode = std::max( after.mode, int(alphaPaint) );
        }
        else if ( brush.rgb && std::all_of( after.controls.begin(), after.controls.end(), []( const PatchControl& p ){
            return p.m_color[0] == 255 && p.m_color[1] == 255 && p.m_color[2] == 255;
        } ) ) after.mode = alphaPaint;
        if ( reset && after.mode != materialPaint && std::all_of( after.controls.begin(), after.controls.end(), []( const PatchControl& p ){ return p.m_color[3] == 255; } ) ) after.mode = noPaint;
        commit( after );
    }
};

class PaintPanel final : public QWidget {
    Canvas* m_canvas;
    QLabel* m_status;
    QComboBox* m_quality;
    QPushButton *m_fill, *m_reset;
public:
    explicit PaintPanel( QWidget* parent ) : QWidget( parent ) {
        setObjectName( "q3mapxPaintPanel" );
        auto* layout = new QVBoxLayout( this );
        auto* intro = new QLabel( "Paint patch RGB and alpha\nRaw Bezier color preview; game shaders control the final appearance." );
        intro->setWordWrap( true ); layout->addWidget( intro );
        m_canvas = new Canvas( this ); layout->addWidget( m_canvas, 1 );
        auto* row = new QHBoxLayout;
        auto* rgb = new QCheckBox( "RGB (material color)" ); rgb->setObjectName( "q3mapxPaintRGB" );
        auto* alpha = new QCheckBox( "Alpha" ); alpha->setObjectName( "q3mapxPaintAlpha" ); alpha->setChecked( true );
        row->addWidget( rgb ); row->addWidget( alpha ); layout->addLayout( row );
        QObject::connect( rgb, &QCheckBox::toggled, [this]( bool b ){ m_canvas->brush.rgb = b; } );
        QObject::connect( alpha, &QCheckBox::toggled, [this]( bool b ){ m_canvas->brush.alpha = b; } );
        auto* form = new QFormLayout; layout->addLayout( form );
        auto* color = new QPushButton( "#ffffff" ); color->setObjectName( "q3mapxPaintColor" );
        form->addRow( "RGB", color );
        QObject::connect( color, &QPushButton::clicked, [this,color]{
            const auto& c = m_canvas->brush.color;
            const QColor picked = QColorDialog::getColor( QColor(c[0],c[1],c[2]), this, "Patch material RGB", QColorDialog::DontUseNativeDialog );
            if ( picked.isValid() ) {
                m_canvas->brush.color[0] = picked.red(); m_canvas->brush.color[1] = picked.green(); m_canvas->brush.color[2] = picked.blue();
                color->setText( picked.name() );
            }
        } );
        auto* opacity = new QSpinBox; opacity->setRange( 0,255 ); opacity->setValue( 255 ); opacity->setObjectName( "q3mapxPaintOpacity" );
        form->addRow( "Alpha value", opacity );
        QObject::connect( opacity, QOverload<int>::of(&QSpinBox::valueChanged), [this]( int n ){ m_canvas->brush.color[3] = n; } );
        const auto add = [&]( const char* label, const char* name, double min, double max, double value, double* target ){
            auto* spin = new QDoubleSpinBox; spin->setRange( min,max ); spin->setDecimals( 2 ); spin->setSingleStep( 0.05 ); spin->setValue( value ); spin->setObjectName( name );
            form->addRow( label, spin );
            QObject::connect( spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [target]( double n ){ *target = n; } );
        };
        add( "Radius (parameter units)", "q3mapxPaintRadius", 0.01, 2, 0.25, &m_canvas->brush.radius );
        add( "Strength per stroke", "q3mapxPaintStrength", 0.01, 1, 1, &m_canvas->brush.strength );
        add( "Falloff (0 = hard)", "q3mapxPaintFalloff", 0, 8, 1, &m_canvas->brush.falloff );
        m_quality = new QComboBox; m_quality->setObjectName( "q3mapxPaintQuality" );
        for ( int n : {1,2,4,8,16,32} ) m_quality->addItem( QString::number(n), n );
        form->addRow( "Segments per quadratic span", m_quality );
        QObject::connect( m_quality, QOverload<int>::of(&QComboBox::activated), [this]( int index ){
            auto after = m_canvas->data(); after.subdivisions = m_quality->itemData(index).toInt();
            m_canvas->commit( after ); refresh();
        } );
        row = new QHBoxLayout;
        m_fill = new QPushButton( "Fill enabled channels" ); m_fill->setObjectName( "q3mapxPaintFill" );
        m_reset = new QPushButton( "Reset enabled channels" ); m_reset->setObjectName( "q3mapxPaintReset" );
        row->addWidget( m_fill ); row->addWidget( m_reset ); layout->addLayout( row );
        QObject::connect( m_fill, &QPushButton::clicked, [this]{ m_canvas->fill( false ); refresh(); } );
        QObject::connect( m_reset, &QPushButton::clicked, [this]{ m_canvas->fill( true ); refresh(); } );
        m_status = new QLabel; m_status->setObjectName( "q3mapxPaintStatus" ); m_status->setWordWrap( true ); layout->addWidget( m_status );
        auto* timer = new QTimer( this );
        QObject::connect( timer, &QTimer::timeout, [this]{ if ( isVisible() ) refresh(); } ); timer->start( 200 );
        refresh();
    }
    void refresh() {
        m_canvas->refresh();
        const bool active = m_canvas->hasTarget();
        m_fill->setEnabled( active && m_canvas->maskedCount() != 0 );
        m_reset->setEnabled( m_fill->isEnabled() ); m_quality->setEnabled( active );
        if ( !active ) { m_status->setText( "Select exactly one Quake 3 patch. Vertex selection can mask the controls to paint." ); return; }
        const auto& data = m_canvas->data();
        m_quality->setCurrentIndex( m_quality->findData( data.subdivisions ) );
        auto* model = static_cast<QStandardItemModel*>( m_quality->model() );
        for ( int i = 0; i < m_quality->count(); ++i )
            model->item(i)->setEnabled( paintMeshFits( data.width, data.height, m_quality->itemData(i).toInt() ) );
        const int n = std::max(16,data.subdivisions);
        m_status->setText( QString( "%1 of %2 controls enabled. RGB: %3.\nUp to %4 render triangles before degenerate removal. Requires q3mapx." )
            .arg( m_canvas->maskedCount() ).arg( data.controls.size() ).arg( data.mode == materialPaint ? "material" : "baked lighting" )
            .arg( 2*((data.width-1)/2*n)*((data.height-1)/2*n) ) );
    }
};
QPointer<PaintPanel> panel;
QPointer<QDialog> dialog;
}

QWidget* Q3mapxPaint_createPanel( QWidget* parent ) { panel = new PaintPanel( parent ); return panel; }
void Q3mapxPaint_update() { if ( panel ) panel->refresh(); }
QWidget* Q3mapxPaint_createButton() {
    auto* button = new QPushButton( "q3mapx patch paint\u2026" );
    button->setObjectName( "q3mapxPaintOpen" );
    QObject::connect( button, &QPushButton::clicked, [button]{
        if ( !dialog ) {
            dialog = new QDialog( button->window() ); dialog->setWindowTitle( "q3mapx patch paint" );
            dialog->setAttribute( Qt::WA_DeleteOnClose );
            auto* layout = new QVBoxLayout( dialog ); layout->addWidget( Q3mapxPaint_createPanel( dialog ) );
            dialog->resize( 550,740 );
        }
        Q3mapxPaint_update(); dialog->show(); dialog->raise();
    } );
    return button;
}
