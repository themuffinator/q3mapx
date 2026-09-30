// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/bezier_uv.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>

int main() try {
    using namespace q3mapx;
    size_t count;
    if (!(std::cin>>count) || count>10'000) throw std::runtime_error("Invalid case count");
    std::cout<<std::setprecision(17);
    for (size_t i=0;i<count;++i) {
        BezierUVNet net; BezierUV target;
        for (auto& point:net) for (auto& value:point) if (!(std::cin>>value)) throw std::runtime_error("Invalid control net");
        if (!(std::cin>>target[0]>>target[1])) throw std::runtime_error("Invalid query");
        uint64_t work=0;
        const auto charge=[&](uint64_t n) { work+=n; if (work>1'000'000) throw std::runtime_error("Work limit"); };
        const BezierUVChart chart(net,charge);
        const auto result=chart.invert(target,charge);
        std::cout<<chart.nodes()<<' '<<chart.unresolvedRegions()<<' '<<work<<' '<<result.unresolved<<' '<<result.roots.size();
        for (const auto& root:result.roots)
            std::cout<<' '<<root.parameter[0]<<' '<<root.parameter[1]<<' '<<root.radius[0]<<' '<<root.radius[1]<<' '<<root.residual<<' '<<root.boundary;
        std::cout<<'\n';
    }
    return 0;
}
catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
