// SPDX-License-Identifier: GPL-3.0-or-later
#include "exact_predicates.h"
#include <algorithm>
#include <cfenv>
#include <cmath>
#include <limits>
#include <stdexcept>

#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ > 0)
#error Exact predicates cannot be built with fast-math
#endif

namespace q3mapx {
namespace {
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits==24);
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits==53);

// Accumulate terms as a nonoverlapping expansion using the rounding residual
// of each sum. No translated coordinates: distant exponent scales must not lose
// their low bits before the determinant is evaluated.
struct ExactSum {
    std::array<double,64> parts{};
    size_t size=0;
    void add(double value) {
        size_t out=0;
        for(size_t i=0;i<size;++i) {
            const double other=parts[i], sum=value+other;
            const double otherPart=sum-value, valuePart=sum-otherPart;
            const double error=(value-valuePart)+(other-otherPart);
            if(error!=0) parts[out++]=error;
            value=sum;
        }
        if(value!=0) parts[out++]=value;
        size=out;
    }
    int sign() const { return size==0?0:parts[size-1]>0?1:-1; }
};
template<size_t N> void finite(const std::array<float,N>& point) {
    for(float v:point) if(!std::isfinite(v)) throw std::runtime_error("Exact predicate requires finite binary32 coordinates");
}
void arithmetic() {
    if(std::fegetround()!=FE_TONEAREST) throw std::runtime_error("Exact predicate requires round-to-nearest arithmetic");
    volatile float smallest=std::numeric_limits<float>::denorm_min();
    if(double(smallest)!=0x1p-149) throw std::runtime_error("Exact predicate requires preserved binary32 subnormals");
}
}

int orient2Exact(const std::array<float,2>& a,const std::array<float,2>& b,const std::array<float,2>& c) {
    arithmetic(); finite(a); finite(b); finite(c);
    ExactSum sum;
    // Products of two binary32 values are exact in binary64, including products
    // involving binary32 subnormals. Six terms expand the homogeneous determinant.
    sum.add(double(a[0])*b[1]); sum.add(double(b[0])*c[1]); sum.add(double(c[0])*a[1]);
    sum.add(-double(a[1])*b[0]); sum.add(-double(b[1])*c[0]); sum.add(-double(c[1])*a[0]);
    return sum.sign();
}

int orient3Exact(const std::array<float,3>& a,const std::array<float,3>& b,
                 const std::array<float,3>& c,const std::array<float,3>& d) {
    arithmetic(); finite(a); finite(b); finite(c); finite(d);
    const std::array points{a,b,c,d};
    std::array<size_t,4> permutation{0,1,2,3};
    ExactSum sum;
    // Homogeneous 4x4 determinant: the fourth column is one, so each of its 24
    // terms has three binary32 factors. Fma retains the third product's exact
    // rounding residual. Their full exponent range fits normal binary64 values.
    do {
        unsigned inversions=0;
        for(size_t i=0;i<4;++i) for(size_t j=i+1;j<4;++j) inversions+=permutation[i]>permutation[j];
        const double first=double(points[permutation[0]][0])*points[permutation[1]][1];
        const double last=points[permutation[2]][2], product=first*last;
        const double residual=std::fma(first,last,-product);
        const double sign=inversions%2?-1:1;
        sum.add(sign*residual); sum.add(sign*product);
    } while(std::next_permutation(permutation.begin(),permutation.end()));
    return sum.sign();
}
}
