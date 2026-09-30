// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace q3mapx {
struct ReductionMaterial {
    std::string name;
    std::string reason; // Empty only for the explicitly supported opaque grammar.
    bool lightmap=false;
    bool marksDisabled=false;
    bool dynamicLightsDisabled=false;
};
// Strict, bounded inventory of shader definitions, including unsupported ones.
// Does not resolve duplicate names or substitute implicit/default materials.
// Malformed structure throws; unknown behavior yields a protected material.
std::vector<ReductionMaterial> scanReductionMaterials(std::string_view source);
std::string materialKey(std::string_view name);
}
