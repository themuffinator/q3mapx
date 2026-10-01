// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
namespace q3mapx { class LightRecovery; }

struct DecompileOptions {
	enum class BrushOrder { Bsp, Rebuild };
	enum class DetailPolicy { Legacy, Cells };
	enum class GroupPolicy { None, Surfaces };
	enum class UVPolicy { Consensus, Triangle };
	enum class PatchColors { None, Alpha, RGBA };
	enum class PatchRecovery { None, Source, Fit, Auto };
	const char* output = nullptr;
	const char* report = nullptr;
	const char* lightProposals = nullptr;
	std::shared_ptr<q3mapx::LightRecovery> lightRecovery;
	bool automaticReport = false;
	BrushOrder brushOrder = BrushOrder::Bsp;
	DetailPolicy detailPolicy = DetailPolicy::Legacy;
	unsigned detailWorkLimit = 50'000'000;
	GroupPolicy groupPolicy = GroupPolicy::None;
	unsigned groupWorkLimit = 50'000'000;
	UVPolicy uvPolicy = UVPolicy::Consensus;
	PatchColors patchColors = PatchColors::None;
	int patchColorSubdivisions = 16;
	PatchRecovery patchRecovery = PatchRecovery::None;
	unsigned patchFitWorkLimit = 50'000'000;
};
inline DecompileOptions decompileOptions;
