/* -------------------------------------------------------------------------------

   Copyright (C) 1999-2007 id Software, Inc. and contributors.
   For a list of contributors, see the accompanying CONTRIBUTORS file.

   This file is part of GtkRadiant.

   GtkRadiant is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   GtkRadiant is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GtkRadiant; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#pragma once

/* dependencies */
#include "q3map2.h"
#include <bit>
#include <type_traits>

// File offsets are validated independently of -force; it must never disable memory safety.
bspHeader_t ReadBSPHeader( const MemBuffer& file, int lumpCount );
void ValidateBSPStrings();
void ValidateBSPData( bool partial = false );

/*
   AddLump()
   adds a lump to an outgoing bsp file
 */
template<typename T>
void AddLump( FILE *file, bspLump_t& lump, const std::vector<T>& data ){
	const int length = sizeof( T ) * data.size();
	/* add lump to bsp file header */
	lump.offset = LittleLong( ftell( file ) );
	lump.length = LittleLong( length );

	/* write lump to file */
	SafeWrite( file, data.data(), length );

	/* write padding zeros */
	SafeWrite( file, std::array<byte, 3>{}.data(), ( ( length + 3 ) & ~3 ) - length );
}


/*
   CopyLump()
   copies a bsp file lump into a destination buffer
 */
template<typename DstT, typename SrcT = DstT>
void CopyLump( const bspHeader_t& header, const MemBuffer& file, int lump, std::vector<DstT>& data ){
	/* get lump length and offset */
	const int length = header.lumps[ lump ].length;
	const int offset = header.lumps[ lump ].offset;

	/* handle erroneous cases */
	if ( length <= 0 ) {
		data.clear();
		return;
	}
	if ( length % sizeof( SrcT ) ) {
		if ( force ) {
			Sys_Warning( "CopyLump: odd lump size (%d) in lump %d\n", length, lump );
			data.clear();
			return;
		}
		else{
			Error( "CopyLump: odd lump size (%d) in lump %d", length, lump );
		}
	}

	/* copy block of memory and return */
	static_assert( std::is_trivially_copyable_v<SrcT> );
	const byte* bytes = static_cast<byte*>( file.data() ) + offset;
	const size_t count = size_t( length ) / sizeof( SrcT );
	if constexpr ( std::is_same_v<DstT, SrcT> ) {
		data.resize( count );
		std::memcpy( data.data(), bytes, size_t( length ) );
	}
	else {
		// memcpy/bit_cast also handles deliberately unaligned lump offsets safely.
		data.clear();
		data.reserve( count );
		for ( size_t i = 0; i < count; ++i ) {
			std::array<byte, sizeof( SrcT )> element;
			std::memcpy( element.data(), bytes + i * sizeof( SrcT ), sizeof( SrcT ) );
			data.push_back( std::bit_cast<SrcT>( element ) );
		}
	}
}
