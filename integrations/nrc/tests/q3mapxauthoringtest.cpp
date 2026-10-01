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
#include "entitylist.h"
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
        mutable int faces = 0, patches = 0, painted = 0;
        bool pre( scene::Node& node ) const override {
            if ( auto* brush = Node_getBrush( node ) ) {
                for ( const auto& face : *brush ) if ( face->lightmapSampleSize()==8 ) ++faces;
            }
            if ( auto* patch = Node_getPatch( node ) ) if ( patch->lightmapSampleSize()==12 ) ++patches;
            if ( auto* patch = Node_getPatch( node ) ) if ( patch->paintMode()==2 ) ++painted;
            return true;
        }
    } count;
    Node_getTraversable( root )->traverse( count );
    require( count.faces==1 && count.patches==1 && count.painted==1, "mapq3 preserves brush, patch and paint associations" );
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

static bool importPaint( Patch& patch, const QByteArray& contents ) {
    BufferInputStream stream( contents.constData(), std::size_t( contents.size() ) );
    Tokeniser& reader = NewMapTokeniser( stream );
    reader.nextLine();
    bool good = Tokeniser_parseToken( reader, "{" ) && PatchTokenImporter( patch ).importTokens( reader );
    reader.release();
    return good;
}

static void paintChecks() {
    GlobalBrushCreator().toggleFormat( eBrushTypeQuake3 );
    NodeSmartReference node( g_patchCreator->createPatch() );
    Patch& patch = *Node_getPatch( node );
    const auto contents = input( "paint.txt" );
    require( importPaint( patch, contents ), "paint MAP parser" );
    Q3mapxPaintData original( patch );
    require( original.valid() && original.mode == 2 && original.subdivisions == 8 && patch.lightmapSampleSize() == 8, "paint metadata" );
    require( original.sample(0.5,0.5) == std::array<unsigned char,4>{138,125,64,191}, "analytic tensor paint sample" );
    require( original.sample(0,0) == original.controls[0].m_color && original.sample(1,1) == original.controls[8].m_color, "paint endpoints" );
    NodeSmartReference copy( NodeTypeCast<scene::Cloneable>::cast( node )->clone() );
    require( original.matches( *Node_getPatch(copy) ), "duplicate retains paint and geometry" );
    Node_getPatch(copy)->ctrlAt(0,0).m_color[0] = 0;
    require( original.matches(patch), "duplicate paint is independent" );
    StringOutputStream text;
    { SimpleTokenWriter writer(text); PatchTokenExporter(patch).exportTokens(writer); }
    output("roundtrip-paint.txt",text.c_str());
    NodeSmartReference reopen(g_patchCreator->createPatch());
    require(importPaint(*Node_getPatch(reopen),QByteArray(text.c_str())) && original.matches(*Node_getPatch(reopen)),"paint save/reopen");
    NodeSmartReference xml(g_patchCreator->createPatch());
    patch.exportXML(*Node_getPatch(xml));
    require(original.matches(*Node_getPatch(xml)),"paint XML transfer preserves geometry/color/metadata");
    require(!Q3mapxPaint_importXML(*Node_getPatch(xml),"material 8 1 2 3 999") && original.matches(*Node_getPatch(xml)),"invalid XML paint is atomic");
    NodeSmartReference alphaNode(g_patchCreator->createPatch());
    require(importPaint(*Node_getPatch(alphaNode),input("paint-alpha.txt")),"lighting-mode native paint import");
    Q3mapxPaintData alphaData(*Node_getPatch(alphaNode));
    require(alphaData.mode==1 && alphaData.sample(0.5,0.5)==std::array<unsigned char,4>{255,255,255,191},"lighting-mode alpha field");
    StringOutputStream alphaText;
    { SimpleTokenWriter writer(alphaText); PatchTokenExporter(*Node_getPatch(alphaNode)).exportTokens(writer); }
    output("roundtrip-paint-alpha.txt",alphaText.c_str());
    for(int segments: {1,2,4,8,16,32}) {
        require(patch.setPaintSettings(2,segments),"supported paint quality setting");
        StringOutputStream encoded;
        {SimpleTokenWriter writer(encoded); PatchTokenExporter(patch).exportTokens(writer);}
        NodeSmartReference quality(g_patchCreator->createPatch());
        require(importPaint(*Node_getPatch(quality),QByteArray(encoded.c_str())) && Node_getPatch(quality)->paintSubdivisions()==segments,"quality round trip");
    }
    patch.setPaintSettings(2,8);
    NodeSmartReference large(g_patchCreator->createPatch()); Node_getPatch(large)->setDims(31,31);
    require(!Node_getPatch(large)->setPaintSettings(2,32) && Node_getPatch(large)->paintMode()==0,"reject over-budget paint grid without mutation");
    const std::pair<QByteArray,QByteArray> invalid[] = {
        {"vertexRGB material","vertexRGB unknown"}, {"vertexRGB material","vertexRGB lighting"},
        {"paintSubdivisions 8","paintSubdivisions 3"}, {"paintSubdivisions 8","paintSubdivisions -8"},
        {"( 3 3 0 0 0 )","( 4 3 0 0 0 )"}, {"( 3 3 0 0 0 )","( 3 32 0 0 0 )"},
        {"10 20 0 255","256 20 0 255"}, {"10 20 0 255","10 20 0 -1"},
        {"10 20 0 255","10.5 20 0 255"}, {"10 20 0 255","1e1 20 0 255"},
        {"lightmapSampleSize 8","lightmapSampleSize 8.0"},
        {"-224.0","nan"}, {"-224.0","1e1000"},
        {"10 20 0 255","0010 20 0 255"}
    };
    for (const auto& replacement: invalid) {
        auto bad = contents;
        require(bad.contains(replacement.first),"negative fixture has target token");
        bad.replace(replacement.first,replacement.second);
        NodeSmartReference rejected(g_patchCreator->createPatch());
        require(!importPaint(*Node_getPatch(rejected),bad),"reject malformed paint MAP");
    }
    // Stroke coverage is evaluated over line segments, independent of event rate.
    Q3mapxPaintBrush brush;
    brush.color={4,8,16,0}; brush.rgb=true; brush.alpha=false; brush.radius=0.3; brush.strength=0.5;
    Q3mapxPaintStroke sparse,dense;
    const std::vector<bool> all(9,true);
    require(sparse.begin(original,all,brush) && dense.begin(original,all,brush),"start RGB strokes");
    sparse.move(0,0.5); sparse.move(1,0.5);
    for(int i=0;i<=100;++i) dense.move(i/100.0,0.5);
    for(std::size_t i=0;i<9;++i) {
        require(sparse.result().controls[i].m_color==dense.result().controls[i].m_color,"stroke event-rate invariance");
        require(sparse.result().controls[i].m_color[3]==original.controls[i].m_color[3],"RGB stroke preserves alpha");
    }
    require(sparse.result().controls[4].m_color==std::array<unsigned char,4>{127,104,136,0},"center brush strength rounds once");
    brush.rgb=false; brush.alpha=true;
    brush.color[3]=255;
    std::vector<bool> mask(9,false); mask[4]=true;
    Q3mapxPaintStroke masked;
    require(masked.begin(original,mask,brush),"masked alpha brush"); masked.move(0.5,0.5);
    for(std::size_t i=0;i<9;++i) if(i!=4) require(original.controls[i].m_color==masked.result().controls[i].m_color,"mask protects unselected controls");
    brush.radius=std::numeric_limits<double>::quiet_NaN();
    require(!masked.begin(original,mask,brush),"reject invalid brush bounds");
    // Row operations must preserve existing associations or refuse lossy edits.
    UndoMemento* saved=patch.exportState();
    patch.TransposeMatrix(); patch.TransposeMatrix();
    patch.InvertMatrix(); patch.InvertMatrix();
    require(original.matches(patch),"transpose/invert preserve paint association");
    patch.InsertRemove(true,true,true);
    Q3mapxPaintData inserted(patch);
    require(inserted.width==5 && inserted.mode==2,"painted row insertion retains mode");
    for(int y=0;y<=8;++y) for(int x=0;x<=8;++x) {
        auto before=original.sample(x/8.0,y/8.0), after=inserted.sample(x/8.0,y/8.0);
        for(int c=0;c<4;++c) require(std::abs(int(before[c])-int(after[c]))<=1,"subdivision paint field within byte quantization");
    }
    patch.InsertRemove(false,true,true);
    require(inserted.matches(patch),"lossy painted row removal is refused");
    patch.importState(saved); saved->release();
    require(original.matches(patch),"topology undo restores paint");
    NodeSmartReference thick(g_patchCreator->createPatch()); bool no12=false,no34=false;
    Node_getPatch(thick)->createThickenedOpposite(patch,4,2,no12,no34);
    for(std::size_t i=0;i<9;++i) require(Node_getPatch(thick)->begin()[i].m_color==original.controls[i].m_color,"thickened opposite retains color");
    require(Node_getPatch(thick)->paintMode()==2 && Node_getPatch(thick)->lightmapSampleSize()==8,"thickening metadata");
    // Wall generation calls upstream NaturalTexture(), which requires a realised
    // GL shader. Its color transport remains outside this no-GL harness.

    // Use native instances and the real undo stack. Invoke model/widget actions
    // directly: this sends no mouse/keyboard events and needs no GL context.
    NodeSmartReference root(NewMapRoot("paint"));
    globalOutputStream()<<"Paint checks: attach native instance\n";
    Node_getTraversable(root)->insert(node);
    GlobalSceneGraph().insert_root(root);
    scene::Path path(makeReference(root.get())); path.push(makeReference(node.get()));
    auto* instance=Instance_getPatch(*GlobalSceneGraph().find(path));
    require(instance!=nullptr,"native paint instance");
    globalOutputStream()<<"Paint checks: select native instance\n";
    instance->setSelected(true);
    GlobalUndoSystem().clear();
    require(Q3mapxPaint_apply(patch,original,sparse.result()),"commit native stroke");
    globalOutputStream()<<"Paint checks: undo/redo\n";
    require(GlobalUndoSystem().size()==1 && sparse.result().matches(patch),"one undo operation per stroke");
    GlobalUndoSystem().undo(); require(original.matches(patch),"real paint undo");
    GlobalUndoSystem().redo(); require(sparse.result().matches(patch),"real paint redo");
    require(!Q3mapxPaint_apply(patch,original,masked.result()),"stale stroke is rejected");
    GlobalUndoSystem().undo();
    auto moved=original; moved.controls[0].m_vertex[0]+=1;
    require(!Q3mapxPaint_apply(patch,original,moved) && original.matches(patch),"paint cannot change geometry");
    Q3mapxPaintStroke cancelled;
    brush.radius=0.3;
    require(cancelled.begin(original,all,brush),"begin cancellable stroke");
    cancelled.move(0.5,0.5); cancelled.end(); cancelled.move(0,0);
    require(!cancelled.active() && original.matches(patch),"cancelled stroke never mutates native patch");
    auto untouched=original;
    untouched.mode=0;
    for(auto& p:untouched.controls) p.m_color={255,255,255,255};
    Q3mapxPaintStroke defaultAlpha;
    Q3mapxPaintBrush whiteAlpha;
    require(defaultAlpha.begin(untouched,all,whiteAlpha),"default alpha stroke"); defaultAlpha.move(0.5,0.5);
    require(defaultAlpha.result().mode==0,"painting default opaque alpha retains legacy representation");
    std::unique_ptr<QWidget> panel(Q3mapxPaint_createPanel());
    auto* alpha=panel->findChild<QCheckBox*>("q3mapxPaintAlpha");
    auto* rgb=panel->findChild<QCheckBox*>("q3mapxPaintRGB");
    auto* value=panel->findChild<QSpinBox*>("q3mapxPaintOpacity");
    auto* fill=panel->findChild<QPushButton*>("q3mapxPaintFill");
    auto* reset=panel->findChild<QPushButton*>("q3mapxPaintReset");
    auto* status=panel->findChild<QLabel*>("q3mapxPaintStatus");
    require(alpha && rgb && value && fill && reset && status,"native paint controls exist");
    require(status->text().contains("9 of 9") && fill->isEnabled(),"native patch selection");
    value->setValue(64); fill->click();
    for(const auto& p:patch) require(p.m_color[3]==64,"alpha fill action");
    GlobalUndoSystem().undo(); require(original.matches(patch),"widget fill is undoable");
    Q3mapxPaint_update();
    GlobalSelectionSystem().SetMode(SelectionSystem::eComponent);
    Q3mapxPaint_update(); require(!fill->isEnabled() && status->text().contains("0 of 9"),"empty component mask disables fill");
    instance->selectCtrl(true); Q3mapxPaint_update();
    require(fill->isEnabled() && status->text().contains("9 of 9"),"component selection enables controls");
    instance->selectCtrl(false); GlobalSelectionSystem().SetMode(SelectionSystem::ePrimitive); Q3mapxPaint_update();
    rgb->setChecked(true); reset->click();
    require(patch.paintMode()==0 && patch.lightmapSampleSize()==8,"full reset restores density-only patch");
    for(const auto& p:patch) require(p.m_color==std::array<unsigned char,4>{255,255,255,255},"full paint reset");
    GlobalUndoSystem().undo(); require(original.matches(patch),"paint reset undo");
    Q3mapxPaint_update();
    panel->resize(550,760);
    QImage image(panel->size(),QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    panel->render(&image);
    require(image.save(outputDirectory+"/paint-controls.png"),"native paint panel render");
    instance->setSelected(false); Q3mapxPaint_update();
    require(!fill->isEnabled(),"deselection clears target");
    panel.reset(); GlobalUndoSystem().clear(); GlobalSceneGraph().erase_root();
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

static void initialiseGrid( Patch& patch, std::size_t width, std::size_t height, int mode ) {
    patch.setDims( width, height );
    patch.SetShader( "textures/q3mapx/paint" );
    for ( std::size_t r = 0; r < height; ++r ) for ( std::size_t c = 0; c < width; ++c ) {
        PatchControl& p = patch.ctrlAt( r,c );
        p.m_vertex = Vector3( -224 + float(c)*4, -224 + float(r)*4, 128 + float((r&1)+(c&1))*8 );
        p.m_texcoord = Vector2( float(c)*0.5f, float(r)*0.25f );
        p.m_color = {255,255,255,255};
        if ( mode != 0 ) p.m_color[3] = (c&1) ? 0 : 252;
        if ( mode == 2 ) {
            p.m_color[0] = (c%3)*80;
            p.m_color[1] = (r%3)*84;
            p.m_color[2] = ((r+c)%3)*96;
        }
    }
    patch.setLightmapSampleSize(8);
    require( patch.setPaintSettings(mode,8), "initialize grid paint mode" );
    patch.controlPointsChanged();
}

static void gridChecks() {
    NodeSmartReference sizes( g_patchCreator->createPatch() );
    const std::size_t requested[] = {0,1,2,3,4,5,30,31,32,std::numeric_limits<std::size_t>::max()};
    const std::size_t expected[] = {3,3,3,3,3,5,29,31,31,31};
    for ( int r = 0; r < 10; ++r ) for ( int c = 0; c < 10; ++c ) {
        Node_getPatch(sizes)->setDims(requested[c],requested[r]);
        require( Node_getPatch(sizes)->getWidth()==expected[c] && Node_getPatch(sizes)->getHeight()==expected[r], "both dimensions clamp and round without unsigned underflow" );
    }
    int roundtrips = 0, boundaries = 0;
    for ( std::size_t width : {3,5,7,31} ) for ( std::size_t height : {3,5,7,31} )
    for ( int mode : {0,1,2} ) for ( bool column : {false,true} ) for ( bool first : {false,true} ) {
        NodeSmartReference node( g_patchCreator->createPatch() );
        Patch& patch = *Node_getPatch(node);
        initialiseGrid( patch,width,height,mode );
        const Q3mapxPaintData before(patch);
        const std::size_t length = column ? width : height;
        if ( length == 3 ) {
            patch.InsertRemove(false,column,first);
            require(before.matches(patch),"minimum-size edit never changes the other axis"); ++boundaries;
        }
        patch.InsertRemove(true,column,first);
        if ( length == 31 ) {
            require(before.matches(patch),"maximum-size edit never changes the other axis"); ++boundaries;
        }
        else {
            require(patch.getWidth()==width+(column?2:0) && patch.getHeight()==height+(column?0:2),"insertion respects chosen axis");
            patch.InsertRemove(false,column,first);
            require(before.matches(patch) && patch.lightmapSampleSize()==8,"subdivide/reduce retains every source control and metadata"); ++roundtrips;
        }
        if ( width==3 && height==3 && column && first ) {
            StringOutputStream source;
            { SimpleTokenWriter writer(source); PatchTokenExporter(patch).exportTokens(writer); }
            output(QString("grid-before-")+QString::number(mode)+".txt",source.c_str());
            patch.InsertRemove(true,true,true); patch.InsertRemove(true,false,false);
            patch.InsertRemove(false,true,true); patch.InsertRemove(false,false,false);
            require(before.matches(patch),"two-axis grid editing returns original source");
            StringOutputStream result;
            { SimpleTokenWriter writer(result); PatchTokenExporter(patch).exportTokens(writer); }
            output(QString("grid-after-")+QString::number(mode)+".txt",result.c_str());
        }
    }
    require(roundtrips==144 && boundaries==96,"complete legacy/alpha/material axis/end matrix");

    // A defect in the final line must reject the whole grid, including all
    // preceding lines that could have been reduced successfully.
    for ( bool column : {false,true} ) for ( bool first : {false,true} ) for ( int channel = 0; channel < 9; ++channel ) {
        NodeSmartReference node(g_patchCreator->createPatch()); Patch& patch=*Node_getPatch(node);
        initialiseGrid(patch,7,5,2); patch.InsertRemove(true,column,first);
        const std::size_t start = first ? 0 : (column?patch.getWidth():patch.getHeight())-5;
        PatchControl& p = column ? patch.ctrlAt(patch.getHeight()-1,start+1) : patch.ctrlAt(start+1,patch.getWidth()-1);
        if ( channel < 3 ) p.m_vertex[channel] += 0.125f;
        else if ( channel < 5 ) p.m_texcoord[channel-3] += 0.125f;
        else p.m_color[channel-5] ^= 1;
        patch.controlPointsChanged();
        const Q3mapxPaintData bad(patch);
        Q3mapxPaintData sentinel; sentinel.width=99;
        require(!Q3mapxPaint_reduceRows(bad,column,first,sentinel) && sentinel.width==99 && sentinel.controls.empty(),"failed reduction leaves output untouched");
        patch.InsertRemove(false,column,first);
        require(bad.matches(patch),"geometry/UV/RGBA discontinuity cannot partially reduce a patch");
    }

    NodeSmartReference node(g_patchCreator->createPatch()); Patch& patch=*Node_getPatch(node);
    initialiseGrid(patch,3,3,2); patch.InsertRemove(true,true,true);
    const Q3mapxPaintData expanded(patch);
    NodeSmartReference root(NewMapRoot("grid-undo")); Node_getTraversable(root)->insert(node); GlobalSceneGraph().insert_root(root);
    GlobalUndoSystem().clear();
    { UndoableCommand command("reduce painted grid"); patch.InsertRemove(false,true,true); }
    const Q3mapxPaintData reduced(patch);
    require(GlobalUndoSystem().size()==1 && reduced.width==3,"native reduction has one undo operation");
    GlobalUndoSystem().undo(); require(expanded.matches(patch),"native reduction undo restores split controls");
    GlobalUndoSystem().redo(); require(reduced.matches(patch),"native reduction redo restores merged controls");
    const std::size_t count=GlobalUndoSystem().size();
    { UndoableCommand command("unavailable grid edit"); patch.InsertRemove(false,true,true); }
    require(GlobalUndoSystem().size()==count && reduced.matches(patch),"boundary no-op creates no undo memento");
    GlobalUndoSystem().clear(); GlobalSceneGraph().erase_root();
}

static int Q3mapxAuthoringTestRun() {
    SurfaceInspector_constructWindow( nullptr );
    EntityList_constructWindow( nullptr );
    GlobalEntityCreator().setKeyValueChangedFunc( +[]{} );
    brushChecks( "quake", eBrushTypeQuake3 );
    brushChecks( "bp", eBrushTypeQuake3BP );
    brushChecks( "valve", eBrushTypeQuake3Valve220 );
    patchChecks();
    paintChecks();
    gridChecks();
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
    EntityList_destroyWindow();
    fprintf( stdout, "q3mapx Radiant authoring: %d checks passed\n", checks );
    return 0;
}
