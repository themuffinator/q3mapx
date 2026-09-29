// SPDX-License-Identifier: GPL-3.0-or-later
#include "columns.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace q3mapx {
static unsigned cell( float value, float minimum, float scale, unsigned grid ){
    return unsigned(std::clamp((value - minimum) * scale, 0.0f, float(grid - 1)));
}

void ColumnScene::buildIndex( float x0, float y0, float x1, float y1 ){
    if ( !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)
         || x1 <= x0 || y1 <= y0 ) throw std::invalid_argument("Invalid column index bounds");
    grid = std::clamp(unsigned(std::sqrt(double(brushes.size())) * 2), 1u, 128u);
    minX = x0; minY = y0; scaleX = grid / (x1 - x0); scaleY = grid / (y1 - y0);
    std::vector<std::vector<uint32_t>> lists(grid * grid);
    size_t referenceCount = 0;
    for ( size_t i = 0; i < brushes.size(); ++i ) {
        auto& b = brushes[i];
        if ( size_t(b.first) + b.count > planes.size() ) throw std::invalid_argument("Invalid column plane range");
        b.minX = b.minY = -std::numeric_limits<float>::infinity();
        b.maxX = b.maxY = std::numeric_limits<float>::infinity();
        // Conservative axial bounds: no assumption about brush-side ordering.
        for ( unsigned j = 0; j < b.count; ++j ) {
            const auto& p = planes[b.first + j];
            if ( p.y == 0 && p.z == 0 && p.x != 0 ) {
                const float bound = p.distance / p.x;
                if ( p.x > 0 ) b.maxX = std::min(b.maxX, bound); else b.minX = std::max(b.minX, bound);
            }
            if ( p.x == 0 && p.z == 0 && p.y != 0 ) {
                const float bound = p.distance / p.y;
                if ( p.y > 0 ) b.maxY = std::min(b.maxY, bound); else b.minY = std::max(b.minY, bound);
            }
        }
        // Clamp outside brushes to boundary cells, also covering supersample spill.
        const unsigned left = cell(b.minX, minX, scaleX, grid), right = cell(b.maxX, minX, scaleX, grid);
        const unsigned bottom = cell(b.minY, minY, scaleY, grid), top = cell(b.maxY, minY, scaleY, grid);
        for ( unsigned y = bottom; y <= top; ++y ) for ( unsigned x = left; x <= right; ++x ) {
            if ( ++referenceCount > 64 * 1024 * 1024 ) throw std::length_error("Column index exceeds 256 MiB reference budget");
            lists[y * grid + x].push_back(uint32_t(i));
        }
    }
    cells.resize(grid * grid);
    references.clear();
    for ( size_t i = 0; i < lists.size(); ++i ) {
        if ( references.size() + lists[i].size() > 64 * 1024 * 1024 )
            throw std::length_error("Column index exceeds 256 MiB reference budget");
        cells[i] = {uint32_t(references.size()), uint32_t(lists[i].size())};
        references.insert(references.end(), lists[i].begin(), lists[i].end());
    }
}

static float intersection( const ColumnScene& scene, const ColumnBrush& b, float x, float y ){
    if ( x < b.minX || x > b.maxX || y < b.minY || y > b.maxY ) return 0;
    bool in = false, out = false;
    float near = 0, far = 0;
    for ( unsigned j = 0; j < b.count; ++j ) {
        const auto& p = scene.planes[b.first + j];
        const float distance = x * p.x + y * p.y;
        if ( p.z == 0 ) { if ( distance > p.distance ) return 0; }
        else {
            const float t = (p.distance - distance) / p.z;
            if ( p.z < 0 ) { if (!in || t > near) near = t; in = true; }
            else { if (!out || t < far) far = t; out = true; }
            if ( in && out && near >= far ) return 0;
        }
    }
    return in && out ? far - near : 0;
}

float ColumnScene::sample( float x, float y, bool indexed ) const {
    float value = 0;
    if ( indexed ) {
        const auto& range = cells[cell(y,minY,scaleY,grid) * grid + cell(x,minX,scaleX,grid)];
        for ( unsigned i = 0; i < range[1]; ++i ) value += intersection(*this, brushes[references[range[0] + i]], x, y);
    }
    else for ( const auto& brush : brushes ) value += intersection(*this, brush, x, y);
    return value;
}
}
