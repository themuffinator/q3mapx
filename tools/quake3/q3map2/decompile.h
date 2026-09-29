// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

struct DecompileOptions {
	const char* output = nullptr;
	const char* report = nullptr;
	bool automaticReport = false;
};
inline DecompileOptions decompileOptions;
