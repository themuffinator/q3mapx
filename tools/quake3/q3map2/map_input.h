// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scriplib.h"
#include "inout.h"
#include "vfs.h"
#include "stream/stringstream.h"
#include "authoring/surface.h"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

// Context belongs to the source primitive; never report compiled surface IDs.
struct MapInputReader
{
	const char *kind;
	int entity, primitive;
	int side = -1;

	[[noreturn]] void fail( const char *expected ) const {
		if ( side >= 0 ) {
			Error( "Invalid MAP %s (entity %d, primitive %d, side %d) at line %d in %s: expected %s, got '%s'",
			       kind, entity, primitive, side, scriptline, g_loadedScriptLocation.c_str(), expected, token );
		}
		Error( "Invalid MAP %s (entity %d, primitive %d) at line %d in %s: expected %s, got '%s'",
		       kind, entity, primitive, scriptline, g_loadedScriptLocation.c_str(), expected, token );
	}
	void next( bool crossline, const char *expected ) const {
		if ( !GetToken( crossline ) ) {
			Error( "Incomplete MAP %s (entity %d, primitive %d) at line %d in %s: expected %s before EOF",
			       kind, entity, primitive, scriptline, g_loadedScriptLocation.c_str(), expected );
		}
	}
	void match( const char *expected ) const {
		next( true, expected );
		if ( !TokenIs( expected ) ) fail( expected );
	}
	double number( const char *field ) const {
		next( false, field );
		const char *begin = token + ( token[0] == '+' );
		const char *end = token + std::strlen( token );
		double value = 0;
		const auto result = std::from_chars( begin, end, value );
		if ( result.ec != std::errc{} || result.ptr != end || !std::isfinite( value )
		  || ( token[0] == '+' && *begin == '-' ) ) fail( field );
		return value;
	}
	float coordinate( const char *field, double limit = std::numeric_limits<float>::max() ) const {
		const double value = number( field );
		if ( std::fabs( value ) > limit ) fail( field );
		const float stored = float( value );
		if ( value != 0 && stored == 0 ) fail( field );
		return stored;
	}
	std::uint32_t flags( const char *field ) const {
		next( false, field );
		const char *begin = token + ( token[0] == '+' );
		const char *end = token + std::strlen( token );
		std::int64_t value = 0;
		const auto result = std::from_chars( begin, end, value );
		// Editors use both signed and unsigned decimal spellings of the 32 bits.
		if ( result.ec != std::errc{} || result.ptr != end
		  || value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::uint32_t>::max()
		  || ( token[0] == '+' && *begin == '-' ) ) fail( field );
		return std::uint32_t( value );
	}
	int surfaceSampleSize() const {
		match( q3mapx::authoring::sampleSizeKey );
		next( false, "lightmap sample size: decimal integer in 0..16384 (0 inherits)" );
		int value;
		if ( !q3mapx::authoring::parseSampleSize( token, value ) )
			fail( "lightmap sample size: decimal integer in 0..16384 (0 inherits)" );
		return value;
	}
};
