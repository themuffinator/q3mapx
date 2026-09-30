// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/uv_fit.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string_view>
#include <cstdlib>

using namespace q3mapx;
static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") {
        size_t count;
        require(bool(std::cin >> count) && count <= 10'000, "Invalid oracle case count");
        std::cout << std::setprecision(17);
        for (size_t i = 0; i < count; ++i) {
            size_t n;
            require(bool(std::cin >> n) && n <= maxUVFitSamples, "Invalid oracle sample count");
            std::vector<UVSample> samples(n);
            for (auto& s : samples)
                require(bool(std::cin >> s.xy[0] >> s.xy[1] >> s.uv[0] >> s.uv[1] >> s.weight), "Invalid oracle sample");
            Affine2 matrix{{ {9,9,9}, {9,9,9} }};
            const auto fit = fitUVConsensus(samples, matrix);
            std::cout << uvFitStatusName(fit.status);
            for (const auto& row : matrix) for (double value : row) std::cout << ' ' << value;
            std::cout << ' ' << fit.rmsError << ' ' << fit.maxError << ' ' << fit.maxToleranceRatio << '\n';
        }
        return 0;
    }
    std::vector<UVSample> samples;
    for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x)
        samples.push_back({ {double(x), double(y)}, {0.5*x+0.25*y+7, -0.25*x+0.125*y-3}, double(x+4) });
    Affine2 matrix{};
    require(fitUVConsensus(samples, matrix).status == UVFitStatus::Consistent, "Exact affine fit rejected");
    require(std::abs(matrix[0][0]-.5) < 1e-12 && std::abs(matrix[1][2]+3) < 1e-12, "Wrong affine fit");
    const auto expected = matrix;
    samples.back().uv[0] += 1;
    require(fitUVConsensus(samples, matrix).status == UVFitStatus::Conflict && matrix == expected, "Seam averaged or failure modified matrix");
    samples.back().uv[0] -= 1;
    samples.front().weight = 0;
    require(fitUVConsensus(samples, matrix).status == UVFitStatus::Invalid, "Zero weight accepted");
    samples.front().weight = 1;
    samples.front().xy[0] = INFINITY;
    require(fitUVConsensus(samples, matrix).status == UVFitStatus::Invalid, "Infinite coordinate accepted");
    samples.front().xy[0] = -2;
    samples.front().uv[0] = NAN;
    require(fitUVConsensus(samples, matrix).status == UVFitStatus::Invalid, "NaN UV accepted");
    for (auto& s : samples) { s.xy[1] = s.xy[0]; s.uv[0] = s.xy[0]; }
    require(fitUVConsensus(samples, matrix).status == UVFitStatus::IllConditioned, "Collinear geometry accepted");
    for (size_t i = 0; i < samples.size(); ++i) { samples[i].xy[1] = double(i/5); samples[i].uv[0] = 0; }
    require(fitUVConsensus(samples, matrix).status == UVFitStatus::ConstantAxis, "Unsupported constant axis accepted");
    samples.assign(maxUVFitSamples+1, {});
    require(fitUVConsensus(samples, matrix).status == UVFitStatus::Limit, "Storage cap ignored");
    require(evaluateUVFit(samples, matrix).status == UVFitStatus::Limit, "Evaluation cap ignored");
    std::cout << "UV consensus: affine fit, seam rejection, finite inputs, conditioning and limits passed\n";
}
