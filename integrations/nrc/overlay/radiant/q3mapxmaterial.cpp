// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapxmaterial.h"
#include "igl.h"
#include "ishaders.h"
#include "itextures.h"
#include "iimage.h"
#include "ifilesystem.h"
#include "iarchive.h"
#include "idatastream.h"
#include "texturelib.h"
#include "renderable.h"
#include "math/vector.h"
#include "authoring/patch_paint.h"
#include "authoring/paint_tga.h"
#include <QOpenGLContext>
#include <QOffscreenSurface>
#include <algorithm>
#include <cmath>

ImageModules& Textures_getImageModules();

namespace {
using namespace q3mapx::authoring;
constexpr GLenum textureMaxAnisotropyEXT=0x84fe;
class StageImage final : public Image {
    PaintTga m_data;
public:
    explicit StageImage(PaintTga data):m_data(std::move(data)){}
    void release() override { delete this; }
    unsigned char* getRGBAPixels() const override { return const_cast<unsigned char*>(m_data.rgba.data()); }
    unsigned getWidth() const override { return m_data.width; }
    unsigned getHeight() const override { return m_data.height; }
};
// NRC's default image loader appends extensions. Material stages may name a
// specific file: preserve that choice instead of silently loading another format.
Image* stageImage( void*, const char* name ) {
    std::string path=name;
    const auto slash=path.find_last_of('/'), dot=path.find_last_of('.');
    std::string extension;
    if ( dot==std::string::npos || (slash!=std::string::npos && dot<slash) ) { path+=".tga"; extension="tga"; }
    else extension=paint_material_detail::lower(path.substr(dot+1));
    if ( extension!="tga" && extension!="png" && extension!="jpg" && extension!="jpeg" ) return nullptr;
    const auto* decoder=Textures_getImageModules().findModule(extension.c_str());
    if ( !decoder && extension!="tga" ) return nullptr;
    ArchiveFile* file=GlobalFileSystem().openFile(path.c_str());
    if ( !file ) return nullptr;
    ScopedArchiveFile close(*file);
    if ( file->size()>64*1024*1024 ) return nullptr;
    if(extension=="tga") {
        std::string bytes(file->size(),'\0'); std::size_t done=0;
        while(done<bytes.size()) {
            const auto n=file->getInputStream().read(reinterpret_cast<unsigned char*>(bytes.data()+done),bytes.size()-done);
            if(!n) return nullptr;
            done+=n;
        }
        auto image=decodePaintTga(bytes);
        return image ? new StageImage(std::move(*image)) : nullptr;
    }
    Image* image=decoder->loadImage(*file);
    if(image && (!image->getWidth() || !image->getHeight() || image->getWidth()>4096 || image->getHeight()>4096)) {
        image->release(); return nullptr;
    }
    return image;
}
const LoadImageCallback stageLoader(nullptr,stageImage);

// Modeless widget callbacks can run between paintGL calls with no current
// context. Use Qt's existing shared context for texture-cache lifetime changes.
class TextureContext {
    QOffscreenSurface m_surface;
    QOpenGLContext* m_context=nullptr;
public:
    TextureContext() {
        if ( !GlobalOpenGL().contextValid || QOpenGLContext::currentContext() ) return;
        if ( auto* context=QOpenGLContext::globalShareContext() ) {
            m_surface.setFormat(context->format()); m_surface.create();
            if ( context->makeCurrent(&m_surface) ) m_context=context;
        }
    }
    bool valid() const { return !GlobalOpenGL().contextValid || QOpenGLContext::currentContext(); }
    ~TextureContext() { if ( m_context ) m_context->doneCurrent(); }
};
const char* stateName( const PaintMaterial& material ) {
    return material.translucent() ? "$Q3MAPX_PAINT_BLEND" : "$Q3MAPX_PAINT_OPAQUE";
}
GLenum blend( PaintBlend value ) {
    static const GLenum values[] = { GL_ZERO,GL_ONE,GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_DST_COLOR,
        GL_ONE_MINUS_DST_COLOR,GL_SRC_COLOR,GL_ONE_MINUS_SRC_COLOR,GL_DST_ALPHA,GL_ONE_MINUS_DST_ALPHA,GL_SRC_ALPHA_SATURATE };
    return values[static_cast<unsigned>(value)];
}
bool same( const Q3mapxPaintData& a, const Q3mapxPaintData& b ) {
    return a.width==b.width && a.height==b.height && a.mode==b.mode && a.subdivisions==b.subdivisions
        && a.controls.size()==b.controls.size() && std::equal(a.controls.begin(),a.controls.end(),b.controls.begin(),
        [](const PatchControl& x,const PatchControl& y){ return x.m_vertex==y.m_vertex && x.m_texcoord==y.m_texcoord && x.m_color==y.m_color; });
}
}

Q3mapxMaterialPreview::Q3mapxMaterialPreview() { GlobalShaderSystem().attach(*this); }
Q3mapxMaterialPreview::~Q3mapxMaterialPreview() { GlobalShaderSystem().detach(*this); release(); }
void Q3mapxMaterialPreview::release() {
    TextureContext context;
    if ( m_state ) { GlobalShaderCache().release(stateName(m_material)); m_state = nullptr; }
    for ( auto* texture : m_textures ) if ( texture ) GlobalTexturesCache().release(texture);
    m_textures.clear(); m_material = {}; m_colors.clear();
}
bool Q3mapxMaterialPreview::material( const char* name ) {
    if ( !m_dirty && m_name==name ) return m_material.valid();
    release(); m_name = name; m_dirty = false;
    TextureContext context;
    if ( !context.valid() ) { m_material.error="The editor's shared GL context is unavailable"; m_dirty=true; return false; }
    IShader* shader = GlobalShaderSystem().getShaderForName(name);
    const std::string filename = shader->getShaderFileName();
    const bool implicit = shader->IsDefault();
    shader->DecRef();
    if ( implicit || filename.empty() ) { m_material.error = "An explicit Quake III shader is required"; return false; }
    ArchiveFile* file = GlobalFileSystem().openFile(filename.c_str());
    if ( !file ) { m_material.error = "Cannot read the shader from the game filesystem"; return false; }
    {
        ScopedArchiveFile close(*file);
        if ( file->size()>4*1024*1024 ) { m_material.error = "Shader source exceeds 4 MiB"; return false; }
        std::string source(file->size(),'\0');
        std::size_t done = 0;
        while ( done<source.size() ) {
            const auto count = file->getInputStream().read(reinterpret_cast<unsigned char*>(source.data()+done),source.size()-done);
            if ( !count ) { m_material.error = "Incomplete shader read"; return false; }
            done += count;
        }
        m_material = parsePaintMaterial(source,m_name);
    }
    if ( !m_material.valid() ) return false;
    for ( const auto& stage : m_material.stages ) {
        if ( stage.image=="$whiteimage" || stage.image=="$lightmap" ) { m_textures.push_back(nullptr); continue; }
        // Check availability instead of silently substituting an editor checker.
        Image* image = stageLoader.loadImage(stage.image.c_str());
        if ( !image ) { m_material.error = "Missing stage image: "+stage.image; break; }
        const bool bounded = image->getWidth()>0 && image->getHeight()>0 && image->getWidth()<=4096 && image->getHeight()<=4096;
        image->release();
        if ( !bounded ) { m_material.error = "Stage image exceeds 4096 x 4096"; break; }
        m_textures.push_back(GlobalTexturesCache().capture(stageLoader,stage.image.c_str()));
    }
    if ( !m_material.valid() ) {
        const std::string reason = m_material.error; release(); m_material.error = reason; return false;
    }
    colors();
    return true;
}

void Q3mapxMaterialPreview::colors() {
    m_colors.clear();
    if ( !m_material.valid() ) return;
    for ( const auto& stage : m_material.stages ) {
        auto& values = m_colors.emplace_back(); values.reserve(m_vertices.size());
        for ( const auto& vertex : m_vertices ) values.push_back(stage.color(vertex.color));
    }
}

bool Q3mapxMaterialPreview::mesh( const Q3mapxPaintData& data ) {
    if ( same(m_data,data) && !m_vertices.empty() ) return true;
    m_vertices.clear(); m_indices.clear(); m_colors.clear(); m_data = {};
    if ( !data.valid() ) return false;
    // A bounded, smooth authoring mesh. Compiler/entity geometric settings may
    // choose a coarser grid; this preview is not a compiled topology inspector.
    const std::size_t n = std::max(16,data.subdivisions);
    const std::size_t w = (data.width-1)/2*n+1, h = (data.height-1)/2*n+1;
    if ( w*h>maxPaintVertices ) return false;
    m_vertices.reserve(w*h); m_indices.reserve((w-1)*(h-1)*6);
    for ( std::size_t y=0; y<h; ++y ) for ( std::size_t x=0; x<w; ++x ) {
        const std::size_t sx=std::min(x/n,(data.width-3)/2), sy=std::min(y/n,(data.height-3)/2);
        const double u=double(x-sx*n)/n, v=double(y-sy*n)/n;
        const double bu[3]={(1-u)*(1-u),2*u*(1-u),u*u}, bv[3]={(1-v)*(1-v),2*v*(1-v),v*v};
        double values[9]{};
        for ( unsigned j=0; j<3; ++j ) for ( unsigned i=0; i<3; ++i ) {
            const auto& c = data.controls[(sy*2+j)*data.width+sx*2+i];
            const double weight=bu[i]*bv[j];
            for ( unsigned axis=0; axis<3; ++axis ) values[axis] += c.m_vertex[axis]*weight;
            for ( unsigned axis=0; axis<2; ++axis ) values[axis+3] += c.m_texcoord[axis]*weight;
            for ( unsigned axis=0; axis<4; ++axis ) values[axis+5] += c.m_color[axis]*weight;
        }
        Q3mapxMaterialVertex vertex;
        for ( unsigned axis=0; axis<3; ++axis ) vertex.xyz[axis] = values[axis];
        for ( unsigned axis=0; axis<2; ++axis ) vertex.uv[axis] = values[axis+3];
        for ( unsigned axis=0; axis<4; ++axis ) vertex.color[axis] = std::clamp(std::floor(values[axis+5]+0.5),0.0,255.0);
        m_vertices.push_back(vertex);
    }
    for ( unsigned y=0; y+1<h; ++y ) for ( unsigned x=0; x+1<w; ++x ) {
        const unsigned quad[]={unsigned(y*w+x),unsigned((y+1)*w+x),unsigned((y+1)*w+x+1),unsigned(y*w+x+1),unsigned(y*w+x)};
        const unsigned r=(x+y)&1;
        m_indices.insert(m_indices.end(),{quad[r],quad[r+1],quad[r+2],quad[r],quad[r+2],quad[r+3]});
    }
    m_data = data; colors();
    return true;
}

bool Q3mapxMaterialPreview::submit( Renderer& renderer, const Matrix4& transform, const OpenGLRenderable& fallback ) {
    if ( !m_material.valid() || m_vertices.empty() || renderer.getStyle()!=Renderer::eFullMaterials ) return false;
    m_fallback = &fallback;
    if ( !m_state ) m_state = GlobalShaderCache().capture(stateName(m_material));
    renderer.PushState();
    renderer.Highlight(Renderer::EHighlightMode(Renderer::eFace|Renderer::ePrimitive|Renderer::eFaceWire|Renderer::ePrimitiveWire),false);
    renderer.SetState(m_state,Renderer::eFullMaterials);
    renderer.addRenderable(*this,transform);
    renderer.PopState();
    return true;
}

void Q3mapxMaterialPreview::render( RenderStateFlags flags ) const {
    if ( !(flags & RENDER_TEXTURE) || !(flags & RENDER_FILL) ) {
        if ( m_fallback ) m_fallback->render(flags);
        return;
    }
    if ( !m_material.valid() || m_vertices.empty() || m_textures.size()!=m_material.stages.size() ) return;
    auto& g=gl();
    GLint program, arrayBuffer, elementBuffer, matrixMode;
    g.glGetIntegerv(GL_CURRENT_PROGRAM,&program); g.glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&arrayBuffer);
    g.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&elementBuffer); g.glGetIntegerv(GL_MATRIX_MODE,&matrixMode);
    g.glPushAttrib(GL_ALL_ATTRIB_BITS); g.glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    g.glUseProgram(0); g.glBindBuffer(GL_ARRAY_BUFFER,0); g.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    GLint units; g.glGetIntegerv(GL_MAX_TEXTURE_UNITS,&units);
    for ( GLint unit=0; unit<units; ++unit ) {
        g.glActiveTexture(GL_TEXTURE0+unit); g.glClientActiveTexture(GL_TEXTURE0+unit);
        g.glDisable(GL_TEXTURE_2D); g.glDisable(GL_TEXTURE_1D); g.glDisable(GL_TEXTURE_3D); g.glDisable(GL_TEXTURE_CUBE_MAP);
        g.glDisable(GL_TEXTURE_GEN_S); g.glDisable(GL_TEXTURE_GEN_T); g.glDisable(GL_TEXTURE_GEN_R); g.glDisable(GL_TEXTURE_GEN_Q);
        g.glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    g.glActiveTexture(GL_TEXTURE0); g.glClientActiveTexture(GL_TEXTURE0);
    g.glMatrixMode(GL_TEXTURE); g.glPushMatrix(); g.glLoadIdentity();
    g.glDisable(GL_LIGHTING); g.glDisable(GL_FOG); g.glDisable(GL_POLYGON_OFFSET_FILL);
    g.glDisable(GL_POLYGON_STIPPLE); g.glDisable(GL_DITHER); g.glEnable(GL_DEPTH_TEST); g.glDepthRange(0,1);
    // Qt composites the editor framebuffer as premultiplied content. Preserve
    // its coverage alpha; material alpha controls RGB blending/coverage only.
    g.glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE); g.glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); g.glShadeModel(GL_SMOOTH);
    // Keep the caller's front-face convention, including reflected instances.
    if ( m_material.cull==PaintCull::none ) g.glDisable(GL_CULL_FACE);
    else { g.glEnable(GL_CULL_FACE); g.glCullFace(m_material.cull==PaintCull::front ? GL_BACK : GL_FRONT); }
    g.glEnableClientState(GL_VERTEX_ARRAY); g.glEnableClientState(GL_COLOR_ARRAY); g.glDisableClientState(GL_NORMAL_ARRAY);
    g.glVertexPointer(3,GL_FLOAT,sizeof(Q3mapxMaterialVertex),m_vertices[0].xyz);
    g.glTexCoordPointer(2,GL_FLOAT,sizeof(Q3mapxMaterialVertex),m_vertices[0].uv);
    g.glBlendEquation(GL_FUNC_ADD);
    for ( std::size_t i=0; i<m_material.stages.size(); ++i ) {
        const auto& stage=m_material.stages[i];
        g.glColorPointer(4,GL_UNSIGNED_BYTE,0,m_colors[i].data());
        g.glDepthMask(stage.depthWrite ? GL_TRUE : GL_FALSE); g.glDepthFunc(stage.depthEqual ? GL_EQUAL : GL_LEQUAL);
        if ( stage.blended() ) { g.glEnable(GL_BLEND); g.glBlendFunc(blend(stage.src),blend(stage.dst)); }
        else g.glDisable(GL_BLEND);
        if ( stage.alphaTest==PaintAlphaTest::none ) g.glDisable(GL_ALPHA_TEST);
        else {
            g.glEnable(GL_ALPHA_TEST);
            g.glAlphaFunc(stage.alphaTest==PaintAlphaTest::gt0 ? GL_GREATER : stage.alphaTest==PaintAlphaTest::lt128 ? GL_LESS : GL_GEQUAL,
                          stage.alphaTest==PaintAlphaTest::gt0 ? 0.f : 0.5f);
        }
        // Cached editor textures share object state with the normal renderer.
        // Restore parameters explicitly: glPushAttrib does not save them.
        static const GLenum keys[]={GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,GL_TEXTURE_BASE_LEVEL,GL_TEXTURE_MAX_LEVEL};
        GLint previous[6]{};
        GLfloat anisotropy=1;
        const bool hasAnisotropy=QOpenGLContext::currentContext()->hasExtension("GL_EXT_texture_filter_anisotropic");
        if ( m_textures[i] ) {
            g.glEnable(GL_TEXTURE_2D); g.glEnableClientState(GL_TEXTURE_COORD_ARRAY);
            g.glBindTexture(GL_TEXTURE_2D,m_textures[i]->texture_number);
            for ( int p=0;p<6;++p ) g.glGetTexParameteriv(GL_TEXTURE_2D,keys[p],&previous[p]);
            if(hasAnisotropy) { g.glGetTexParameterfv(GL_TEXTURE_2D,textureMaxAnisotropyEXT,&anisotropy); g.glTexParameterf(GL_TEXTURE_2D,textureMaxAnisotropyEXT,1); }
            g.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,stage.clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            g.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,stage.clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            g.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); g.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            g.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,0); g.glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
            g.glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        }
        else { g.glDisable(GL_TEXTURE_2D); g.glDisableClientState(GL_TEXTURE_COORD_ARRAY); }
        g.glDrawElements(GL_TRIANGLES,GLsizei(m_indices.size()),GL_UNSIGNED_INT,m_indices.data());
        if ( m_textures[i] ) for ( int p=0;p<6;++p ) g.glTexParameteri(GL_TEXTURE_2D,keys[p],previous[p]);
        if ( m_textures[i] && hasAnisotropy ) g.glTexParameterf(GL_TEXTURE_2D,textureMaxAnisotropyEXT,anisotropy);
    }
    g.glMatrixMode(GL_TEXTURE); g.glPopMatrix(); g.glMatrixMode(matrixMode);
    g.glPopClientAttrib(); g.glPopAttrib();
    g.glBindBuffer(GL_ARRAY_BUFFER,arrayBuffer); g.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,elementBuffer); g.glUseProgram(program);
}
