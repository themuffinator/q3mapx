// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/vis_clip.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <random>
#include <stdexcept>
#include <vector>

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct HalfPlane { double y,z,d; };

// Independent 2D feasibility oracle: enumerate vertices of the intersection of
// half-planes, rather than clipping a polygon. Relax/tighten the new planes to
// exclude numerical boundary cases when comparing the float epsilon clipper.
static bool feasible(const std::vector<Vector3>& polygon, const std::vector<Plane3f>& cuts, double margin) {
    std::vector<HalfPlane> constraints;
    for (std::size_t i=0;i<polygon.size();++i) {
        const auto& a=polygon[i]; const auto& b=polygon[(i+1)%polygon.size()];
        const double dy=double(b.y())-a.y(), dz=double(b.z())-a.z(), length=std::hypot(dy,dz);
        constraints.push_back({-dz/length,dy/length,(-dz*a.y()+dy*a.z())/length});
    }
    for (const auto& p : cuts) constraints.push_back({p.b,p.c,p.d+margin});
    const auto inside=[&](double y,double z) {
        return std::all_of(constraints.begin(),constraints.end(),[&](const auto& p) {
            return p.y*y+p.z*z >= p.d-1e-8;
        });
    };
    for (const auto& p:polygon) if (inside(p.y(),p.z())) return true;
    for (std::size_t i=polygon.size();i<constraints.size();++i) {
        const auto& a=constraints[i];
        for (std::size_t j=0;j<i;++j) {
            const auto& b=constraints[j]; const double determinant=a.y*b.z-b.y*a.z;
            if (std::abs(determinant)>1e-12 && inside((a.d*b.z-b.d*a.z)/determinant,
                                                    (a.y*b.d-b.y*a.d)/determinant)) return true;
        }
    }
    return false;
}

int main() try {
    auto scratch=std::make_unique<q3mapx::PassageClipper<1536>>();
    const std::vector<Vector3> square{{0,-10,-10},{0,10,-10},{0,10,10},{0,-10,10}};
    auto result=scratch->intersects(square,{},0.1);
    require(result.visible && !result.overflows,"Uncut winding disappeared");
    require(!scratch->intersects({}, {},0.1).visible,"Empty winding became visible");
    for (const auto& plane : {Plane3f(0,1,0,-11),Plane3f(1,0,0,0),Plane3f(0,1,0,0)}) {
        result=scratch->intersects(square,std::span(&plane,1),0.1);
        require(result.visible && !result.overflows,"Front/on/straddling winding disappeared");
    }
    const Plane3f back(0,1,0,11);
    require(!scratch->intersects(square,std::span(&back,1),0.1).visible,"Back winding survived");
    // The float immediately above double 0.1 must remain strictly in front,
    // as in the inherited clipper (do not round the threshold to float).
    auto epsilonSquare=square;
    for (auto& p:epsilonSquare) p.y()=p.y()>0?float(0.1):-1;
    const Plane3f epsilonPlane(0,1,0,0);
    require(scratch->intersects(epsilonSquare,std::span(&epsilonPlane,1),0.1).visible,"Epsilon threshold changed");

    // A corner cut grows a square into a pentagon. A capacity-four buffer must
    // retain the original rather than an arbitrary four-point prefix. A later
    // cut may still reject it safely, and subsequent calls must reuse scratch.
    q3mapx::PassageClipper<4> tiny;
    const float diagonal=1/std::sqrt(2.0f);
    const std::vector<Plane3f> corner{{0,-diagonal,-diagonal,-15*diagonal}};
    result=tiny.intersects(square,corner,0.1);
    require(result.visible && result.overflows==1,"Overflow was not conservative/reported");
    q3mapx::PassageClipper<5> exact;
    result=exact.intersects(square,corner,0.1);
    require(result.visible && !result.overflows,"Exact-capacity clipped winding was rejected");
    auto survivingCorner=corner;
    survivingCorner.emplace_back(0,-1,0,9);
    survivingCorner.emplace_back(0,0,1,9);
    result=tiny.intersects(square,survivingCorner,0.1);
    require(result.visible && result.overflows==1,"Overflow lost the surviving corner outside a partial prefix");
    auto later=corner; later.push_back(back);
    result=tiny.intersects(square,later,0.1);
    require(!result.visible && result.overflows==1,"Later safe rejection failed after overflow");
    auto oversized=square; oversized.push_back({0,-10,0});
    result=tiny.intersects(oversized,corner,0.1);
    require(result.visible && result.overflows==1,"Oversized input read past bounded scratch");
    require(tiny.intersects(square,{},0.1).visible,"Scratch reuse lost input");

    std::mt19937 random(20260930);
    std::uniform_real_distribution<double> angle(0,2*std::numbers::pi),distance(-1400,1400);
    std::size_t checked=0,ambiguous=0,visible=0,hidden=0;
    for (unsigned count : {3,4,23,24,25,32,64,129,511,512}) {
        std::vector<Vector3> polygon;
        for (unsigned i=0;i<count;++i) {
            const double theta=2*std::numbers::pi*i/count;
            polygon.emplace_back(0,1000*std::cos(theta),1000*std::sin(theta));
        }
        // Many shallow cuts grow the four-point seed well past 24 and 512.
        if (count==4) {
            std::vector<Plane3f> many;
            for (unsigned i=0;i<1024;++i) {
                const double theta=2*std::numbers::pi*i/1024;
                many.emplace_back(0,std::cos(theta),std::sin(theta),-500);
            }
            result=scratch->intersects(polygon,many,0.0);
            require(result.visible && !result.overflows,"Valid intermediate winding exceeded capacity");
            many.emplace_back(0,1,0,600);
            require(!scratch->intersects(polygon,many,0.0).visible,"Grown winding failed disjoint control");
        }
        for (unsigned trial=0;trial<100;++trial) {
            std::vector<Plane3f> planes;
            for (unsigned i=0;i<1+trial%12;++i) {
                const double theta=angle(random);
                planes.emplace_back(0,std::cos(theta),std::sin(theta),distance(random));
            }
            const bool contracted=feasible(polygon,planes,0.5),expanded=feasible(polygon,planes,-0.5);
            if (contracted!=expanded) { ++ambiguous; continue; }
            for (bool reverse : {false,true}) {
                auto winding=polygon;
                std::rotate(winding.begin(),winding.begin()+trial%count,winding.end());
                if (reverse) std::reverse(winding.begin(),winding.end());
                result=scratch->intersects(winding,planes,0.1);
                require(!result.overflows,"Convex input overflowed passage scratch");
                require(result.visible==contracted,"Clipping differs from half-plane feasibility oracle");
                ++checked; visible+=result.visible; hidden+=!result.visible;
            }
        }
    }
    require(visible>100 && hidden>100,"Oracle lacked positive/negative coverage");
    std::cout<<checked<<" winding/half-plane comparisons passed ("<<visible<<" visible, "<<hidden
             <<" hidden, "<<ambiguous<<" numerical boundary cases excluded); growth, epsilon and overflow controls passed\n";
}
catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
