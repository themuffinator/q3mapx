// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/portal_graph.h"
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

int main() try {
    const std::string winding="3 0 1 7 (+0 -0.0 1e1) (1 0 0) (0 1 0)\n";
    const std::string text="PRT1\r\n2\n1\n1\n"+winding+"3 0 (0 0 0)(0 1 0)(1 0 0)\n";
    const auto parsed=q3mapx::parsePortalGraph(text);
    if(parsed.clusters!=2 || parsed.portals.size()!=1 || parsed.faces.size()!=1 || parsed.pointCount!=6
        || parsed.portals[0].flags!=7 || parsed.portals[0].back!=1 || parsed.faces[0].back!=-1
        || !std::signbit(parsed.portals[0].points[0][1]) || parsed.portals[0].points[0][2]!=10)
        throw std::runtime_error("Valid PRT values or float bits changed");
    size_t rejected=0;
    const auto fails=[&](const std::string& value,const q3mapx::PortalLimits& limits=q3mapx::PortalLimits{}) {
        try { q3mapx::parsePortalGraph(value,limits); }
        catch(const std::exception&) { ++rejected; return; }
        throw std::runtime_error("Invalid portal input accepted: "+value.substr(0,100));
    };
    for(const auto& header: {"PRT2\n2\n1\n0\n","PRT1\n0\n1\n0\n","PRT1\n2\n-1\n0\n",
        "PRT1\n2\n65537\n0\n","PRT1\n2\n1\n999999999999999\n"}) fails(header+winding);
    for(const auto& polygon: {"2 0 1 0 (0 0 0)(1 0 0)","3 0 0 0 (0 0 0)(1 0 0)(0 1 0)",
        "3 0 2 0 (0 0 0)(1 0 0)(0 1 0)","3 0 1 -1 (0 0 0)(1 0 0)(0 1 0)",
        "3 0 1 0 (nan 0 0)(1 0 0)(0 1 0)","3 0 1 0 (1e999 0 0)(1 0 0)(0 1 0)",
        "3 0 1 0 (+-1 0 0)(1 0 0)(0 1 0)","3 0 1 0 (1x 0 0)(1 0 0)(0 1 0)",
        "3 0 1 0 (0 0 0)(1 0 0)(0 1 0", "3 0 1 0 0 0 0)(1 0 0)(0 1 0)"})
        fails(std::string("PRT1\n2\n1\n0\n")+polygon);
    fails(text+"unexpected");
    auto limits=q3mapx::PortalLimits{};
    limits.bytes=text.size()-1; fails(text,limits);
    limits={}; limits.points=5; fails(text,limits);
    limits={}; limits.windingsPerCluster=1; fails("PRT1\n2\n2\n0\n"+winding+winding,limits);
    // Every truncation before the last coordinate closes must be rejected.
    const size_t last=text.rfind(')');
    for(size_t i=0;i<=last;++i) fails(text.substr(0,i));
    std::mt19937 random(73);
    size_t floatChecks=0;
    for(int i=0;i<4000;++i) {
        const float value=std::bit_cast<float>(uint32_t(random()));
        if(!std::isfinite(value)) continue;
        for(const char* format:{"%.3f","%.9g"}) {
            char encoded[80]; std::snprintf(encoded,sizeof(encoded),format,double(value));
            float reference;
            if(std::sscanf(encoded,"%f",&reference)!=1) throw std::runtime_error("Reference float decode failed");
            const auto graph=q3mapx::parsePortalGraph(std::string("PRT1\n2\n1\n0\n3 0 1 0 (")+encoded+" 0 0)(1 0 0)(0 1 0)");
            if(std::bit_cast<uint32_t>(graph.portals[0].points[0][0])!=std::bit_cast<uint32_t>(reference))
                throw std::runtime_error("Decimal PRT float differs from legacy scanf: "+std::string(encoded));
            ++floatChecks;
        }
    }
    for(int i=0;i<2000;++i) {
        std::string value="PRT1\n2\n1\n0\n";
        for(unsigned j=0, count=random()%150;j<count;++j) value+=char(random()%128);
        fails(value);
    }
    std::cout << "PRT syntax, flags, budgets, " << floatChecks << " legacy float comparisons and " << rejected << " rejected inputs passed\n";
}
catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
