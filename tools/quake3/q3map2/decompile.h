// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

struct DecompileOptions {
	enum class BrushOrder { Bsp, Rebuild };
	enum class DetailPolicy { Legacy, Cells };
	enum class GroupPolicy { None, Surfaces };
	const char* output = nullptr;
	const char* report = nullptr;
	bool automaticReport = false;
	BrushOrder brushOrder = BrushOrder::Bsp;
	DetailPolicy detailPolicy = DetailPolicy::Legacy;
	unsigned detailWorkLimit = 50'000'000;
	GroupPolicy groupPolicy = GroupPolicy::None;
	unsigned groupWorkLimit = 50'000'000;
};
inline DecompileOptions decompileOptions;
