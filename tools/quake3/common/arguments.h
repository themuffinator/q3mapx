// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "inout.h"
#include <charconv>
#include <cstring>
#include <cmath>

inline int ParseIntegerOption( const char* option, const char* text, int minimum, int maximum ){
	const char* begin = text + ( text[0] == '+' );
	const char* end = text + std::strlen( text );
	int value = 0;
	const auto result = std::from_chars( begin, end, value );
	// from_chars accepts a minus; stripping a plus must not accept a second sign.
	if ( ( text[0] == '+' && text[1] == '-' )
	     || result.ec != std::errc{} || result.ptr != end || value < minimum || value > maximum ) {
		Error( "%s expects an integer in %d..%d, got '%s'", option, minimum, maximum, text );
	}
	return value;
}

inline double ParseDoubleOption( const char* option, const char* text, double minimum, double maximum ){
	const char* begin = text + ( text[0] == '+' );
	const char* end = text + std::strlen(text);
	double value = 0;
	const auto result = std::from_chars(begin, end, value);
	if ( ( text[0] == '+' && text[1] == '-' )
	     || result.ec != std::errc{} || result.ptr != end || !std::isfinite(value) || value < minimum || value > maximum )
		Error("%s expects a finite number in %g..%g, got '%s'", option, minimum, maximum, text);
	return value;
}

inline float ParseFloatOption( const char* option, const char* text, float minimum, float maximum ){
	return float(ParseDoubleOption(option,text,minimum,maximum));
}
