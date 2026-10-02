// SPDX-License-Identifier: GPL-3.0-or-later
#include "patch_fit.h"
#include "authoring/patch_paint.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <stdexcept>

namespace q3mapx {
namespace {
struct Limit {};
struct Budget {
    uint64_t& used;
    uint64_t limit;
    void spend( size_t n = 1 ) { if ( n > limit - std::min( used, limit ) ) throw Limit{}; used += n; }
};
std::array<double,3> basis( double u ) { return { (1-u)*(1-u), 2*u*(1-u), u*u }; }
double evaluate( const std::array<double,9>& c, double u, double v ) {
    const auto x=basis(u), y=basis(v);
    double value=0;
    for ( int j=0; j<3; ++j ) for ( int i=0; i<3; ++i ) value += x[i]*y[j]*c[j*3+i];
    return value;
}
int rounded( double value ) { return std::clamp( int(std::floor(value+0.5)), 0, 255 ); }

// Byte samples represent rounding intervals, not exact polynomial values.
// Enumerate the small endpoint/midpoint-consistent control ranges and verify
// every sample. Already assigned shared controls constrain later spans.
template<class Accept>
bool colorControls( const std::vector<PatchFitVertex>& grid, int stride, int ox, int oy, int n, int channel,
                    std::array<double,9>& c, const std::array<int,9>& fixed, Budget& budget, Accept accept ) {
    const auto sample = [&]( int x, int y ) { return grid[(oy+y)*stride+ox+x].color[channel]; };
    for ( int y : {0,2} ) for ( int x : {0,2} ) {
        const int i=y*3+x;
        c[i]=sample(x*n/2,y*n/2);
        if ( fixed[i]>=0 && fixed[i]!=c[i] ) return false;
    }
    constexpr int mids[]{1,3,5,7}, ends[][2]{{0,2},{0,6},{2,8},{6,8}};
    std::array<std::vector<int>,4> candidates;
    for ( int edge=0; edge<4; ++edge ) {
        const int a=ends[edge][0], b=ends[edge][1], mid=mids[edge];
        const double estimate=2*sample((mid%3)*n/2,(mid/3)*n/2)-0.5*(c[a]+c[b]);
        for ( int k=std::max(0,int(std::ceil(estimate-1))); k<=std::min(255,int(std::floor(estimate+1))); ++k ) {
            if ( fixed[mid]>=0 && fixed[mid]!=k ) continue;
            bool good=true;
            for ( int t=0; t<=n; ++t ) {
                budget.spend(); const auto w=basis(double(t)/n);
                const int x=(a%3)*n/2+(b%3-a%3)*t/2, y=(a/3)*n/2+(b/3-a/3)*t/2;
                if ( rounded(w[0]*c[a]+w[1]*k+w[2]*c[b])!=sample(x,y) ) { good=false; break; }
            }
            if ( good ) candidates[edge].push_back(k);
        }
        if ( candidates[edge].empty() ) return false;
    }
    for ( int a:candidates[0] ) for ( int b:candidates[1] ) for ( int d:candidates[2] ) for ( int e:candidates[3] ) {
        c[1]=a; c[3]=b; c[5]=d; c[7]=e;
        const double estimate=4*sample(n/2,n/2)-0.25*(c[0]+c[2]+c[6]+c[8])-0.5*(a+b+d+e);
        for ( int center=std::max(0,int(std::ceil(estimate-2))); center<=std::min(255,int(std::floor(estimate+2))); ++center ) {
            if ( fixed[4]>=0 && fixed[4]!=center ) continue;
            c[4]=center; bool good=true;
            for ( int y=0; good && y<=n; ++y ) for ( int x=0; x<=n; ++x ) {
                budget.spend();
                if ( rounded(evaluate(c,double(x)/n,double(y)/n))!=sample(x,y) ) { good=false; break; }
            }
            if ( good && accept(c) ) return true;
        }
    }
    return false;
}
}

PatchFit fitTrianglePatch( std::span<const PatchFitVertex> vertices,
    std::span<const std::array<int,3>> triangles, int firstChannel, uint64_t& work, uint64_t workLimit ) try {
    PatchFit out;
    Budget budget{work,workLimit};
    if ( vertices.size()<25 || vertices.size()>65536 || triangles.size()>131072 ) { out.status="sample_limit"; return out; }
    if ( firstChannel!=0 && firstChannel!=3 && firstChannel!=4 ) { out.status="invalid_channels"; return out; }
    for ( const auto& v:vertices ) {
        budget.spend();
        for ( double value:v.value ) if ( !std::isfinite(value) || std::abs(value)>1e7 ) { out.status="invalid_sample"; return out; }
    }
    // Discover the parameter grid from connectivity, not texture coordinates.
    // On an odd checkerboard grid each corner has degree three and two
    // degree-three boundary neighbours. Other boundary vertices alternate
    // degree three/five. Verify the entire lattice before trusting that clue.
    struct Edge { int faces[2]{-1,-1}; };
    std::map<std::array<int,2>,Edge> edges;
    std::vector<int> degree(vertices.size(),0);
    const auto edgeKey=[](int a,int b) { return std::array<int,2>{std::min(a,b),std::max(a,b)}; };
    for ( size_t i=0; i<triangles.size(); ++i ) {
        const auto& tri=triangles[i];
        budget.spend();
        for ( int v:tri ) if ( v<0 || size_t(v)>=vertices.size() ) { out.status="invalid_index"; return out; }
        if ( tri[0]==tri[1] || tri[1]==tri[2] || tri[0]==tri[2] ) { out.status="degenerate_triangle"; return out; }
        for ( int e=0; e<3; ++e ) {
            const auto key=edgeKey(tri[e],tri[(e+1)%3]);
            auto& edge=edges[key];
            if ( edge.faces[0]<0 ) { edge.faces[0]=int(i); ++degree[key[0]]; ++degree[key[1]]; }
            else if ( edge.faces[1]<0 ) edge.faces[1]=int(i);
            else { out.status="nonmanifold_mesh"; return out; }
        }
    }
    std::map<int,std::vector<int>> boundary;
    for ( const auto& [key,edge]:edges ) if ( edge.faces[1]<0 ) { boundary[key[0]].push_back(key[1]); boundary[key[1]].push_back(key[0]); }
    if ( boundary.empty() ) return out;
    for ( const auto& [v,adj]:boundary ) if ( adj.size()!=2 ) { out.status="nonmanifold_boundary"; return out; }
    std::vector<int> loop;
    int prev=-1, current=boundary.begin()->first;
    do {
        budget.spend(); loop.push_back(current);
        const auto& adj=boundary.at(current);
        const int next=adj[0]==prev?adj[1]:adj[0]; prev=current; current=next;
        if ( loop.size()>boundary.size() ) return out;
    } while ( current!=loop.front() );
    if ( loop.size()!=boundary.size() ) { out.status="multiple_boundaries"; return out; }
    std::vector<int> corners;
    for ( size_t i=0; i<loop.size(); ++i ) {
        budget.spend();
        if ( degree[loop[i]]==3 && degree[loop[(i+loop.size()-1)%loop.size()]]==3
          && degree[loop[(i+1)%loop.size()]]==3 ) corners.push_back(int(i));
    }
    if ( corners.size()!=4 ) { out.status="nonrectangular_topology"; return out; }
    int width=corners[1]-corners[0]+1, height=corners[2]-corners[1]+1;
    if ( width<5 || height<5 || width>481 || height>481 || !(width&1) || !(height&1)
      || corners[3]-corners[2]!=width-1 || int(loop.size())+corners[0]-corners[3]!=height-1 ) {
        out.status="nonrectangular_topology"; return out;
    }
    if ( size_t(width*height)!=vertices.size() || triangles.size()!=size_t((width-1)*(height-1)*2) ) { out.status="incomplete_grid"; return out; }
    // Each lattice edge records the opposite vertex on each incident face.
    // Propagate a one-to-one embedding across the actual triangles, requiring
    // boundary/interior edges and all shared vertices to agree exactly.
    struct Opposite { int vertices[2]{-1,-1}; };
    std::map<std::array<int,2>,Opposite> expected;
    for ( int y=0; y<height-1; ++y ) for ( int x=0; x<width-1; ++x ) {
        budget.spend(2);
        const int p[]{y*width+x,(y+1)*width+x,(y+1)*width+x+1,y*width+x+1,y*width+x};
        const int r=(x+y)&1;
        for ( const std::array<int,3> tri:{std::array{p[r],p[r+1],p[r+2]},std::array{p[r],p[r+2],p[r+3]}} )
            for ( int e=0; e<3; ++e ) {
                auto& opposite=expected[edgeKey(tri[e],tri[(e+1)%3])];
                opposite.vertices[opposite.vertices[0]<0?0:1]=tri[(e+2)%3];
            }
    }
    std::vector<int> location(vertices.size(),-1), occupant(vertices.size(),-1);
    const auto place=[&](int vertex,int cell) {
        if ( (location[vertex]>=0 && location[vertex]!=cell) || (occupant[cell]>=0 && occupant[cell]!=vertex) ) return false;
        location[vertex]=cell; occupant[cell]=vertex; return true;
    };
    const int start=loop[corners[0]], next=loop[corners[0]+1];
    place(start,0); place(next,1);
    const int first=edges.at(edgeKey(start,next)).faces[0];
    for ( int v:triangles[first] ) if ( v!=start && v!=next ) place(v,width+1);
    std::vector<int> queue{first};
    std::vector<bool> visited(triangles.size(),false); visited[first]=true;
    for ( size_t i=0; i<queue.size(); ++i ) {
        const int face=queue[i]; const auto& tri=triangles[face];
        for ( int e=0; e<3; ++e ) {
            budget.spend();
            const int a=tri[e], b=tri[(e+1)%3], c=tri[(e+2)%3];
            const auto found=expected.find(edgeKey(location[a],location[b]));
            if ( found==expected.end() ) { out.status="unsupported_triangulation"; return out; }
            const auto& opposite=found->second.vertices;
            const int third=location[c]==opposite[0]?opposite[1]:location[c]==opposite[1]?opposite[0]:-2;
            const auto& edge=edges.at(edgeKey(a,b));
            const int adjacent=edge.faces[0]==face?edge.faces[1]:edge.faces[0];
            if ( third==-2 || (third<0)!=(adjacent<0) ) { out.status="unsupported_triangulation"; return out; }
            if ( adjacent<0 ) continue;
            for ( int v:triangles[adjacent] ) if ( v!=a && v!=b && !place(v,third) ) {
                out.status="overlapping_grid_samples"; return out;
            }
            if ( !visited[adjacent] ) { visited[adjacent]=true; queue.push_back(adjacent); }
        }
    }
    if ( queue.size()!=triangles.size() || std::find(location.begin(),location.end(),-1)!=location.end() ) {
        out.status="incomplete_grid"; return out;
    }
    std::vector<std::array<int,2>> coords;
    for ( int cell:location ) coords.push_back({cell%width,cell/width});
    int orientation=0;
    std::vector<unsigned> cells((width-1)*(height-1),0);
    for ( const auto& tri:triangles ) {
        budget.spend();
        const auto p=coords[tri[0]], q=coords[tri[1]], r=coords[tri[2]];
        const int x=std::min({p[0],q[0],r[0]}), y=std::min({p[1],q[1],r[1]});
        if ( std::max({p[0],q[0],r[0]})!=x+1 || std::max({p[1],q[1],r[1]})!=y+1 ) { out.status="nonlocal_grid_triangle"; return out; }
        const int winding=(q[0]-p[0])*(r[1]-p[1])-(q[1]-p[1])*(r[0]-p[0]);
        if ( orientation && orientation!=winding ) { out.status="inconsistent_winding"; return out; } orientation=winding;
        unsigned present=0;
        for ( int v:tri ) { const int dx=coords[v][0]-x, dy=coords[v][1]-y; present |= 1u<<(dy?(dx?2:3):dx); }
        const unsigned missing=15^present;
        auto& cell=cells[y*(width-1)+x];
        if ( cell & missing ) { out.status="overlapping_triangles"; return out; } cell |= missing;
    }
    for ( int y=0; y<height-1; ++y ) for ( int x=0; x<width-1; ++x ) {
        if ( cells[y*(width-1)+x] != ( (x+y)&1 ? 5u : 10u ) ) { out.status="unsupported_triangulation"; return out; }
    }
    // Compiler patch quads run clockwise in parameter space. Transposition
    // preserves the checkerboard diagonals while fixing the winding.
    if ( orientation>0 ) { std::swap(width,height); for ( auto& p:coords ) std::swap(p[0],p[1]); }
    std::vector<PatchFitVertex> grid(vertices.size());
    for ( size_t i=0; i<vertices.size(); ++i ) grid[coords[i][1]*width+coords[i][0]]=vertices[i];
    out.status="no_verified_quadratic_fit";
    for ( int n=32; n>=4; n/=2 ) {
        if ( (width-1)%n || (height-1)%n ) continue;
        const int cw=(width-1)/n*2+1, ch=(height-1)/n*2+1;
        if ( !authoring::paintMeshFits(cw,ch,n) ) continue;
        std::vector<PatchFitVertex> controls(cw*ch);
        std::vector<bool> assigned(cw*ch,false);
        double positionError=0, uvError=0;
        bool good=true;
        for ( int sy=0; good && sy<(height-1)/n; ++sy ) for ( int sx=0; good && sx<(width-1)/n; ++sx ) {
            std::array<int,9> ids;
            for ( int j=0; j<3; ++j ) for ( int i=0; i<3; ++i ) ids[j*3+i]=(sy*2+j)*cw+sx*2+i;
            for ( int channel=0; good && channel<5; ++channel ) {
                std::array<double,9> c;
                for ( int j=0; j<3; ++j ) for ( int i=0; i<3; ++i ) c[j*3+i]=grid[(sy*n+j*n/2)*width+sx*n+i*n/2].value[channel];
                for ( int j=0; j<3; ++j ) c[j*3+1]=2*c[j*3+1]-0.5*(c[j*3]+c[j*3+2]);
                for ( int i=0; i<3; ++i ) c[3+i]=2*c[3+i]-0.5*(c[i]+c[6+i]);
                for ( int i=0; i<9; ++i ) {
                    c[i]=float(c[i]);
                    if ( !std::isfinite(c[i]) || std::abs(c[i])>1e7 || (assigned[ids[i]] && controls[ids[i]].value[channel]!=c[i]) ) good=false;
                    controls[ids[i]].value[channel]=c[i];
                }
                for ( int y=0; good && y<=n; ++y ) for ( int x=0; x<=n; ++x ) {
                    budget.spend();
                    const double observed=grid[(sy*n+y)*width+sx*n+x].value[channel];
                    const double error=std::abs(double(float(evaluate(c,double(x)/n,double(y)/n)))-observed);
                    const double tolerance=std::min(channel<3?0.001:0.00001,0.000001+std::abs(observed)*0.00000048);
                    if ( error>tolerance ) { good=false; break; }
                    auto& maxError=channel<3?positionError:uvError; maxError=std::max(maxError,error);
                }
            }
            for ( int id:ids ) assigned[id]=true;
        }
        for ( int channel=firstChannel; good && channel<4; ++channel ) {
            std::vector<int> owners(cw*ch,0);
            const int spansX=(width-1)/n, spanCount=spansX*((height-1)/n);
            std::function<bool(int)> solve = [&](int span) {
                budget.spend();
                if ( span==spanCount ) return true;
                const int sx=span%spansX, sy=span/spansX;
                std::array<int,9> ids,fixed; std::array<double,9> c{};
                for ( int j=0; j<3; ++j ) for ( int i=0; i<3; ++i ) ids[j*3+i]=(sy*2+j)*cw+sx*2+i;
                for ( int i=0; i<9; ++i ) fixed[i]=owners[ids[i]]?controls[ids[i]].color[channel]:-1;
                return colorControls(grid,width,sx*n,sy*n,n,channel,c,fixed,budget,[&](const auto& candidate) {
                    for ( int i=0; i<9; ++i ) { controls[ids[i]].color[channel]=uint8_t(candidate[i]); ++owners[ids[i]]; }
                    if ( solve(span+1) ) return true;
                    for ( int id:ids ) --owners[id];
                    return false;
                });
            };
            good=solve(0);
        }
        if ( good ) {
            out.status="fitted"; out.width=cw; out.height=ch; out.subdivisions=n;
            out.controls=std::move(controls); out.positionError=positionError; out.uvError=uvError; return out;
        }
    }
    return out;
}
catch ( const Limit& ) { PatchFit out; out.status="work_limit"; return out; }
}
