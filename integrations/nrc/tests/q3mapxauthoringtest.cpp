// SPDX-License-Identifier: GPL-3.0-or-later
// Optional test executable using the actual editor model, parser and widgets.
#define Q3MAPX_AUTHORING_TEST
static void Q3mapxAuthoringTestSetup();
static void Q3mapxAuthoringTestQtSetup();
static int Q3mapxAuthoringTestRun();
#include "main.cpp"
#include "brushnode.h"
#include "patch.h"
#include "patchmanip.h"
#include "surfacedialog.h"
#include "map.h"
#include "maplib.h"
#include "plugin.h"
#include "ientity.h"
#include "script/scripttokeniser.h"
#include "script/scripttokenwriter.h"
#include "stream/textfilestream.h"
#include "stream/memstream.h"
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSpinBox>
#include <QCheckBox>
#include <QFontDatabase>
#include <QPushButton>
#include <QLabel>
#include <memory>
#include <limits>

static QString outputDirectory;
static int checks = 0;

class TestDebugHandler final : public DefaultDebugMessageHandler {
    bool handleMessage() override { std::fflush( nullptr ); std::_Exit( 1 ); }
};
static TestDebugHandler testDebugHandler;

static void Q3mapxAuthoringTestQtSetup() {
    GlobalDebugMessageHandler::instance().setHandler( testDebugHandler );
    const QString font = QString::fromUtf8( qgetenv( "Q3MAPX_TEST_FONT" ) );
    if ( !font.isEmpty() ) QFontDatabase::addApplicationFont( font );
}

static void require( bool condition, const char* description ) {
    if ( !condition ) {
        fprintf( stderr, "AUTHORING CHECK FAILED: %s\n", description );
        fflush( stderr );
        std::_Exit( 1 );
    }
    ++checks;
}

static void Q3mapxAuthoringTestSetup() {
    outputDirectory = QString::fromUtf8( qgetenv( "Q3MAPX_TEST_OUTPUT" ) );
    require( QDir::isAbsolutePath( outputDirectory ) && QDir( outputDirectory ).exists(), "existing absolute test output directory" );
    QSettings::setDefaultFormat( QSettings::IniFormat );
    QSettings::setPath( QSettings::IniFormat, QSettings::UserScope, outputDirectory + "/qt-settings" );
    QSettings::setPath( QSettings::IniFormat, QSettings::SystemScope, outputDirectory + "/qt-settings" );
    qputenv( "QT_QPA_PLATFORM", "offscreen" );
}

static QByteArray input( const QString& filename ) {
    QFile file( outputDirectory+'/'+filename );
    require( file.open( QIODevice::ReadOnly ), "read generated native input" );
    return file.readAll();
}

static void output( const QString& filename, const char* contents ) {
    QFile file( outputDirectory+'/'+filename );
    require( file.open( QIODevice::WriteOnly|QIODevice::Truncate ), "open native result" );
    require( file.write( contents ) == qint64( std::strlen( contents ) ), "write complete native result" );
}

static bool importBrush( Brush& brush, const QByteArray& contents ) {
    BufferInputStream stream( contents.constData(), std::size_t( contents.size() ) );
    Tokeniser& reader = NewMapTokeniser( stream );
    reader.nextLine();
    bool good = Tokeniser_parseToken( reader, "{" );
    reader.nextLine();
    const char* kind = reader.getToken();
    if ( kind != nullptr && !string_equal( kind, q3mapx::authoring::brushDefinition ) && !string_equal( kind, "brushDef" ) ) reader.ungetToken();
    good = good && BrushTokenImporter( brush ).importTokens( reader );
    reader.release();
    return good;
}

static void brushChecks( const char* name, EBrushType format ) {
    GlobalBrushCreator().toggleFormat( format );
    NodeSmartReference node( GlobalBrushCreator().createBrush() );
    Brush& brush = *Node_getBrush( node );
    require( importBrush( brush, input( QString( "brush-" )+name+".txt" ) ), "read versioned brush" );
    require( brush.size()==6 && brush.back()->lightmapSampleSize()==8, "read face association" );
    brush.evaluateBRep();
    for ( const auto& face : brush ) require( face->contributes(), "all fixture faces contribute" );
    Face& face = *brush.back();
    UndoMemento* before = face.exportState();
    face.setLightmapSampleSize( 32 );
    UndoMemento* after = face.exportState();
    face.importState( before );
    require( face.lightmapSampleSize()==8, "face undo memento" );
    face.importState( after );
    require( face.lightmapSampleSize()==32, "face redo memento" );
    before->release(); after->release();
    face.setLightmapSampleSize( 8 );
    NodeSmartReference copy( NodeTypeCast<scene::Cloneable>::cast( node )->clone() );
    require( Node_getBrush( copy )->back()->lightmapSampleSize()==8, "duplicate retains face override" );
    Node_getBrush( copy )->back()->setLightmapSampleSize( 4 );
    require( face.lightmapSampleSize()==8, "duplicate is independent" );
    StringOutputStream text;
    { SimpleTokenWriter writer( text ); BrushTokenExporter( brush ).exportTokens( writer ); }
    output( QString( "roundtrip-" )+name+".txt", text.c_str() );
    NodeSmartReference reopened( GlobalBrushCreator().createBrush() );
    require( importBrush( *Node_getBrush( reopened ), QByteArray( text.c_str() ) ), "reopen emitted brush" );
    require( Node_getBrush( reopened )->back()->lightmapSampleSize()==8, "save/reopen retains face override" );
    NodeSmartReference xml( GlobalBrushCreator().createBrush() );
    BrushXMLImporter xmlReader( *Node_getBrush( xml ) );
    BrushXMLExporter( brush ).exportXML( xmlReader );
    require( Node_getBrush( xml )->back()->lightmapSampleSize()==8, "XML retains face override" );
    Node_getBrush( xml )->evaluateBRep();
    StringOutputStream xmlText;
    { SimpleTokenWriter writer( xmlText ); BrushTokenExporter( *Node_getBrush( xml ) ).exportTokens( writer ); }
    require( string_equal( text.c_str(), xmlText.c_str() ), "XML retains brush geometry and texture projection" );
    face.setLightmapSampleSize( 0 );
    StringOutputStream legacy;
    { SimpleTokenWriter writer( legacy ); BrushTokenExporter( brush ).exportTokens( writer ); }
    require( std::strstr( legacy.c_str(), "q3mapxBrushDef" )==nullptr, "reset writes legacy brush format" );
    NodeSmartReference legacyNode( GlobalBrushCreator().createBrush() );
    require( importBrush( *Node_getBrush( legacyNode ), QByteArray( legacy.c_str() ) ), "legacy brush import remains supported" );
    for ( const char* value : { "-1", "1.5", "16385" } ) {
        NodeSmartReference bad( GlobalBrushCreator().createBrush() );
        auto invalid = input( QString( "brush-" )+name+".txt" );
        invalid.replace( "lightmapSampleSize 8", QByteArray( "lightmapSampleSize " )+value );
        require( !importBrush( *Node_getBrush( bad ), invalid ), "native parser rejects invalid density" );
    }
}

static void patchChecks() {
    NodeSmartReference node( g_patchCreator->createPatch() );
    Patch& patch = *Node_getPatch( node );
    const auto contents = input( "patch.txt" );
    BufferInputStream stream( contents.constData(), std::size_t( contents.size() ) );
    Tokeniser& reader = NewMapTokeniser( stream );
    reader.nextLine();
    require( Tokeniser_parseToken( reader, "{" ), "patch outer brace" );
    require( PatchTokenImporter( patch ).importTokens( reader ), "read versioned patch" );
    reader.release();
    require( patch.lightmapSampleSize()==12 && patch.getWidth()==3 && patch.getHeight()==3, "patch association/dimensions" );
    UndoMemento* before = patch.exportState();
    patch.setLightmapSampleSize( 4 );
    UndoMemento* after = patch.exportState();
    patch.importState( before ); require( patch.lightmapSampleSize()==12, "patch undo memento" );
    patch.importState( after ); require( patch.lightmapSampleSize()==4, "patch redo memento" );
    before->release(); after->release();
    patch.setLightmapSampleSize( 12 );
    NodeSmartReference copy( NodeTypeCast<scene::Cloneable>::cast( node )->clone() );
    require( Node_getPatch( copy )->lightmapSampleSize()==12, "duplicate retains patch override" );
    StringOutputStream text;
    { SimpleTokenWriter writer( text ); PatchTokenExporter( patch ).exportTokens( writer ); }
    output( "roundtrip-patch.txt", text.c_str() );
    NodeSmartReference xml( g_patchCreator->createPatch() );
    patch.exportXML( *Node_getPatch( xml ) );
    require( Node_getPatch( xml )->lightmapSampleSize()==12, "XML retains patch override" );
    StringOutputStream xmlText;
    { SimpleTokenWriter writer( xmlText ); PatchTokenExporter( *Node_getPatch( xml ) ).exportTokens( writer ); }
    require( string_equal( text.c_str(), xmlText.c_str() ), "XML retains patch controls and UVs" );
    Q3mapxLightmapGrid grid;
    grid.update( patch.lightmapPreviewMesh(), 12 );
    require( grid.area()>128*128 && !grid.lines().empty() && !grid.limited(), "curved native patch grid" );
    patch.setLightmapSampleSize( 0 );
    StringOutputStream legacy;
    { SimpleTokenWriter writer( legacy ); PatchTokenExporter( patch ).exportTokens( writer ); }
    require( std::strstr( legacy.c_str(), "q3mapxPatchDef" )==nullptr, "reset writes legacy patch format" );
}

static void graphChecks( const char* name, EBrushType format ) {
    MapFormat* module = Radiant_getMapModules().findModule( "mapq3" );
    require( module != nullptr, "native mapq3 module loaded" );
    NodeSmartReference root( NewMapRoot( "density" ) );
    const auto contents = input( QString( "map-" )+name+".map" );
    BufferInputStream stream( contents.constData(), std::size_t( contents.size() ) );
    module->readGraph( root, stream, GlobalEntityCreator() );
    require( GlobalBrushCreator().getFormat()==format, "mapq3 projection detection" );
    class Count final : public scene::Traversable::Walker {
    public:
        mutable int faces = 0, patches = 0;
        bool pre( scene::Node& node ) const override {
            if ( auto* brush = Node_getBrush( node ) ) {
                for ( const auto& face : *brush ) if ( face->lightmapSampleSize()==8 ) ++faces;
            }
            if ( auto* patch = Node_getPatch( node ) ) if ( patch->lightmapSampleSize()==12 ) ++patches;
            return true;
        }
    } count;
    Node_getTraversable( root )->traverse( count );
    require( count.faces==1 && count.patches==1, "mapq3 preserves brush and patch associations" );
    StringOutputStream text;
    module->writeGraph( root, Map_Traverse, text );
    output( QString( "native-" )+name+".map", text.c_str() );
    NodeSmartReference reopened( NewMapRoot( "density" ) );
    BufferInputStream saved( text.c_str(), std::strlen( text.c_str() ) );
    module->readGraph( reopened, saved, GlobalEntityCreator() );
    StringOutputStream textAgain;
    module->writeGraph( reopened, Map_Traverse, textAgain );
    require( string_equal( text.c_str(), textAgain.c_str() ), "full map round trip is stable" );
}

static void selectionChecks( QWidget& controls ) {
    GlobalBrushCreator().toggleFormat( eBrushTypeQuake3 );
    NodeSmartReference node( GlobalBrushCreator().createBrush() );
    Brush& brush = *Node_getBrush( node );
    require( importBrush( brush, input( "brush-quake.txt" ) ), "selection fixture" );
    brush.evaluateBRep();
    FaceInstance first( **brush.begin(), SelectionChangeCallback() );
    FaceInstance last( *brush.back(), SelectionChangeCallback() );
    GlobalSelectionSystem().SetMode( SelectionSystem::eComponent );
    first.setSelected( SelectionSystem::eFace, true );
    last.setSelected( SelectionSystem::eFace, true );
    Q3mapxLightmaps_update();
    auto* status = controls.findChild<QLabel*>( "q3mapxDensityStatus" );
    auto* value = controls.findChild<QSpinBox*>( "q3mapxTexelSize" );
    auto* apply = controls.findChild<QPushButton*>( "q3mapxDensityApply" );
    auto* reset = controls.findChild<QPushButton*>( "q3mapxDensityReset" );
    require( status && status->text().contains( "Mixed spacing" ), "mixed selected face values" );
    require( apply && reset && value, "native apply/reset actions" );
    value->setValue( 32 );
    apply->click(); // invoke the action directly; no mouse or keyboard events
    require( ( *brush.begin() )->lightmapSampleSize()==32 && brush.back()->lightmapSampleSize()==32, "apply changes selected faces" );
    require( ( *std::next( brush.begin() ) )->lightmapSampleSize()==0, "unselected face unchanged" );
    require( status->text().contains( "Requested spacing: 32" ), "selection status follows action" );
    reset->click();
    require( ( *brush.begin() )->lightmapSampleSize()==0 && brush.back()->lightmapSampleSize()==0, "inherit resets selected faces" );
    first.setSelected( SelectionSystem::eFace, false );
    last.setSelected( SelectionSystem::eFace, false );
    GlobalSelectionSystem().SetMode( SelectionSystem::ePrimitive );
    Q3mapxLightmaps_update();
}

static int Q3mapxAuthoringTestRun() {
    SurfaceInspector_constructWindow( nullptr );
    GlobalEntityCreator().setKeyValueChangedFunc( +[]{} );
    brushChecks( "quake", eBrushTypeQuake3 );
    brushChecks( "bp", eBrushTypeQuake3BP );
    brushChecks( "valve", eBrushTypeQuake3Valve220 );
    patchChecks();
    graphChecks( "quake", eBrushTypeQuake3 );
    graphChecks( "bp", eBrushTypeQuake3BP );
    graphChecks( "valve", eBrushTypeQuake3Valve220 );
    const std::vector<DoubleVector3> triangles = { {0,0,0}, {64,0,0}, {64,64,0}, {0,0,0}, {64,64,0}, {0,64,0} };
    Q3mapxLightmapGrid grid;
    grid.update( triangles, 8 );
    require( grid.area()==4096 && !grid.limited(), "analytic planar area" );
    const auto fine = grid.lines();
    require( !fine.empty(), "fine grid present" );
    for ( const auto& point : fine ) require( point[0]>=0 && point[0]<=64 && point[1]>=0 && point[1]<=64, "grid clipped to polygon" );
    grid.update( triangles, 8 ); require( grid.lines()==fine, "unchanged preview is stable" );
    grid.update( triangles, 32 ); require( grid.lines().size()<fine.size(), "density change updates grid" );
    class Submission final : public Renderer {
    public:
        int depth = 0, submissions = 0;
        void PushState() override { ++depth; }
        void PopState() override { --depth; }
        void SetState( Shader* state, EStyle ) override { require( state != nullptr, "preview shader available" ); }
        EStyle getStyle() const override { return eFullMaterials; }
        void Highlight( EHighlightMode, bool enable ) override { require( !enable, "preview suppresses selection tint" ); }
        void addRenderable( const OpenGLRenderable&, const Matrix4& ) override { require( depth==1, "preview isolates render state" ); ++submissions; }
    } renderer;
    grid.submit( renderer, g_matrix4_identity );
    require( renderer.submissions==1 && renderer.depth==0, "preview submits and restores state" );
    grid.update( triangles, 0 ); require( grid.lines().empty(), "inherit has no estimated grid" );
    grid.update( { {0,0,0}, {65536,0,0}, {65536,65536,0} }, 1 );
    require( grid.limited() && grid.lines().empty(), "dense preview is bounded" );
    grid.update( { {0,0,0}, {1,0,0}, {1,std::numeric_limits<double>::infinity(),0} }, 8 );
    require( grid.limited() && grid.lines().empty(), "nonfinite preview is rejected" );
    grid.update( { {0,0,0}, {65536,0,0}, {65536,1e-12,0},
                   {0,0,0}, {65536,0,0}, {65536,1e-12,0},
                   {0,0,0}, {65536,0,0}, {65536,1e-12,0},
                   {0,0,0}, {65536,0,0}, {65536,1e-12,0} }, 1 );
    require( grid.limited() && grid.lines().empty(), "near-degenerate preview work is bounded even without emitted lines" );
    std::unique_ptr<QWidget> controls( Q3mapxLightmaps_create() );
    selectionChecks( *controls );
    auto* value = controls->findChild<QSpinBox*>( "q3mapxTexelSize" );
    auto* preview = controls->findChild<QCheckBox*>( "q3mapxDensityPreview" );
    require( value!=nullptr && value->minimum()==0 && value->maximum()==16384 && value->value()==0, "native density controls" );
    require( preview!=nullptr, "native preview toggle" );
    preview->setChecked( true ); require( Q3mapxLightmaps_previewEnabled(), "preview toggle state" );
    preview->setChecked( false ); require( !Q3mapxLightmaps_previewEnabled(), "preview reset state" );
    controls->resize( 540, controls->sizeHint().height() );
    QImage image( controls->size(), QImage::Format_ARGB32_Premultiplied );
    image.fill( Qt::transparent );
    controls->render( &image ); // paints our own widget; no OS capture or input
    require( image.save( outputDirectory+"/density-controls.png" ), "native widget render" );
    QJsonObject result { {"checks",checks}, {"editor_model_roundtrip",true}, {"os_input_control",false}, {"viewport_raster_tested",false} };
    output( "results.json", QJsonDocument( result ).toJson().constData() );
    controls.reset();
    SurfaceInspector_destroyWindow();
    fprintf( stdout, "q3mapx Radiant authoring: %d checks passed\n", checks );
    return 0;
}
