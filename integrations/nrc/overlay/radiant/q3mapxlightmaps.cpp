// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapxlightmaps.h"
#include "brush.h"
#include "patch.h"
#include "iundo.h"
#include "qgl.h"
#include "scenelib.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
bool preview = false;
constexpr std::size_t maxPreviewTriangles = 262144;
constexpr std::size_t maxPreviewCells = 200000;
QPointer<QGroupBox> densityGroup;
QPointer<QSpinBox> densityValue;
QPointer<QLabel> densityStatus;

struct Selection {
    std::vector<Face*> faces;
    std::vector<Patch*> patches;
    bool supported = Brush::m_type == eBrushTypeQuake3
                  || Brush::m_type == eBrushTypeQuake3BP
                  || Brush::m_type == eBrushTypeQuake3Valve220;
    Selection() {
        if ( GlobalSelectionSystem().Mode() == SelectionSystem::eComponent ) {
            Scene_ForEachSelectedBrushFace( GlobalSceneGraph(), [this]( Face& face ){ faces.push_back( &face ); } );
        }
        else {
            Scene_ForEachSelectedBrush_ForEachFace( GlobalSceneGraph(), [this]( Face& face ){
                if ( face.contributes() ) faces.push_back( &face );
            } );
            Scene_forEachVisibleSelectedPatch( [this]( Patch& patch ){
                patches.push_back( &patch );
                supported = supported && !patch.m_patchDef3;
            } );
        }
    }
    std::size_t count() const { return faces.size() + patches.size(); }
    template<typename F> void each( const F& callback ) const {
        for ( Face* face : faces ) callback( *face );
        for ( Patch* patch : patches ) callback( *patch );
    }
};

void applySampleSize( int value ) {
    const Selection selection;
    if ( !selection.supported || selection.count() == 0 ) return;
    UndoableCommand command( "q3mapx lightmap density" );
    selection.each( [value]( auto& surface ){ surface.setLightmapSampleSize( value ); } );
    Q3mapxLightmaps_update();
    SceneChangeNotify();
}
}

bool Q3mapxLightmaps_previewEnabled() { return preview; }

QWidget* Q3mapxLightmaps_create() {
    densityGroup = new QGroupBox( "q3mapx lightmap density" );
    densityGroup->setObjectName( "q3mapxDensity" );
    auto* form = new QFormLayout( densityGroup );
    densityValue = new QSpinBox;
    densityValue->setObjectName( "q3mapxTexelSize" );
    densityValue->setRange( 0, q3mapx::authoring::maxSampleSize );
    densityValue->setSpecialValueText( "Inherit" );
    densityValue->setToolTip( "World units per lightmap texel. Smaller values add detail and cost.\n"
                             "An override replaces shader/entity spacing and scaling; the compiler minimum still applies.\n"
                             "0 restores inheritance. MAPs with overrides require this editor integration and q3mapx." );
    auto* row = new QHBoxLayout;
    row->addWidget( densityValue );
    auto* apply = new QPushButton( "Apply" );
    apply->setObjectName( "q3mapxDensityApply" );
    QObject::connect( apply, &QPushButton::clicked, []{ applySampleSize( densityValue->value() ); } );
    row->addWidget( apply );
    auto* reset = new QPushButton( "Inherit" );
    reset->setObjectName( "q3mapxDensityReset" );
    QObject::connect( reset, &QPushButton::clicked, []{ applySampleSize( 0 ); } );
    row->addWidget( reset );
    form->addRow( "Units / texel", row );
    auto* showGrid = new QCheckBox( "Preview requested spacing in the camera viewport" );
    showGrid->setObjectName( "q3mapxDensityPreview" );
    showGrid->setChecked( preview );
    QObject::connect( showGrid, &QCheckBox::toggled, []( bool enabled ){
        preview = enabled;
        SceneChangeNotify();
    } );
    form->addRow( showGrid );
    densityStatus = new QLabel;
    densityStatus->setWordWrap( true );
    densityStatus->setObjectName( "q3mapxDensityStatus" );
    form->addRow( densityStatus );
    auto* help = new QLabel( "Cyan grids show explicit overrides on faces and curved patches. "
        "They estimate world-space spacing, not baked atlas placement. "
        "Material rules, chart projection, minimum spacing and packing can change the bake; "
        "inherited surfaces have no grid." );
    help->setWordWrap( true );
    form->addRow( help );
    Q3mapxLightmaps_update();
    return densityGroup;
}

void Q3mapxLightmaps_update() {
    if ( !densityGroup ) return;
    const Selection selection;
    densityGroup->setEnabled( selection.supported );
    if ( !selection.supported ) {
        densityStatus->setText( "Available for Quake III MAP surfaces and patchDef2 patches." );
        return;
    }
    int first = -1;
    bool mixed = false;
    selection.each( [&]( const auto& surface ){
        if ( first < 0 ) first = surface.lightmapSampleSize();
        else mixed = mixed || first != surface.lightmapSampleSize();
    } );
    densityValue->setValue( mixed || first < 0 ? 0 : first );
    QString status = QString( "%1 faces, %2 patches selected. " ).arg( selection.faces.size() ).arg( selection.patches.size() );
    status += selection.count() == 0 ? "Select faces, brushes or patches to edit."
            : mixed ? "Mixed spacing. Apply assigns one value to the selection."
            : first == 0 ? "Inheriting compiler, entity and shader settings."
            : QString( "Requested spacing: %1 units / texel." ).arg( first );
    double estimate = 0;
    bool limited = false;
    Q3mapxLightmapGrid grid;
    for ( const Face* face : selection.faces ) {
        if ( face->lightmapSampleSize() > 0 ) {
            grid.update( face->getWinding(), face->lightmapSampleSize() );
            estimate += grid.area() / ( double( face->lightmapSampleSize() ) * face->lightmapSampleSize() );
            limited = limited || grid.limited();
        }
    }
    for ( const Patch* patch : selection.patches ) {
        if ( patch->lightmapSampleSize() > 0 ) {
            grid.update( patch->lightmapPreviewMesh(), patch->lightmapSampleSize() );
            estimate += grid.area() / ( double( patch->lightmapSampleSize() ) * patch->lightmapSampleSize() );
            limited = limited || grid.limited();
        }
    }
    if ( estimate > 0 ) status += QString( " Approx. %1 texels by surface area; excludes padding, styles and supersampling." ).arg( std::ceil( estimate ), 0, 'f', 0 );
    if ( limited ) status += " Dense/invalid preview geometry exceeds the grid limit; its grid is hidden.";
    densityStatus->setText( status );
}

Q3mapxLightmapGrid::~Q3mapxLightmapGrid() {
    if ( m_state != nullptr ) GlobalShaderCache().release( "$Q3MAPX_DENSITY" );
}

void Q3mapxLightmapGrid::update( const Winding& winding, int sampleSize ) {
    std::vector<DoubleVector3> triangles;
    for ( std::size_t i = 1; i + 1 < winding.numpoints; ++i ) {
        triangles.insert( triangles.end(), { winding[0].vertex, winding[i].vertex, winding[i+1].vertex } );
    }
    update( std::move( triangles ), sampleSize );
}

void Q3mapxLightmapGrid::update( const PatchTesselation& tess, int sampleSize ) {
    std::vector<DoubleVector3> triangles;
    for ( std::size_t strip = 0; strip < tess.m_numStrips; ++strip ) {
        const auto* indices = tess.m_indices.data() + strip * tess.m_lenStrips;
        for ( std::size_t i = 0; i + 3 < tess.m_lenStrips; i += 2 ) {
            if ( triangles.size() > maxPreviewTriangles * 3 ) {
                update( std::move( triangles ), sampleSize );
                return;
            }
            for ( std::size_t index : { i, i+1, i+2, i+2, i+1, i+3 } ) {
                triangles.emplace_back( vertex3f_to_vector3( tess.m_vertices[indices[index]].vertex ) );
            }
        }
    }
    update( std::move( triangles ), sampleSize );
}

void Q3mapxLightmapGrid::update( std::vector<DoubleVector3> triangles, int sampleSize ) {
    if ( m_sampleSize == sampleSize && m_triangles == triangles ) return;
    m_sampleSize = sampleSize;
    m_triangles = std::move( triangles );
    m_lines.clear();
    m_limited = false;
    m_area = 0;
    if ( sampleSize <= 0 || sampleSize > q3mapx::authoring::maxSampleSize ) return;
    if ( m_triangles.size() % 3 != 0 || m_triangles.size() > maxPreviewTriangles * 3 ) { m_limited = true; return; }
    DoubleVector3 totalNormal( 0 );
    for ( const auto& point : m_triangles ) {
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( !std::isfinite( point[axis] ) || std::fabs( point[axis] ) > 65536 ) { m_limited = true; return; }
        }
    }
    for ( std::size_t i = 0; i < m_triangles.size(); i += 3 ) {
        const auto normal = vector3_cross( m_triangles[i+1] - m_triangles[i], m_triangles[i+2] - m_triangles[i] );
        totalNormal += normal;
        m_area += vector3_length( normal ) * 0.5;
    }
    int major = 0;
    std::size_t cells = 0;
    for ( int axis = 1; axis < 3; ++axis ) if ( std::fabs( totalNormal[axis] ) > std::fabs( totalNormal[major] ) ) major = axis;
    for ( std::size_t i = 0; i < m_triangles.size(); i += 3 ) {
        const DoubleVector3* triangle = &m_triangles[i];
        auto normal = vector3_cross( triangle[1] - triangle[0], triangle[2] - triangle[0] );
        const double length = vector3_length( normal );
        if ( length == 0 ) continue;
        normal *= 0.08 / length; // avoid coincident fragments on the editor surface
        for ( int axis = 0; axis < 3; ++axis ) {
            if ( axis == major ) continue;
            const double lo = std::min( { triangle[0][axis], triangle[1][axis], triangle[2][axis] } );
            const double hi = std::max( { triangle[0][axis], triangle[1][axis], triangle[2][axis] } );
            for ( int cell = int( std::ceil( lo / sampleSize ) ); cell <= int( std::floor( hi / sampleSize ) ); ++cell ) {
                if ( ++cells > maxPreviewCells ) { m_lines.clear(); m_limited = true; return; }
                const double position = double( cell ) * sampleSize;
                std::vector<DoubleVector3> points;
                auto add = [&]( const DoubleVector3& point ){
                    if ( std::none_of( points.begin(), points.end(), [&]( const auto& p ){ return vector3_length_squared( p - point ) < 1e-16; } ) ) points.push_back( point );
                };
                for ( int edge = 0; edge < 3; ++edge ) {
                    const auto& a = triangle[edge];
                    const auto& b = triangle[(edge+1)%3];
                    if ( a[axis] == position ) add( a );
                    if ( ( a[axis] < position && b[axis] > position ) || ( a[axis] > position && b[axis] < position ) ) {
                        add( a + ( b-a ) * ( ( position-a[axis] ) / ( b[axis]-a[axis] ) ) );
                    }
                }
                if ( points.size() == 2 ) {
                    if ( m_lines.size() >= 40000 ) { m_lines.clear(); m_limited = true; return; }
                    m_lines.emplace_back( points[0] + normal );
                    m_lines.emplace_back( points[1] + normal );
                }
            }
        }
    }
}

void Q3mapxLightmapGrid::render( RenderStateFlags ) const {
    if ( m_lines.empty() ) return;
    gl().glVertexPointer( 3, GL_FLOAT, sizeof( Vector3 ), m_lines.data() );
    gl().glDrawArrays( GL_LINES, 0, GLsizei( m_lines.size() ) );
}

void Q3mapxLightmapGrid::submit( Renderer& renderer, const Matrix4& localToWorld ) const {
    if ( m_lines.empty() ) return;
    if ( m_state == nullptr ) m_state = GlobalShaderCache().capture( "$Q3MAPX_DENSITY" );
    renderer.PushState();
    renderer.Highlight( Renderer::EHighlightMode( Renderer::eFace | Renderer::ePrimitive | Renderer::ePrimitiveWire | Renderer::eFaceWire ), false );
    renderer.SetState( m_state, Renderer::eFullMaterials );
    renderer.SetState( m_state, Renderer::eWireframeOnly );
    renderer.addRenderable( *this, localToWorld );
    renderer.PopState();
}
