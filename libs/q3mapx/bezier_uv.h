// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace q3mapx {
using BezierUV = std::array<double,2>;
using BezierUVNet = std::array<BezierUV,9>; // row-major, u across columns
using BezierWork = std::function<void(uint64_t)>;
inline constexpr unsigned bezierMaxDepth=10, bezierMaxNodes=1023, bezierMaxRootHits=64;
inline constexpr unsigned bezierMaxIterations=24;
inline constexpr double bezierUVTolerance=1e-7, bezierParameterTolerance=1e-9;
struct BezierUVRoot {
    BezierUV parameter{}, radius{};
    double residual=0;
    // Interior roots have a contraction enclosure inside their parameter region.
    // Boundary results meet residual/radius tolerances; exact coverage is unknown.
    bool boundary=false;
};
struct BezierUVResult {
    std::vector<BezierUVRoot> roots;
    bool unresolved=false;
};
class BezierUVChart {
public:
    explicit BezierUVChart(const BezierUVNet& controls,const BezierWork& charge);
    ~BezierUVChart();
    BezierUVChart(const BezierUVChart&)=delete;
    BezierUVChart& operator=(const BezierUVChart&)=delete;
    BezierUVResult invert(const BezierUV& texel,const BezierWork& charge) const;
    BezierUV mins() const;
    BezierUV maxs() const;
    size_t nodes() const;
    size_t unresolvedRegions() const;
private:
    struct Node;
    std::vector<Node> nodes_;
};

// Tensor biquadratic evaluation and derivatives. Inputs are control values, not
// tessellated surface vertices; useful for position, stored normal or paint fields.
template<size_t N> struct BezierValue { std::array<double,N> value{}, du{}, dv{}; };
template<size_t N> BezierValue<N> evaluateBezier(const std::array<std::array<double,N>,9>& net,double u,double v) {
    const std::array<double,3> bu{(1-u)*(1-u),2*u*(1-u),u*u};
    const std::array<double,3> bv{(1-v)*(1-v),2*v*(1-v),v*v};
    const std::array<double,2> lu{1-u,u},lv{1-v,v};
    BezierValue<N> out;
    for (size_t y=0;y<3;++y) for (size_t x=0;x<3;++x) for (size_t a=0;a<N;++a) {
        out.value[a]+=net[y*3+x][a]*bu[x]*bv[y];
    }
    // Difference control nets avoid translation cancellation and give exactly
    // zero tangents for constant geometry, rather than normalizing roundoff.
    for(size_t y=0;y<3;++y) for(size_t x=0;x<2;++x) for(size_t a=0;a<N;++a)
        out.du[a]+=2*(net[y*3+x+1][a]-net[y*3+x][a])*lu[x]*bv[y];
    for(size_t y=0;y<2;++y) for(size_t x=0;x<3;++x) for(size_t a=0;a<N;++a)
        out.dv[a]+=2*(net[(y+1)*3+x][a]-net[y*3+x][a])*bu[x]*lv[y];
    return out;
}
}
