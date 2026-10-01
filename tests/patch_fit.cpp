// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/patch_fit.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <stdexcept>

using namespace q3mapx;
static void require( bool value, const char* message ) { if (!value) throw std::runtime_error(message); }
static long double curve( long double a, long double b, long double c, long double t ) {
    return (1-t)*((1-t)*a+t*b)+t*((1-t)*b+t*c);
}
static std::vector<PatchFitVertex> sample( const std::vector<PatchFitVertex>& controls, int width, int height, int n ) {
    std::vector<PatchFitVertex> result;
    const int sx=(width-1)/2, sy=(height-1)/2;
    for (int y=0; y<=sy*n; ++y) for (int x=0; x<=sx*n; ++x) {
        const int px=std::min(x/n,sx-1), py=std::min(y/n,sy-1);
        const long double u=static_cast<long double>(x-px*n)/n, v=static_cast<long double>(y-py*n)/n;
        PatchFitVertex vert;
        for (int ch=0; ch<9; ++ch) {
            long double rows[3];
            for (int j=0; j<3; ++j) {
                long double c[3];
                for (int i=0; i<3; ++i) { const auto& p=controls[(py*2+j)*width+px*2+i]; c[i]=ch<5?p.value[ch]:p.color[ch-5]; }
                rows[j]=curve(c[0],c[1],c[2],u);
            }
            const auto value=curve(rows[0],rows[1],rows[2],v);
            if(ch<5) vert.value[ch]=float(value);
            else vert.color[ch-5]=uint8_t(std::floor(value+0.5L));
        }
        result.push_back(vert);
    }
    return result;
}
static std::vector<std::array<int,3>> topology( int w,int h ) {
    std::vector<std::array<int,3>> triangles;
    for(int y=0;y<h-1;++y) for(int x=0;x<w-1;++x) {
        int p[]{y*w+x,(y+1)*w+x,(y+1)*w+x+1,y*w+x+1,y*w+x}; int r=(x+y)&1;
        triangles.push_back({p[r],p[r+1],p[r+2]}); triangles.push_back({p[r],p[r+2],p[r+3]});
    }
    return triangles;
}
int main() try {
    size_t cases=0,samples=0;
    std::mt19937 random(713);
    std::vector<PatchFitVertex> last; std::vector<std::array<int,3>> lastTris;
    for(int width:{3,5}) for(int n:{4,8,16,32}) for(int channels:{0,3,4}) for(bool curved:{false,true}) {
        std::vector<PatchFitVertex> c;
        for(int y=0;y<3;++y) for(int x=0;x<width;++x) {
            PatchFitVertex v;
            v.value={double(x*64-128),double(y*64-64),double(128+(curved?((x%2)*48+(y%2)*32):0)),3.25+x+y*0.25,-5.5+y-x*0.5};
            v.color={uint8_t((x%3)*73),uint8_t(y*99),uint8_t((x%2&&y%2)?253:5),uint8_t((x%2&&y%2)?3:253)};
            c.push_back(v);
        }
        auto vertices=sample(c,width,3,n); auto triangles=topology((width-1)/2*n+1,n+1);
        if(channels!=0) for(auto& v:vertices) for(int k=0;k<channels;++k) v.color[k]=255;
        // Randomized BSP emission order must not supply an implicit grid oracle.
        std::vector<int> order(vertices.size()),inverse(vertices.size()); std::iota(order.begin(),order.end(),0);
        std::shuffle(order.begin(),order.end(),random); auto shuffled=vertices;
        for(size_t i=0;i<order.size();++i) { shuffled[i]=vertices[order[i]]; inverse[order[i]]=i; }
        for(auto& tri:triangles) for(int& v:tri) v=inverse[v];
        std::shuffle(triangles.begin(),triangles.end(),random);
        uint64_t work=0; const auto fit=fitTrianglePatch(shuffled,triangles,channels,work,50'000'000);
        if(fit.controls.empty()) throw std::runtime_error(std::string("Positive fit rejected: ")+fit.status+" width="+std::to_string(width)+" n="+std::to_string(n)+" channels="+std::to_string(channels)+" curved="+std::to_string(curved));
        auto rebuilt=sample(fit.controls,fit.width,fit.height,fit.subdivisions);
        std::sort(vertices.begin(),vertices.end()); std::sort(rebuilt.begin(),rebuilt.end());
        require(vertices==rebuilt,"Independent sampled-field mismatch");
        samples+=vertices.size(); ++cases;
        last=std::move(shuffled); lastTris=std::move(triangles);
    }
    size_t rejected=0;
    const auto reject=[&](const auto& v,const auto& t,int channels=0,uint64_t limit=50'000'000) {
        uint64_t work=0; const auto fit=fitTrianglePatch(v,t,channels,work,limit);
        require(fit.controls.empty(),"Invalid mesh was fitted"); require(work<=limit,"Work limit exceeded"); ++rejected;
    };
    auto t=lastTris; t.pop_back(); reject(last,t);
    t=lastTris; t.push_back(t[0]); reject(last,t);
    t=lastTris; std::swap(t[0][0],t[0][1]); reject(last,t);
    t=lastTris; t[0][0]=int(last.size()); reject(last,t);
    t=lastTris; t[0][0]=t[0][1]; reject(last,t);
    auto v=last; v[0].value[0]=std::numeric_limits<double>::quiet_NaN(); reject(v,lastTris);
    v=last; v[0].value[2]+=1; reject(v,lastTris);
    v=last; v[0].value[3]+=0.003; reject(v,lastTris);
    v=last; v[0].color[3]=0; reject(v,lastTris);
    reject(last,lastTris,0,1); reject(last,lastTris,2);
    std::cout<<cases<<" shuffled affine-UV fit cases, "<<samples<<" independent samples and "<<rejected<<" rejection cases passed\n";
}
catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
