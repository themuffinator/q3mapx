// SPDX-License-Identifier: GPL-3.0-or-later
#include "uv_fit.h"
#include <vector>
#include <tuple>

namespace q3mapx {
const char* uvFitStatusName(UVFitStatus status) {
    switch (status) {
    case UVFitStatus::Consistent: return "consistent";
    case UVFitStatus::Insufficient: return "insufficient_samples";
    case UVFitStatus::Invalid: return "invalid_numeric_data";
    case UVFitStatus::IllConditioned: return "ill_conditioned";
    case UVFitStatus::ConstantAxis: return "constant_axis";
    case UVFitStatus::Conflict: return "conflicting_mappings";
    case UVFitStatus::Limit: return "sample_limit";
    }
    return "invalid_status";
}

double uvFitTolerance(double value) {
    return std::min(1.0 / 4096, std::max(1e-6,
        8 * double(std::numeric_limits<float>::epsilon()) * std::max(1.0, std::abs(value))));
}

static bool validSample(const UVSample& sample) {
    return std::isfinite(sample.weight) && sample.weight > 0
        && std::isfinite(sample.xy[0]) && std::isfinite(sample.xy[1])
        && std::isfinite(sample.uv[0]) && std::isfinite(sample.uv[1]);
}

UVFitResult evaluateUVFit(std::span<const UVSample> samples, const Affine2& matrix) {
    if (samples.size() > maxUVFitSamples) return {UVFitStatus::Limit};
    if (samples.empty()) return {};
    for (const auto& row : matrix) for (double value : row)
        if (!std::isfinite(value)) return {UVFitStatus::Invalid};
    double maxWeight = 0;
    for (const auto& sample : samples) {
        if (!validSample(sample)) return {UVFitStatus::Invalid};
        maxWeight = std::max(maxWeight, sample.weight);
    }
    UVFitResult result{UVFitStatus::Consistent};
    double squareError = 0, weights = 0;
    for (const auto& sample : samples) {
        const double weight = sample.weight / maxWeight;
        weights += weight;
        for (size_t axis = 0; axis < 2; ++axis) {
            const auto& row = matrix[axis];
            const double error = std::abs(row[0]*sample.xy[0] + row[1]*sample.xy[1] + row[2] - sample.uv[axis]);
            if (!std::isfinite(error)) return {UVFitStatus::Invalid};
            squareError += weight * error * error;
            result.maxError = std::max(result.maxError, error);
            result.maxToleranceRatio = std::max(result.maxToleranceRatio, error / uvFitTolerance(sample.uv[axis]));
        }
    }
    result.rmsError = std::sqrt(squareError / (2 * weights));
    if (!std::isfinite(result.rmsError) || !std::isfinite(result.maxToleranceRatio)) return {UVFitStatus::Invalid};
    if (result.maxToleranceRatio > 1) result.status = UVFitStatus::Conflict;
    return result;
}

UVFitResult fitUVConsensus(std::span<const UVSample> samples, Affine2& result) {
    if (samples.size() > maxUVFitSamples) return {UVFitStatus::Limit};
    if (samples.size() < 3) return {};
    for (const auto& sample : samples) if (!validSample(sample)) return {UVFitStatus::Invalid};
    std::vector<UVSample> ordered(samples.begin(), samples.end());
    std::sort(ordered.begin(), ordered.end(), [](const UVSample& a, const UVSample& b) {
        return std::tie(a.xy, a.uv, a.weight) < std::tie(b.xy, b.uv, b.weight);
    });
    const auto& reference = ordered.front();
    double maxWeight = 0;
    for (const auto& sample : ordered) maxWeight = std::max(maxWeight, sample.weight);
    Point2 meanXY{}, meanUV{};
    double weights = 0;
    for (const auto& sample : ordered) {
        const double weight = sample.weight / maxWeight;
        weights += weight;
        for (size_t axis = 0; axis < 2; ++axis) {
            meanXY[axis] += weight * (sample.xy[axis] - reference.xy[axis]);
            meanUV[axis] += weight * (sample.uv[axis] - reference.uv[axis]);
        }
    }
    for (size_t axis = 0; axis < 2; ++axis) {
        meanXY[axis] /= weights;
        meanUV[axis] /= weights;
        if (!std::isfinite(meanXY[axis]) || !std::isfinite(meanUV[axis])) return {UVFitStatus::Invalid};
    }
    double scale = 0;
    for (const auto& sample : ordered) for (size_t axis = 0; axis < 2; ++axis)
        scale = std::max(scale, std::abs((sample.xy[axis] - reference.xy[axis]) - meanXY[axis]));
    if (!std::isfinite(scale)) return {UVFitStatus::Invalid};
    if (scale == 0) return {UVFitStatus::IllConditioned};
    double xx = 0, xy = 0, yy = 0;
    Point2 xu{}, yu{};
    for (const auto& sample : ordered) {
        const double weight = sample.weight / maxWeight;
        const double x = ((sample.xy[0] - reference.xy[0]) - meanXY[0]) / scale;
        const double y = ((sample.xy[1] - reference.xy[1]) - meanXY[1]) / scale;
        xx += weight*x*x; xy += weight*x*y; yy += weight*y*y;
        for (size_t axis = 0; axis < 2; ++axis) {
            const double uv = (sample.uv[axis] - reference.uv[axis]) - meanUV[axis];
            xu[axis] += weight*x*uv; yu[axis] += weight*y*uv;
        }
    }
    const double determinant = xx*yy - xy*xy;
    if (!std::isfinite(determinant) || determinant <= (xx+yy)*(xx+yy)*1e-12)
        return {UVFitStatus::IllConditioned};
    Affine2 candidate{};
    for (size_t axis = 0; axis < 2; ++axis) {
        auto& row = candidate[axis];
        row[0] = (xu[axis]*yy - yu[axis]*xy) / determinant / scale;
        row[1] = (yu[axis]*xx - xu[axis]*xy) / determinant / scale;
        row[2] = reference.uv[axis] + meanUV[axis]
            - row[0]*(reference.xy[0] + meanXY[0]) - row[1]*(reference.xy[1] + meanXY[1]);
        for (double value : row)
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return {UVFitStatus::Invalid};
        if (std::hypot(row[0], row[1]) < 1e-12) return {UVFitStatus::ConstantAxis};
    }
    const UVFitResult checked = evaluateUVFit(ordered, candidate);
    if (checked.status == UVFitStatus::Consistent) result = candidate;
    return checked;
}
}
