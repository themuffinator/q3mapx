// SPDX-License-Identifier: GPL-3.0-or-later
#include "bezier_uv.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#ifdef __FAST_MATH__
#error Bezier interval bounds require ordinary IEEE arithmetic
#endif

namespace q3mapx {
namespace {
constexpr double inf=std::numeric_limits<double>::infinity();
double down(double x) { return std::nextafter(x,-inf); }
double up(double x) { return std::nextafter(x,inf); }
struct Interval { double lo=0,hi=0; };
Interval exact(double x) { return {x,x}; }
Interval operator+(Interval a,Interval b) { return {down(a.lo+b.lo),up(a.hi+b.hi)}; }
Interval operator-(Interval a,Interval b) { return {down(a.lo-b.hi),up(a.hi-b.lo)}; }
Interval operator*(Interval a,Interval b) {
    const std::array<double,4> p{a.lo*b.lo,a.lo*b.hi,a.hi*b.lo,a.hi*b.hi};
    return {down(*std::min_element(p.begin(),p.end())),up(*std::max_element(p.begin(),p.end()))};
}
Interval scale(Interval a,double x) { return a*exact(x); }
double magnitude(Interval x) { return std::max(std::abs(x.lo),std::abs(x.hi)); }
double midpoint(Interval x) { return x.lo*.5+x.hi*.5; }
using IPoint=std::array<Interval,2>;
using INet=std::array<IPoint,9>;
using Matrix=std::array<std::array<double,2>,2>;
using IMatrix=std::array<std::array<Interval,2>,2>;
IPoint evaluate(const INet& net,const BezierUV& p) {
    std::array<std::array<Interval,3>,2> b;
    for (size_t a=0;a<2;++a) {
        const Interval t=exact(p[a]),s=exact(1)-t;
        b[a]={s*s,scale(t*s,2),t*t};
    }
    IPoint value{};
    for (size_t y=0;y<3;++y) for (size_t x=0;x<3;++x) for (size_t a=0;a<2;++a)
        value[a]=value[a]+net[y*3+x][a]*b[0][x]*b[1][y];
    return value;
}
BezierUVNet midpoints(const INet& net) {
    BezierUVNet out;
    for (size_t i=0;i<9;++i) for (size_t a=0;a<2;++a) out[i][a]=midpoint(net[i][a]);
    return out;
}
bool inverse(const std::array<double,2>& du,const std::array<double,2>& dv,Matrix& result) {
    const double det=du[0]*dv[1]-du[1]*dv[0];
    const double size=std::max({std::abs(du[0]),std::abs(du[1]),std::abs(dv[0]),std::abs(dv[1])});
    if (!std::isfinite(det) || std::abs(det)<=1e-14*size*size) return false;
    result={{{dv[1]/det,-dv[0]/det},{-du[1]/det,du[0]/det}}};
    for (const auto& row:result) for (double x:row) if (!std::isfinite(x)) return false;
    return true;
}
IPoint multiply(const Matrix& m,const IPoint& p) {
    IPoint out;
    for (size_t a=0;a<2;++a) out[a]=scale(p[0],m[a][0])+scale(p[1],m[a][1]);
    return out;
}
// Subdivision intervals include arithmetic rounding, so their convex hulls also
// contain the original binary64 control net restricted to the child rectangle.
std::array<INet,2> split(const INet& net,unsigned axis) {
    std::array<INet,2> out;
    for (size_t line=0;line<3;++line) {
        const auto index=[&](size_t along) { return axis?along*3+line:line*3+along; };
        for (size_t a=0;a<2;++a) {
            const Interval p0=net[index(0)][a],p1=net[index(1)][a],p2=net[index(2)][a];
            const Interval a01=scale(p0+p1,.5),a12=scale(p1+p2,.5),center=scale(a01+a12,.5);
            out[0][index(0)][a]=p0; out[0][index(1)][a]=a01; out[0][index(2)][a]=center;
            out[1][index(0)][a]=center; out[1][index(1)][a]=a12; out[1][index(2)][a]=p2;
        }
    }
    return out;
}
bool outsideControlHull(const INet& controls,const BezierUV& target,const BezierWork& charge) {
    // Any separating projection is sufficient. Testing control-pair directions
    // avoids an orientation-sensitive floating-point hull construction; interval
    // dot products enclose every true restricted control point.
    const auto centers=midpoints(controls);
    for (size_t i=0;i<9;++i) for (size_t j=i+1;j<9;++j) {
        BezierUV n{centers[j][1]-centers[i][1],centers[i][0]-centers[j][0]};
        const double length=std::max(std::abs(n[0]),std::abs(n[1]));
        if (length==0) continue;
        charge(1); for (auto& v:n) v/=length;
        Interval bound{inf,-inf};
        for (const auto& p:controls) {
            const Interval projection=scale(p[0]-exact(target[0]),n[0])+scale(p[1]-exact(target[1]),n[1]);
            bound.lo=std::min(bound.lo,projection.lo); bound.hi=std::max(bound.hi,projection.hi);
        }
        if (bound.lo>0 || bound.hi<0) return true;
    }
    return false;
}
}

struct BezierUVChart::Node {
    INet controls;
    BezierUV lo{},hi{},origin{},extent{1,1};
    Matrix preconditioner{};
    IMatrix remainder{};
    double contraction=inf;
    int children[2]{-1,-1};
    unsigned depth=0;
    bool regular=false;
};

BezierUVChart::BezierUVChart(const BezierUVNet& controls,const BezierWork& charge) {
    Node root;
    for (size_t i=0;i<9;++i) for (size_t a=0;a<2;++a) {
        if (!std::isfinite(controls[i][a]) || std::abs(controls[i][a])>1e45)
            throw std::invalid_argument("Bezier UV control outside finite supported range");
        root.controls[i][a]=exact(controls[i][a]);
    }
    nodes_.push_back(root);
    std::vector<size_t> pending{0};
    while (!pending.empty()) {
        const size_t index=pending.back(); pending.pop_back(); charge(1);
        auto& node=nodes_[index];
        node.lo.fill(inf); node.hi.fill(-inf);
        for (const auto& p:node.controls) for (size_t a=0;a<2;++a) {
            node.lo[a]=std::min(node.lo[a],p[a].lo); node.hi[a]=std::max(node.hi[a],p[a].hi);
        }
        const auto center=evaluateBezier(midpoints(node.controls),.5,.5);
        if (inverse(center.du,center.dv,node.preconditioner)) {
            IMatrix jacobian;
            for (size_t a=0;a<2;++a) for (size_t dir=0;dir<2;++dir) {
                Interval bound{inf,-inf};
                for (size_t y=0;y<(dir?2:3);++y) for (size_t x=0;x<(dir?3:2);++x) {
                    const Interval derivative=scale(node.controls[y*3+x+(dir?3:1)][a]-node.controls[y*3+x][a],2);
                    bound.lo=std::min(bound.lo,derivative.lo); bound.hi=std::max(bound.hi,derivative.hi);
                }
                jacobian[a][dir]=bound;
            }
            for (size_t a=0;a<2;++a) for (size_t b=0;b<2;++b)
                node.remainder[a][b]=exact(a==b?1:0)-(scale(jacobian[0][b],node.preconditioner[a][0])+scale(jacobian[1][b],node.preconditioner[a][1]));
            node.contraction=std::max(up(magnitude(node.remainder[0][0])+magnitude(node.remainder[0][1])),
                                      up(magnitude(node.remainder[1][0])+magnitude(node.remainder[1][1])));
            node.regular=node.contraction<=.75;
        }
        if (node.regular || node.lo[0]==node.hi[0] || node.lo[1]==node.hi[1]
            || node.depth==bezierMaxDepth || nodes_.size()+2>bezierMaxNodes) continue;
        const unsigned axis=node.depth%2;
        const auto nets=split(node.controls,axis);
        const auto origin=node.origin,extent=node.extent;
        const unsigned depth=node.depth+1;
        const int first=int(nodes_.size()); node.children[0]=first; node.children[1]=first+1;
        for (unsigned side=0;side<2;++side) {
            Node child; child.controls=nets[side]; child.origin=origin; child.extent=extent;
            child.extent[axis]*=.5; child.origin[axis]+=side*child.extent[axis]; child.depth=depth;
            nodes_.push_back(child);
        }
        pending.push_back(size_t(first+1)); pending.push_back(size_t(first));
    }
}
BezierUVChart::~BezierUVChart()=default;
BezierUV BezierUVChart::mins() const { return nodes_.front().lo; }
BezierUV BezierUVChart::maxs() const { return nodes_.front().hi; }
size_t BezierUVChart::nodes() const { return nodes_.size(); }
size_t BezierUVChart::unresolvedRegions() const {
    return std::count_if(nodes_.begin(),nodes_.end(),[](const Node& n) { return n.children[0]<0 && !n.regular; });
}

BezierUVResult BezierUVChart::invert(const BezierUV& texel,const BezierWork& charge) const {
    for (double x:texel) if (!std::isfinite(x) || std::abs(x)>1e45) throw std::invalid_argument("Invalid Bezier UV query");
    BezierUVResult result;
    std::array<size_t,bezierMaxDepth+2> pending{}; size_t count=1;
    while (count) {
        const auto& node=nodes_[pending[--count]]; charge(1);
        if (texel[0]<node.lo[0] || texel[0]>node.hi[0] || texel[1]<node.lo[1] || texel[1]>node.hi[1]) continue;
        if (node.children[0]>=0) {
            pending[count++]=size_t(node.children[1]); pending[count++]=size_t(node.children[0]); continue;
        }
        if (!node.regular) {
            if (!outsideControlHull(node.controls,texel,charge)) result.unresolved=true;
            continue;
        }
        const IPoint target{exact(texel[0]),exact(texel[1])};
        const auto f=evaluate(node.controls,{.5,.5});
        const auto correction=multiply(node.preconditioner,{f[0]-target[0],f[1]-target[1]});
        IPoint enclosure;
        for (size_t a=0;a<2;++a)
            enclosure[a]=exact(.5)-correction[a]+node.remainder[a][0]*Interval{-.5,.5}+node.remainder[a][1]*Interval{-.5,.5};
        // The preconditioned mean-value enclosure contains every root in the
        // parameter square. A disjoint interval is an actual exclusion, whereas
        // a failed numerical solve below is explicitly unresolved coverage.
        if (enclosure[0].hi<0 || enclosure[0].lo>1 || enclosure[1].hi<0 || enclosure[1].lo>1) continue;
        BezierUV p{std::clamp(.5-midpoint(correction[0]),0.0,1.0),std::clamp(.5-midpoint(correction[1]),0.0,1.0)};
        const auto nominal=midpoints(node.controls);
        bool found=false;
        for (unsigned iteration=0;iteration<bezierMaxIterations;++iteration) {
            charge(1);
            const auto at=evaluate(node.controls,p);
            const IPoint residual{at[0]-target[0],at[1]-target[1]};
            const auto delta=multiply(node.preconditioner,residual);
            const double radius=up(std::max(magnitude(delta[0]),magnitude(delta[1]))/down(1-node.contraction));
            const double error=std::max(magnitude(residual[0]),magnitude(residual[1]));
            if (radius<=bezierParameterTolerance && error<=bezierUVTolerance) {
                if (result.roots.size()==bezierMaxRootHits) { result.unresolved=true; return result; }
                BezierUVRoot root; root.residual=error;
                for (size_t a=0;a<2;++a) {
                    root.parameter[a]=node.origin[a]+node.extent[a]*p[a];
                    const Interval range=exact(node.origin[a])+scale(Interval{down(p[a]-radius),up(p[a]+radius)},node.extent[a]);
                    root.radius[a]=up(std::max(root.parameter[a]-range.lo,range.hi-root.parameter[a]));
                    root.boundary|=p[a]-radius<=0 || p[a]+radius>=1;
                }
                result.roots.push_back(root); found=true; break;
            }
            const auto evaluated=evaluateBezier(nominal,p[0],p[1]); Matrix step;
            if (!inverse(evaluated.du,evaluated.dv,step)) break;
            BezierUV next;
            for (size_t a=0;a<2;++a)
                next[a]=std::clamp(p[a]-step[a][0]*(evaluated.value[0]-texel[0])-step[a][1]*(evaluated.value[1]-texel[1]),0.0,1.0);
            if (next==p) break;
            p=next;
        }
        if (!found && !outsideControlHull(node.controls,texel,charge)) result.unresolved=true;
    }
    return result;
}
}
