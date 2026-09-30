// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

struct DecompileOptions {
	enum class BrushOrder { Bsp, Rebuild };
	const char* output = nullptr;
	const char* report = nullptr;
	bool automaticReport = false;
	BrushOrder brushOrder = BrushOrder::Bsp;
};
inline DecompileOptions decompileOptions;
