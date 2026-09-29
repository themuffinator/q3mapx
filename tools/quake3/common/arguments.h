// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "inout.h"
#include <charconv>
#include <cstring>

inline int ParseIntegerOption( const char* option, const char* text, int minimum, int maximum ){
	const char* begin = text + ( text[0] == '+' );
	const char* end = text + std::strlen( text );
	int value = 0;
	const auto result = std::from_chars( begin, end, value );
	if ( result.ec != std::errc{} || result.ptr != end || value < minimum || value > maximum ) {
		Error( "%s expects an integer in %d..%d, got '%s'", option, minimum, maximum, text );
	}
	return value;
}
