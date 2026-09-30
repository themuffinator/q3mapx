// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/index_validation.h"
#include <cstdlib>
#include <iostream>
#include <random>

static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

static std::optional<size_t> reference(std::span<const int> values, size_t first, size_t count, int vertices) {
    for (size_t i=first; i<first+count; ++i)
        if (values[i]<0 || values[i]>=vertices) return i;
    return std::nullopt;
}

int main() {
    // Empty and exact-end slices never dereference the source.
    q3mapx::IndexRangeValidator empty({});
    require(!empty.firstInvalid(0,0,0),"Empty span was rejected");
    bool rejected=false;
    try { empty.firstInvalid(0,1,1); } catch(const std::out_of_range&) { rejected=true; }
    require(rejected,"Out-of-range helper span was accepted");
    for(auto span : {std::pair{std::numeric_limits<size_t>::max(),size_t(0)},
                     std::pair{size_t(0),std::numeric_limits<size_t>::max()}}) {
        rejected=false;
        try { empty.firstInvalid(span.first,span.second,1); } catch(const std::out_of_range&) { rejected=true; }
        require(rejected,"Overflowing helper span was accepted");
    }
    rejected=false;
    try { empty.firstInvalid(0,0,-1); } catch(const std::invalid_argument&) { rejected=true; }
    require(rejected,"Negative local vertex count was accepted");

    // Exhaust all subranges on both sides of block boundaries, before and after
    // the adaptive lookup is built. Compare actual first errors, not extrema.
    for (size_t count : {1u,63u,64u,65u,127u,128u,129u,191u,257u}) {
        std::vector<int> values(count);
        for (size_t i=0; i<count; ++i) values[i]=int(i%11);
        values.back()=std::numeric_limits<int>::max();
        q3mapx::IndexRangeValidator checker(values);
        for (unsigned pass=0; pass<2; ++pass) {
            for (size_t first=0; first<=count; ++first)
                for (size_t end=first; end<=count; ++end)
                    for (int vertices : {0,1,7,11,std::numeric_limits<int>::max()})
                        require(checker.firstInvalid(first,end-first,vertices)==reference(values,first,end-first,vertices),"Exhaustive ordered error mismatch");
        }
    }

    std::mt19937 random(0x713478a1);
    for (unsigned trial=0; trial<80; ++trial) {
        const size_t n=128+random()%8192;
        std::vector<int> values(n);
        for (auto& value:values) value=int(random()%97);
        // Inject faults before constructing the checker; source data must stay
        // immutable for the lifetime of any cached range observations.
        if (trial%3==0) values[n/2]=std::numeric_limits<int>::min();
        if (trial%5==0) values[n-1]=std::numeric_limits<int>::max();
        q3mapx::IndexRangeValidator checker(values);
        for (unsigned query=0; query<1200; ++query) {
            const size_t first=random()%(n+1), count=random()%(n-first+1);
            const int limit=query%7==0 ? std::numeric_limits<int>::max() : int(random()%110);
            require(checker.firstInvalid(first,count,limit)==reference(values,first,count,limit),"Random shared-slice error mismatch");
        }
        require(checker.sourceValuesExamined()<=3*n+128*1200,"Source work grew with expanded index spans");
    }

    // An ordinary disjoint walk requires neither preprocessing nor storage.
    std::vector<int> indices(240'003,2);
    q3mapx::IndexRangeValidator disjoint(indices);
    for(size_t first=0; first<indices.size(); first+=3)
        require(!disjoint.firstInvalid(first,3,3),"Legal disjoint triangles rejected");
    require(disjoint.auxiliaryBytes()==0 && disjoint.sourceValuesExamined()==indices.size(),"Disjoint validation allocated or repeated work");

    // Vary the starting offset so caching identical requests cannot satisfy this
    // bound. Billions of implied references must require only linear source
    // preprocessing plus a bounded amount per surface, with modest scratch.
    q3mapx::IndexRangeValidator overlaps(indices);
    constexpr uint64_t queries=20'000;
    for(size_t i=0; i<queries; ++i)
        require(!overlaps.firstInvalid(i%127,indices.size()-256-i%127,3),"Legal overlapping indices rejected");
    const auto maximumWork=3*indices.size()+128*queries;
    require(overlaps.sourceValuesExamined()<=maximumWork,"Overlapping validation rescanned full spans");
    require(overlaps.auxiliaryBytes()>0 && overlaps.auxiliaryBytes()<indices.size()*sizeof(int)/16+16,"Validation scratch exceeds 6.25 percent of index storage");
    std::cout << "Ordered index checks passed; " << queries << " overlapping ranges examined "
              << overlaps.sourceValuesExamined() << " source values, " << overlaps.auxiliaryBytes() << " scratch bytes\n";
}
