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

   ----------------------------------------------------------------------------------

   This code has been altered significantly from its original form, to support
   several games based on the Quake III Arena engine, in the form of "Q3Map2."

   ------------------------------------------------------------------------------- */



/* dependencies */
#include "q3map2.h"
#include "bspfile_abstract.h"
#include "q3mapx/lightgrid.h"
#include <ctime>




/* -------------------------------------------------------------------------------

   this file handles translating the bsp file format used by quake 3, rtcw, and ef
   into the abstracted bsp file used by q3map2.

   ------------------------------------------------------------------------------- */

/* constants */
#define LUMP_ENTITIES       0
#define LUMP_SHADERS        1
#define LUMP_PLANES         2
#define LUMP_NODES          3
#define LUMP_LEAFS          4
#define LUMP_LEAFSURFACES   5
#define LUMP_LEAFBRUSHES    6
#define LUMP_MODELS         7
#define LUMP_BRUSHES        8
#define LUMP_BRUSHSIDES     9
#define LUMP_DRAWVERTS      10
#define LUMP_DRAWINDEXES    11
#define LUMP_FOGS           12
#define LUMP_SURFACES       13
#define LUMP_LIGHTMAPS      14
#define LUMP_LIGHTGRID      15
#define LUMP_VISIBILITY     16
#define LUMP_LIGHTARRAY     17
#define HEADER_LUMPS        18


/* types */
struct rbspHeader_t
{
	char ident[ 4 ];
	int version;

	bspLump_t lumps[ HEADER_LUMPS ];
};



static void CopyLightGridLumps( const bspHeader_t& header, const MemBuffer& file ){
	std::vector<bspGridPoint_t> gridPoints;
	std::vector<unsigned short> gridArray;
	CopyLump( header, file, LUMP_LIGHTGRID, gridPoints );
	CopyLump( header, file, LUMP_LIGHTARRAY, gridArray );

	bspGridPoints.clear();
	bspGridPoints.reserve( gridArray.size() );

	for( const auto rawId : gridArray ) {
		const auto id = uint16_t( LittleShort( rawId ) );
		if ( id >= gridPoints.size() ) Error( "Invalid BSP: lightgrid array index %u exceeds %zu entries", unsigned( id ), gridPoints.size() );
		bspGridPoints.push_back( gridPoints[ id ] );
	}
}


static void AddLightGridLumps( FILE *file, rbspHeader_t& header, q3mapx::PackedLightGrid<bspGridPoint_t>& grid ){
	/* swap array */
	for ( auto&& a : grid.indices )
		a = LittleShort( a );

	/* write lumps */
	AddLump( file, header.lumps[LUMP_LIGHTGRID], grid.points );
	AddLump( file, header.lumps[LUMP_LIGHTARRAY], grid.indices );
}



/*
   LoadRBSPFile()
   loads a raven bsp file into memory
 */

void LoadRBSPFile( const char *filename ){
	/* load the file */
	MemBuffer file = LoadFile( filename );

	const bspHeader_t header = ReadBSPHeader( file, 18 );

	/* make sure it matches the format we're trying to load */
	if ( !force && memcmp( header.ident, g_game->bspIdent, 4 ) ) {
		Error( "%s is not a %s file", filename, g_game->bspIdent );
	}
	if ( !force && header.version != g_game->bspVersion ) {
		Error( "%s is version %d, not %d", filename, header.version, g_game->bspVersion );
	}

	/* load/convert lumps */
	CopyLump( header, file, LUMP_SHADERS, bspShaders );
	CopyLump( header, file, LUMP_MODELS, bspModels );
	CopyLump( header, file, LUMP_PLANES, bspPlanes );
	CopyLump( header, file, LUMP_LEAFS, bspLeafs );
	CopyLump( header, file, LUMP_NODES, bspNodes );
	CopyLump( header, file, LUMP_LEAFSURFACES, bspLeafSurfaces );
	CopyLump( header, file, LUMP_LEAFBRUSHES, bspLeafBrushes );
	CopyLump( header, file, LUMP_BRUSHES, bspBrushes );
	CopyLump( header, file, LUMP_BRUSHSIDES, bspBrushSides );
	CopyLump( header, file, LUMP_DRAWVERTS, bspDrawVerts );
	CopyLump( header, file, LUMP_SURFACES, bspDrawSurfaces );
	CopyLump( header, file, LUMP_FOGS, bspFogs );
	CopyLump( header, file, LUMP_DRAWINDEXES, bspDrawIndexes );
	CopyLump( header, file, LUMP_VISIBILITY, bspVisBytes );
	CopyLump( header, file, LUMP_LIGHTMAPS, bspLightBytes );
	CopyLump( header, file, LUMP_ENTITIES, bspEntData );
	CopyLightGridLumps( header, file );
}



/*
   WriteRBSPFile()
   writes a raven bsp file
 */

void WriteRBSPFile( const char *filename ){
	rbspHeader_t header{};
	// Validate/pack before opening even the temporary output file. Grid records
	// contain bytes only, so SwapBSPFile does not change the comparison semantics.
	Sys_Printf( "Storing lightgrid: %zu points\n", bspGridPoints.size() );
	auto grid = q3mapx::packLightGrid<bspGridPoint_t>( bspGridPoints );

	//%	Swapfile();

	/* set up header */
	memcpy( header.ident, g_game->bspIdent, 4 );
	header.version = LittleLong( g_game->bspVersion );

	/* write initial header */
	FILE *file = SafeOpenWrite( filename );
	SafeWrite( file, &header, sizeof( header ) );    /* overwritten later */

	{ /* add marker lump */
		time_t t;
		time( &t );
		/* asctime adds an implicit trailing \n */
		const auto marker = StringStream( "I LOVE MY Q3MAP2 " Q3MAP_VERSION " on ", asctime( localtime( &t ) ) );
		AddLump( file, header.lumps[0], std::vector<char>( marker.cbegin(), marker.cend() + 1 ) );
	}

	/* add lumps */
	AddLump( file, header.lumps[LUMP_SHADERS], bspShaders );
	AddLump( file, header.lumps[LUMP_PLANES], bspPlanes );
	AddLump( file, header.lumps[LUMP_LEAFS], bspLeafs );
	AddLump( file, header.lumps[LUMP_NODES], bspNodes );
	AddLump( file, header.lumps[LUMP_BRUSHES], bspBrushes );
	AddLump( file, header.lumps[LUMP_BRUSHSIDES], bspBrushSides );
	AddLump( file, header.lumps[LUMP_LEAFSURFACES], bspLeafSurfaces );
	AddLump( file, header.lumps[LUMP_LEAFBRUSHES], bspLeafBrushes );
	AddLump( file, header.lumps[LUMP_MODELS], bspModels );
	AddLump( file, header.lumps[LUMP_DRAWVERTS], bspDrawVerts );
	AddLump( file, header.lumps[LUMP_SURFACES], bspDrawSurfaces );
	AddLump( file, header.lumps[LUMP_VISIBILITY], bspVisBytes );
	AddLump( file, header.lumps[LUMP_LIGHTMAPS], bspLightBytes );
	AddLightGridLumps( file, header, grid );
	AddLump( file, header.lumps[LUMP_ENTITIES], bspEntData );
	AddLump( file, header.lumps[LUMP_FOGS], bspFogs );
	AddLump( file, header.lumps[LUMP_DRAWINDEXES], bspDrawIndexes );

	/* emit bsp size */
	const int size = ftell( file );
	Sys_Printf( "Wrote %.1f MB (%d bytes)\n", (float) size / ( 1024 * 1024 ), size );

	/* write the completed header */
	if ( fseek( file, 0, SEEK_SET ) != 0 ) Error( "BSP header seek failed" );
	SafeWrite( file, &header, sizeof( header ) );

	/* close the file */
	SafeClose( file );
}
