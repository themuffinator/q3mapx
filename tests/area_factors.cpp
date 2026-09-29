// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/area_factors.h"
#include "lighting_math.h"
#include <cstdlib>
#include <iostream>
#include <random>

static void require(bool condition, const char* message){
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
static Vector3 xyz(const std::array<float,4>& p){ return { p[0],p[1],p[2] }; }

int main(int argc, char**){
    q3mapx::ComputeReport report;
    if (argc > 1) {
        require(!q3mapx::createAreaFactorComputer(-1,report),"Disabled GPU unexpectedly initialized");
        require(!report.reason.empty(),"GPU failure had no diagnostic");
        return 0;
    }
    std::string reason;
    const auto devices = q3mapx::computeDevices(reason);
    if (devices.empty()) { std::cout << "SKIP: " << reason << '\n'; return 77; }
    std::vector<q3mapx::AreaFactorSample> samples;
    std::vector<q3mapx::AreaFactorLight> lights;
    std::vector<q3mapx::AreaFactorVertex> vertices;
    std::mt19937 rng(9017);
    std::uniform_real_distribution<float> random(-1,1);
    for (size_t i=0; i<256; ++i) {
        Vector3 normal = VectorNormalized(Vector3(random(rng),random(rng),random(rng)));
        samples.push_back({ { random(rng)*256, random(rng)*256, random(rng)*256, i%13 == 0 ? 0.f : 1.f },
                           { normal[0],normal[1],normal[2],0 } });
    }
    for (float z : { 0.f,120.f,127.f,128.f,129.f,136.f,256.f })
        samples.push_back({ { 64,0,z,1 }, { 0,0,1,0 } });
    for (uint32_t n : { 0u,1u,2u,3u,4u,16u,64u,511u,512u,513u,1024u }) {
        lights.push_back({ { 0,0,128,384 }, { 0,0,1,128 }, { uint32_t(vertices.size()),n,0,0 } });
        for (uint32_t i=0; i<n; ++i) {
            double a = c_2pi*i/n;
            vertices.push_back({ float(64*std::cos(a)),float(64*std::sin(a)),128,0 });
        }
    }
    std::vector<float> expected;
    for (const auto& l : lights) for (const auto& s : samples) {
        Vector3 p = xyz(s.origin), delta = xyz(l.origin)-p;
        if (!s.origin[3] || VectorNormalize(delta) >= l.origin[3]) { expected.push_back(0); continue; }
        const float d = vector3_dot(p,xyz(l.plane))-l.plane[3];
        if (d > -8 && d < 8) p += xyz(l.plane)*(8.f-d);
        std::vector<Vector3> polygon;
        for (uint32_t i=0; i<l.winding[1]; ++i) polygon.push_back(xyz(vertices[l.winding[0]+i]));
        expected.push_back(q3mapx::polygonFormFactor(p,xyz(s.normal),polygon));
    }
    int tested = 0;
    for (const auto& device : devices) {
        // Native devices cover actual hardware; duplicate translation layers can
        // lack FP64 even when the native device supports it.
        if (device.vendor.find("Microsoft") != std::string::npos) continue;
        auto computer = q3mapx::createAreaFactorComputer(device.index,report);
        if (!computer && report.reason.find("double precision") != std::string::npos) {
            std::cout << device.name << ": unsupported FP64, fallback available\n"; continue;
        }
        if (!computer) std::cerr << report.reason << '\n';
        require(bool(computer),"Area-factor device initialization failed");
        std::vector<float> actual;
        float maxError = 0;
        for (int repeat=0; repeat<2; ++repeat) {
            if (!computer->compute(samples,lights,vertices,actual,report)) std::cerr << report.reason << '\n';
            require(report.usedGPU && actual.size() == expected.size(),"Area-factor dispatch failed");
            for (size_t i=0; i<actual.size(); ++i) {
                maxError = std::max(maxError,std::abs(actual[i]-expected[i]));
                require(std::isfinite(actual[i]) && std::abs(actual[i]-expected[i]) <= 1e-6f,"GPU polygon math differs from CPU");
            }
        }
        auto invalid = lights;
        invalid.back().winding[1] = uint32_t(vertices.size()+1);
        require(!computer->compute(samples,invalid,vertices,actual,report) && actual.empty(),"Invalid winding accepted");
        invalid = lights;
        invalid[0].plane[0] = std::numeric_limits<float>::quiet_NaN();
        require(!computer->compute(samples,invalid,vertices,actual,report),"Non-finite light accepted");
        require(!computer->compute({},lights,vertices,actual,report),"Empty batch accepted");
        const std::vector<q3mapx::AreaFactorLight> oversized(65536,lights[3]);
        require(!computer->compute(samples,oversized,vertices,actual,report),"Oversized result matrix accepted");
        require(computer->compute(samples,lights,vertices,actual,report),"Device failed to recover after rejected input");
        std::cout << device.name << ": maximum factor error " << maxError << ", bounded inputs/repeated batches passed\n";
        ++tested;
    }
    return tested ? 0 : 77;
}
