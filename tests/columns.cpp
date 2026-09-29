// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/columns.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

int main(){
    q3mapx::ColumnScene scene;
    std::mt19937 generator(42);
    std::uniform_real_distribution<float> coordinate(-500,500), extent(1,50);
    for ( int i = 0; i < 500; ++i ) {
        const float x = coordinate(generator), y = coordinate(generator), z = coordinate(generator), e = extent(generator);
        q3mapx::ColumnBrush b{}; b.first = uint32_t(scene.planes.size()); b.count = 6;
        std::array<q3mapx::ColumnPlane,6> planes{{{1,0,0,x+e},{-1,0,0,-x},{0,1,0,y+e},
                                                {0,-1,0,-y},{0,0,1,z+e},{0,0,-1,-z}}};
        std::shuffle(planes.begin(),planes.end(),generator);
        scene.planes.insert(scene.planes.end(),planes.begin(),planes.end());
        scene.brushes.push_back(b);
    }
    // Nonaxial sides must conservatively occupy every possibly intersecting cell.
    q3mapx::ColumnBrush rotated{}; rotated.first = uint32_t(scene.planes.size()); rotated.count = 6;
    for ( auto plane : std::initializer_list<q3mapx::ColumnPlane>{{1,1,0,20},{-1,-1,0,20},
           {1,-1,0,20},{-1,1,0,20},{0,0,1,40},{0,0,-1,0}} ) scene.planes.push_back(plane);
    scene.brushes.push_back(rotated);
    scene.buildIndex(-400,-400,400,400);
    for ( int i = 0; i < 50000; ++i ) {
        const float x = coordinate(generator), y = coordinate(generator);
        if ( scene.sample(x,y) != scene.sample(x,y,false) ) { std::cerr << "Index missed brush\n"; return 1; }
    }
    for ( const auto& b : scene.brushes ) {
        if ( !std::isfinite(b.minX) ) continue;
        for ( float x : {b.minX,b.maxX} ) for ( float y : {b.minY,b.maxY} )
            if ( scene.sample(x,y) != scene.sample(x,y,false) ) return 2;
    }
    std::cout << "50,000 random columns, boundary points, shuffled and nonaxial brush planes passed\n";
}
