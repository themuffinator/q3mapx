// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "irender.h"
#include "moduleobserver.h"
#include "q3mapxpaint.h"
#include "authoring/paint_material.h"
#include <string>

class Renderer;
struct Q3mapxMaterialVertex {
    float xyz[3], uv[2];
    std::array<unsigned char,4> color;
};

// One selected-patch preview. Shader refreshes invalidate VFS/texture resources;
// all CPU meshes are bounded and GPU state is restored after each draw.
class Q3mapxMaterialPreview final : public OpenGLRenderable, public ModuleObserver {
    std::string m_name;
    q3mapx::authoring::PaintMaterial m_material;
    std::vector<qtexture_t*> m_textures;
    std::vector<Q3mapxMaterialVertex> m_vertices;
    std::vector<unsigned int> m_indices;
    std::vector<std::vector<std::array<unsigned char,4>>> m_colors;
    Q3mapxPaintData m_data;
    Shader* m_state = nullptr;
    const OpenGLRenderable* m_fallback = nullptr;
    bool m_dirty = true;
    void release();
    void colors();
public:
    Q3mapxMaterialPreview();
    ~Q3mapxMaterialPreview();
    Q3mapxMaterialPreview(const Q3mapxMaterialPreview&) = delete;
    Q3mapxMaterialPreview& operator=(const Q3mapxMaterialPreview&) = delete;
    void realise() override { m_dirty = true; }
    void unrealise() override { release(); m_dirty = true; }
    void refresh() { release(); m_dirty = true; }
    bool material( const char* name );
    bool mesh( const Q3mapxPaintData& );
    const auto& vertices() const { return m_vertices; }
    const auto& indices() const { return m_indices; }
    const auto& definition() const { return m_material; }
    bool submit( Renderer&, const Matrix4&, const OpenGLRenderable& fallback );
    void render( RenderStateFlags ) const override;
};
