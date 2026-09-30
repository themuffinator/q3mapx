// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "affine.h"
#include <span>

namespace q3mapx {
struct UVSample {
    Point2 xy{}, uv{};
    double weight = 1;
};
inline constexpr size_t maxUVFitSamples = 12'288;
enum class UVFitStatus { Consistent, Insufficient, Invalid, IllConditioned, ConstantAxis, Conflict, Limit };
const char* uvFitStatusName(UVFitStatus status);
struct UVFitResult {
    UVFitStatus status = UVFitStatus::Insufficient;
    double rmsError = 0, maxError = 0, maxToleranceRatio = 0;
};
// UV values are texture repeats, not pixels. The capped allowance accounts for
// binary32 arithmetic; it is not a probability or proof of original authoring.
double uvFitTolerance(double value);
UVFitResult evaluateUVFit(std::span<const UVSample> samples, const Affine2& matrix);
// Deterministic, area-weighted centered regression. Every sample must agree:
// conflicting charts are never averaged into an exported transform. Failure
// leaves result untouched. The sample cap bounds storage and sorting work.
UVFitResult fitUVConsensus(std::span<const UVSample> samples, Affine2& result);
}
