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

   -------------------------------------------------------------------------------

   This code has been altered significantly from its original form, to support
   several games based on the Quake III Arena engine, in the form of "Q3Map2."

   ------------------------------------------------------------------------------- */



/* dependencies */
#include "q3map2.h"
#include "decompile.h"
#include "light_recovery.h"
#include "export_mesh.h"



inline void AAS_DData( unsigned char *data, int size ){
	for ( int i = 0; i < size; ++i )
		data[i] ^= (unsigned char) i * 119;
}

/*
   FixAAS()
   resets an aas checksum to match the given BSP
 */

int FixAAS( Args& args ){
	/* arg checking */
	if ( args.empty() ) {
		Sys_Printf( "Usage: q3map2 -fixaas [-v] <mapname>\n" );
		return 0;
	}

	/* do some path mangling */
	strcpy( source, ExpandArg( args.takeBack() ) );
	path_set_extension( source, ".bsp" );

	/* note it */
	Sys_Printf( "--- FixAAS ---\n" );

	/* load the bsp */
	Sys_Printf( "Loading %s\n", source );
	MemBuffer buffer = LoadFile( source );

	/* create bsp checksum */
	Sys_Printf( "Creating checksum...\n" );
	int checksum = LittleLong( (int)Com_BlockChecksum( buffer.data(), buffer.size() ) ); // md4 checksum for a block of data
	AAS_DData( (unsigned char *) &checksum, 4 );

	/* write checksum to aas */
	for( auto&& ext : { ".aas", "_b0.aas", "_b1.aas" } )
	{
		/* mangle name */
		char aas[ 1024 ];
		strcpy( aas, source );
		path_set_extension( aas, ext );
		Sys_Printf( "Trying %s\n", aas );

		/* fix it */
		FILE *file = fopen( aas, "r+b" );
		if ( !file ) {
			continue;
		}
		if ( fseek( file, 8, SEEK_SET ) != 0 || fwrite( &checksum, 4, 1, file ) != 1 ) {
			Error( "Error writing checksum to %s", aas );
		}
		fclose( file );
	}

	/* return to sender */
	return 0;
}



/*
   AnalyzeBSP() - ydnar
   analyzes a Quake engine BSP file
 */

struct abspLumpTest_t
{
	int radix, minCount;
	const char     *name;
};

int AnalyzeBSP( Args& args ){
	int i, version, offset, length, lumpInt, count;
	char ident[ 5 ];
	void                    *lump;
	float lumpFloat;
	char lumpString[ 1024 ], source[ 1024 ];
	bool lumpSwap = false;
	abspLumpTest_t          *lumpTest;
	static abspLumpTest_t lumpTests[] =
	{
		{ sizeof( bspPlane_t ),         6,      "IBSP LUMP_PLANES" },
		{ sizeof( bspBrush_t ),         1,      "IBSP LUMP_BRUSHES" },
		{ 8,                            6,      "IBSP LUMP_BRUSHSIDES" },
		{ sizeof( bspBrushSide_t ),     6,      "RBSP LUMP_BRUSHSIDES" },
		{ sizeof( bspModel_t ),         1,      "IBSP LUMP_MODELS" },
		{ sizeof( bspNode_t ),          2,      "IBSP LUMP_NODES" },
		{ sizeof( bspLeaf_t ),          1,      "IBSP LUMP_LEAFS" },
		{ 104,                          3,      "IBSP LUMP_DRAWSURFS" },
		{ 44,                           3,      "IBSP LUMP_DRAWVERTS" },
		{ 4,                            6,      "IBSP LUMP_DRAWINDEXES" },
		{ 128 * 128 * 3,                1,      "IBSP LUMP_LIGHTMAPS" },
		{ 256 * 256 * 3,                1,      "IBSP LUMP_LIGHTMAPS (256 x 256)" },
		{ 512 * 512 * 3,                1,      "IBSP LUMP_LIGHTMAPS (512 x 512)" },
		{ 0, 0, nullptr }
	};


	/* arg checking */
	if ( args.empty() ) {
		Sys_Printf( "Usage: q3map2 -analyze [-lumpswap] [-v] <mapname>\n" );
		return 0;
	}

	/* process arguments */
	while ( args.takeArg( "-lumpswap" ) ) {
		Sys_Printf( "Swapped lump structs enabled\n" );
		lumpSwap = true;
	}

	/* clean up map name */
	strcpy( source, ExpandArg( args.takeBack() ) );
	Sys_Printf( "Loading %s\n", source );

	/* load the file */
	MemBuffer file = LoadFile( source );
	if ( file.size() < 8 ) Error( "Invalid BSP analysis: truncated signature/version" );
	const auto* bytes = static_cast<const byte*>(file.data());

	/* analyze ident/version */
	memcpy( ident, bytes, 4 );
	ident[ 4 ] = '\0';
	std::memcpy( &version, bytes + 4, sizeof(version) );
	version = LittleLong( version );

	Sys_Printf( "Identity:      %s\n", ident );
	Sys_Printf( "Version:       %d\n", version );
	Sys_Printf( "---------------------------------------\n" );

	// Unknown formats still get the traditional bounded heuristic analysis.
	// Stop before the earliest payload; copy unaligned entries and scalar data.
	// Native layouts should use -inspect for reliable directory names/counts.
	size_t firstPayload = file.size();
	/* analyze each lump */
	for ( i = 0; i < 100; ++i )
	{
		const size_t entryOffset = 8 + size_t(i) * 8;
		if ( entryOffset == firstPayload ) break;
		if ( entryOffset > firstPayload || firstPayload - entryOffset < 8 )
			Error( "Invalid BSP analysis: truncated directory entry %d", i );
		bspLump_t entry;
		std::memcpy( &entry, bytes + entryOffset, sizeof(entry) );
		/* call of duty swapped lump pairs */
		if ( lumpSwap ) {
			offset = LittleLong( entry.length );
			length = LittleLong( entry.offset );
		}

		/* standard lump pairs */
		else
		{
			offset = LittleLong( entry.offset );
			length = LittleLong( entry.length );
		}
		if ( offset < 0 || length < 0 || size_t(offset) > file.size()
		  || size_t(length) > file.size() - size_t(offset)
		  || ( length && size_t(offset) < entryOffset + 8 ) )
			Error( "Invalid BSP analysis: lump %d range (%d, %d) is outside the file or overlaps the directory", i, offset, length );
		if ( length ) firstPayload = std::min(firstPayload, size_t(offset));

		/* extract data */
		lump = static_cast<byte*>(file.data()) + offset;
		lumpInt = 0; lumpFloat = 0;
		if ( length >= 4 ) {
			std::memcpy( &lumpInt, lump, sizeof(lumpInt) );
			std::memcpy( &lumpFloat, lump, sizeof(lumpFloat) );
			lumpInt = LittleLong(lumpInt); lumpFloat = LittleFloat(lumpFloat);
		}
		const size_t stringBytes = std::min(size_t(length), std::size(lumpString)-1);
		memcpy( lumpString, lump, stringBytes );
		lumpString[stringBytes] = '\0';

		/* print basic lump info */
		Sys_Printf( "Lump:          %d\n", i );
		Sys_Printf( "Offset:        %d bytes\n", offset );
		Sys_Printf( "Length:        %d bytes\n", length );

		/* only operate on valid lumps */
		if ( length > 0 ) {
			/* print data in 4 formats */
			if ( length >= 4 ) {
				Sys_Printf( "As hex:        %08X\n", lumpInt );
				Sys_Printf( "As int:        %d\n", lumpInt );
				Sys_Printf( "As float:      %f\n", lumpFloat );
			}
			Sys_Printf( "As string:     %s\n", lumpString );

			/* guess lump type */
			if ( stringBytes >= 3 && lumpString[ 0 ] == '{' && lumpString[ 2 ] == '"' ) {
				Sys_Printf( "Type guess:    IBSP LUMP_ENTITIES\n" );
			}
			else if ( strstr( lumpString, "textures/" ) ) {
				Sys_Printf( "Type guess:    IBSP LUMP_SHADERS\n" );
			}
			else
			{
				/* guess based on size/count */
				for ( lumpTest = lumpTests; lumpTest->radix > 0; ++lumpTest )
				{
					if ( ( length % lumpTest->radix ) != 0 ) {
						continue;
					}
					count = length / lumpTest->radix;
					if ( count < lumpTest->minCount ) {
						continue;
					}
					Sys_Printf( "Type guess:    %s (%d x %d)\n", lumpTest->name, count, lumpTest->radix );
				}
			}
		}

		Sys_Printf( "---------------------------------------\n" );

		/* end of file */
		if ( length && size_t(offset) + size_t(length) == file.size() ) {
			++i;
			break;
		}
	}

	/* last stats */
	Sys_Printf( "Lump count:    %d\n", i );
	Sys_Printf( "File size:     %zu bytes\n", file.size() );

	/* return to caller */
	return 0;
}



/*
   BSPInfo()
   emits statistics about the bsp file
 */

int BSPInfo( Args& args ){
	char source[ 1024 ];


	/* dummy check */
	if ( args.empty() ) {
		Sys_Printf( "No files to dump info for.\n" );
		return -1;
	}

	/* walk file list */
	while ( !args.empty() )
	{
		Sys_Printf( "---------------------------------\n" );

		/* mangle filename and get size */
		const char *fileName = args.takeFront();
		strcpy( source, fileName );
		path_set_extension( source, ".bsp" );
		int size = 0;
		if ( FILE *f = fopen( source, "rb" ); f != nullptr ) {
			size = Q_filelength( f );
			fclose( f );
		}

		/* load the bsp file and print lump sizes */
		Sys_Printf( "%s\n", source );
		LoadBSPFile( source );
		PrintBSPFileSizes();

		/* print sizes */
		Sys_Printf( "\n" );
		Sys_Printf( "          total         %9d\n", size );
		Sys_Printf( "                        %9d KB\n", size / 1024 );
		Sys_Printf( "                        %9d MB\n", size / ( 1024 * 1024 ) );

		Sys_Printf( "---------------------------------\n" );
	}

	return 0;
}


static void ExtrapolateTexcoords( const bspDrawVert_t& a,
                                  const bspDrawVert_t& b,
								  const bspDrawVert_t& c,
								  bspDrawVert_t& anew,
								  bspDrawVert_t& bnew,
								  bspDrawVert_t& cnew ){

	const Vector3 norm = vector3_cross( b.xyz - a.xyz, c.xyz - a.xyz );

	// assume:
	//   s = f(x, y, z)
	//   s(v + norm) = s(v) when n ortho xyz

	// s(v) = DotProduct(v, scoeffs) + scoeffs[3]

	// solve:
	//   scoeffs * (axyz, 1) == ast[0]
	//   scoeffs * (bxyz, 1) == bst[0]
	//   scoeffs * (cxyz, 1) == cst[0]
	//   scoeffs * (norm, 0) == 0
	// scoeffs * [axyz, 1 | bxyz, 1 | cxyz, 1 | norm, 0] = [ast[0], bst[0], cst[0], 0]
	Matrix4 solvematrix;
	solvematrix.x() = { a.xyz, 1 };
	solvematrix.y() = { b.xyz, 1 };
	solvematrix.z() = { c.xyz, 1 };
	solvematrix.t() = { norm, 0 };
	matrix4_transpose( solvematrix );

	const double md = matrix4_determinant( solvematrix );
	if ( md * md < 1e-10 ) {
		Sys_Printf( "Cannot invert some matrix, some texcoords aren't extrapolated!" );
		return;
	}

	matrix4_full_invert( solvematrix );

	Matrix4 stcoeffs( g_matrix4_identity );
	stcoeffs.x() = { a.st[0], b.st[0], c.st[0], 0 };
	stcoeffs.y() = { a.st[1], b.st[1], c.st[1], 0 };
	matrix4_premultiply_by_matrix4( stcoeffs, solvematrix );
	matrix4_transpose( stcoeffs );
	anew.st = matrix4_transformed_point( stcoeffs, anew.xyz ).vec2();
	bnew.st = matrix4_transformed_point( stcoeffs, bnew.xyz ).vec2();
	cnew.st = matrix4_transformed_point( stcoeffs, cnew.xyz ).vec2();
}

/*
   ScaleBSPMain()
   amaze and confuse your enemies with weird scaled maps!
 */

int ScaleBSPMain( Args& args ){
	float f, a;
	Vector3 scale;
	Vector3 vec;
	int axis;
	bool texscale;
	DrawVerts old_xyzst;
	float spawn_ref = 0;


	/* arg checking */
	if ( args.size() < 2 ) {
		Sys_Printf( "Usage: q3map2 [-v] -scale [-tex] [-spawn_ref <value>] <value> <mapname>\n" );
		return 0;
	}

	texscale = false;
	const char *fileName = args.takeBack();
	const auto argsToInject = args.getVector();
	{
		if ( args.takeArg( "-tex" ) ) {
			texscale = true;
		}
		if ( args.takeArg( "-spawn_ref" ) ) {
			spawn_ref = args.takeDouble();
		}
	}

	/* get scale */
	scale[2] = scale[1] = scale[0] = atof( args.takeBack() );
	if ( !args.empty() ) {
		scale[1] = scale[0] = atof( args.takeBack() );
	}
	if ( !args.empty() ) {
		scale[0] = atof( args.takeBack() );
	}

	if ( scale == g_vector3_identity ) {
		Sys_Printf( "Usage: q3map2 [-v] -scale [-tex] [-spawn_ref <value>] <value> <mapname>\n" );
		Sys_Printf( "Non-zero scale value required.\n" );
		return 0;
	}

	/* do some path mangling */
	strcpy( source, ExpandArg( fileName ) );
	path_set_extension( source, ".bsp" );

	/* load the bsp */
	Sys_Printf( "Loading %s\n", source );
	LoadBSPFile( source );
	ParseEntities();

	/* note it */
	Sys_Printf( "--- ScaleBSP ---\n" );
	Sys_FPrintf( SYS_VRB, "%9zu entities\n", entities.size() );

	/* scale entity keys */
	for ( auto& e : entities )
	{
		/* scale origin */
		if ( e.read_keyvalue( vec, "origin" ) ) {
			if ( e.classname_prefixed( "info_player_" ) ) {
				vec[2] += spawn_ref;
			}
			vec *= scale;
			if ( e.classname_prefixed( "info_player_" ) ) {
				vec[2] -= spawn_ref;
			}
			e.setKeyValue( "origin", vec );
		}

		a = e.floatForKey( "angle" );
		if ( a == -1 || a == -2 ) { // z scale
			axis = 2;
		}
		else if ( std::fabs( sin( degrees_to_radians( a ) ) ) < 0.707 ) {
			axis = 0;
		}
		else{
			axis = 1;
		}

		/* scale door lip */
		if ( e.read_keyvalue( f, "lip" ) ) {
			f *= scale[axis];
			e.setKeyValue( "lip", f );
		}

		/* scale plat height */
		if ( e.read_keyvalue( f, "height" ) ) {
			f *= scale[2];
			e.setKeyValue( "height", f );
		}

		// TODO maybe allow a definition file for entities to specify which values are scaled how?
	}

	/* scale models */
	for ( auto& model : bspModels )
	{
		model.minmax.mins *= scale;
		model.minmax.maxs *= scale;
	}

	/* scale nodes */
	for ( bspNode_t& node : bspNodes )
	{
		node.minmax.mins = scale * node.minmax.mins; // this multiplication order to calculate in floats
		node.minmax.maxs = scale * node.minmax.maxs;
	}

	/* scale leafs */
	for ( bspLeaf_t& leaf : bspLeafs )
	{
		leaf.minmax.mins = scale * leaf.minmax.mins;
		leaf.minmax.maxs = scale * leaf.minmax.maxs;
	}

	/* scale patch lodbounds */
	for ( bspDrawSurface_t& surf : bspDrawSurfaces )
	{
		if ( surf.surfaceType == MST_PATCH ){
			surf.lightmapVecs[0] *= scale;
			surf.lightmapVecs[1] *= scale;
		}
	}

	if ( texscale ) {
		Sys_Printf( "Using texture unlocking (and probably breaking texture alignment a lot)\n" );
		old_xyzst = bspDrawVerts;
	}

	/* scale drawverts */
	for ( bspDrawVert_t& vert : bspDrawVerts )
	{
		vert.xyz *= scale;
		vert.normal /= scale;
		VectorNormalize( vert.normal );
	}

	if ( texscale ) {
		for ( const bspDrawSurface_t& surf : bspDrawSurfaces )
		{
			switch ( surf.surfaceType )
			{
			case MST_PLANAR:
				if ( surf.numIndexes % 3 ) {
					Error( "Not a triangulation!" );
				}
				for ( int j = surf.firstIndex; j < surf.firstIndex + surf.numIndexes; j += 3 )
				{
					const int ia = bspDrawIndexes[j + 0] + surf.firstVert,
					          ib = bspDrawIndexes[j + 1] + surf.firstVert,
							  ic = bspDrawIndexes[j + 2] + surf.firstVert;
					// extrapolate:
					//   a->xyz -> oa
					//   b->xyz -> ob
					//   c->xyz -> oc
					ExtrapolateTexcoords(
					    old_xyzst[ia],
					    old_xyzst[ib],
					    old_xyzst[ic],
					    bspDrawVerts[ia],
					    bspDrawVerts[ib],
					    bspDrawVerts[ic] );
				}
				break;
			default:
				break;
			}
		}
	}

	/* scale planes */
	if ( ( scale[0] == scale[1] ) && ( scale[1] == scale[2] ) ) { // uniform scale
		for ( bspPlane_t& plane : bspPlanes )
		{
			plane.dist() *= scale[0];
		}
	}
	else
	{
		for ( bspPlane_t& plane : bspPlanes )
		{
			plane.normal() /= scale;
			const double len = vector3_length( plane.normal() );
			plane.normal() /= len;
			plane.dist() /= len;
		}
	}

	/* scale gridsize */
	if ( !entities[ 0 ].read_keyvalue( vec, "gridsize" ) ) {
		vec = gridSize;
	}
	vec *= scale;
	entities[ 0 ].setKeyValue( "gridsize", vec );

	/* inject command line parameters */
	InjectCommandLine( "-scale", argsToInject );

	/* write the bsp */
	UnparseEntities();
	path_set_extension( source, "_s.bsp" );
	WriteBSPFile( source );

	/* return to sender */
	return 0;
}


/*
   ShiftBSPMain()
   shifts a map: for testing physics with huge coordinates
 */

int ShiftBSPMain( Args& args ){
	Vector3 shift;
	Vector3 vec;


	/* arg checking */
	if ( args.size() < 2 ) {
		Sys_Printf( "Usage: q3map2 [-v] -shift <value> <mapname>\n" );
		return 0;
	}

	const char *fileName = args.takeBack();
	const auto argsToInject = args.getVector();

	/* get shift */
	shift[2] = shift[1] = shift[0] = atof( args.takeBack() );
	if ( !args.empty() ) {
		shift[1] = shift[0] = atof( args.takeBack() );
	}
	if ( !args.empty() ) {
		shift[0] = atof( args.takeBack() );
	}


	/* do some path mangling */
	strcpy( source, ExpandArg( fileName ) );
	path_set_extension( source, ".bsp" );

	/* load the bsp */
	Sys_Printf( "Loading %s\n", source );
	LoadBSPFile( source );
	ParseEntities();

	/* note it */
	Sys_Printf( "--- ShiftBSP ---\n" );
	Sys_FPrintf( SYS_VRB, "%9zu entities\n", entities.size() );

	/* shift entity keys */
	for ( auto& e : entities )
	{
		/* shift origin */
		if ( e.read_keyvalue( vec, "origin" ) ) { // fixme: this doesn't consider originless point entities; group entities with origin will be wrong too
			vec += shift;
			e.setKeyValue( "origin", vec );
		}
	}

	/* shift models */
	for ( auto& model : bspModels )
	{
		model.minmax.mins += shift;
		model.minmax.maxs += shift;
	}

	/* shift nodes */
	for ( bspNode_t& node : bspNodes )
	{
		node.minmax.mins += shift;
		node.minmax.maxs += shift;
	}

	/* shift leafs */
	for ( bspLeaf_t& leaf : bspLeafs )
	{
		leaf.minmax.mins += shift;
		leaf.minmax.maxs += shift;
	}

	/* shift patch lodbounds */
	for ( bspDrawSurface_t& surf : bspDrawSurfaces )
	{
		if ( surf.surfaceType == MST_PATCH ){
			surf.lightmapVecs[0] += shift;
			surf.lightmapVecs[1] += shift;
		}
	}

	/* shift drawverts */
	for ( bspDrawVert_t& vert : bspDrawVerts )
	{
		vert.xyz += shift;
	}

	/* shift planes */
	for ( bspPlane_t& plane : bspPlanes )
	{
		plane = plane3_translated( plane, shift );
	}

	// fixme: engine says 'light grid mismatch', unless translation is multiple of grid size

	/* inject command line parameters */
	InjectCommandLine( "-shift", argsToInject );

	/* write the bsp */
	UnparseEntities();
	path_set_extension( source, "_sh.bsp" );
	WriteBSPFile( source );

	/* return to sender */
	return 0;
}


/*
   MergeBSPMain()
   merges two bsps
 */

int MergeBSPMain( Args& args ){
	/* arg checking */
	if ( args.size() < 2 ) {
		Sys_Printf( "Usage: q3map2 [-v] -mergebsp [-fixnames] [-world] <mainBsp> <bspToinject>\n" );
		return 0;
	}

	const char *fileName2 = args.takeBack();
	const char *fileName1 = args.takeBack();
	const auto argsToInject = args.getVector();

	const bool fixnames = args.takeArg( "-fixnames" );
	const bool addworld = args.takeArg( "-world" );

	/* do some path mangling */
	strcpy( source, ExpandArg( fileName2 ) );
	path_set_extension( source, ".bsp" );

	/* load the bsp */
	Sys_Printf( "Loading %s\n", source );
	LoadBSPFile( source );
	ParseEntities();

	struct
	{
		decltype( ::entities        ) entities        = std::move( ::entities );
		decltype( ::bspModels       ) bspModels       = std::move( ::bspModels );
		decltype( ::bspShaders      ) bspShaders      = std::move( ::bspShaders );
		decltype( ::bspLeafs        ) bspLeafs        = std::move( ::bspLeafs );
		decltype( ::bspPlanes       ) bspPlanes       = std::move( ::bspPlanes );
		decltype( ::bspNodes        ) bspNodes        = std::move( ::bspNodes );
		decltype( ::bspLeafSurfaces ) bspLeafSurfaces = std::move( ::bspLeafSurfaces );
		decltype( ::bspLeafBrushes  ) bspLeafBrushes  = std::move( ::bspLeafBrushes );
		decltype( ::bspBrushes      ) bspBrushes      = std::move( ::bspBrushes );
		decltype( ::bspBrushSides   ) bspBrushSides   = std::move( ::bspBrushSides );
		decltype( ::bspLightBytes   ) bspLightBytes   = std::move( ::bspLightBytes );
		decltype( ::bspGridPoints   ) bspGridPoints   = std::move( ::bspGridPoints );
		decltype( ::bspVisBytes     ) bspVisBytes     = std::move( ::bspVisBytes );
		decltype( ::bspDrawVerts    ) bspDrawVerts    = std::move( ::bspDrawVerts );
		decltype( ::bspDrawIndexes  ) bspDrawIndexes  = std::move( ::bspDrawIndexes );
		decltype( ::bspDrawSurfaces ) bspDrawSurfaces = std::move( ::bspDrawSurfaces );
		decltype( ::bspFogs         ) bspFogs         = std::move( ::bspFogs );
	} bsp;

	/* do some path mangling */
	strcpy( source, ExpandArg( fileName1 ) );
	path_set_extension( source, ".bsp" );

	/* load the bsp */
	Sys_Printf( "Loading %s\n", source );
	LoadBSPFile( source );
	ParseEntities();


	/* reindex */
	{
		for( auto&& model : bsp.bspModels )
		{
			model.firstBSPSurface += bspDrawSurfaces.size();
			model.firstBSPBrush += bspBrushes.size();
		}

		for( auto&& side : bsp.bspBrushSides ){
			side.planeNum += bspPlanes.size();
			side.shaderNum += bspShaders.size();
			side.surfaceNum += bspDrawSurfaces.size();
		}

		for( auto&& brush : bsp.bspBrushes )
		{
			brush.shaderNum += bspShaders.size();
			brush.firstSide += bspBrushSides.size();
		}

		for( auto&& fog : bsp.bspFogs )
			fog.brushNum += bspBrushes.size();

		/* deduce max lm index, using bspLightBytes is insufficient for native external lightmaps */
		int maxLmIndex = LIGHTMAP_BY_VERTEX;
		for( const auto& surf : bspDrawSurfaces )
			for( auto index : surf.lightmapNum )
				value_maximize( maxLmIndex, index );
		for( auto&& surf : bsp.bspDrawSurfaces )
		{
			surf.shaderNum += bspShaders.size();
			surf.fogNum += bspFogs.size();
			surf.firstVert += bspDrawVerts.size();
			surf.firstIndex += bspDrawIndexes.size();
			for( auto&& index : surf.lightmapNum )
				if( index >= 0 && maxLmIndex >= 0 )
					index += maxLmIndex + 1;
		}

		ENSURE( bsp.entities[0].classname_is( "worldspawn" ) );
		for( auto&& e : bsp.entities )
		{
			const char *model = e.valueForKey( "model" );
			if( model[0] == '*' ){
				e.setKeyValue( "model", atoi( model + 1 ) + bspModels.size() - 1, "*%i" ); // -1 : minus world
			}
		}
		/* make target/targetname names unique */
		if( fixnames ){
			const auto is_name = []( const epair_t& ep ){ return striEqual( ep.key.c_str(), "target" ) || striEqual( ep.key.c_str(), "targetname" ); };
			const auto has_name = [is_name]( const std::vector<entity_t>& entities, const char *name ){
				for( auto&& e : entities )
					for( auto&& ep : e.epairs )
						if( is_name( ep ) && striEqual( name, ep.value.c_str() ) )
							return true;
				return false;
			};
			for( auto&& e : bsp.entities )
			{
				for( const char *key : { "target", "targetname" } )
				{
					if( const char *name; e.read_keyvalue( name, key ) ){
						if( has_name( entities, name ) ){
							StringOutputStream newName;
							int id = 0;
							do{
								newName( name, '_', id++ );
							} while( has_name( entities, newName )
							      || has_name( bsp.entities, newName ) );

							const CopiedString oldName = name; // backup it, original will change
							for( auto&& e : bsp.entities )
								for( auto&& ep : e.epairs )
									if( is_name( ep ) && striEqual( ep.value.c_str(), oldName.c_str() ) )
										ep.value = newName;
						}
					}
				}
			}
		}
	}

	{
		entities.insert( entities.cend(), bsp.entities.cbegin() + 1, bsp.entities.cend() ); // minus world
		numBSPEntities = entities.size();
		bspModels.insert( bspModels.cend(), bsp.bspModels.cbegin() + 1, bsp.bspModels.cend() ); // minus world
		bspShaders.insert( bspShaders.cend(), bsp.bspShaders.cbegin(), bsp.bspShaders.cend() );
		// bspLeafs
		bspPlanes.insert( bspPlanes.cend(), bsp.bspPlanes.cbegin(), bsp.bspPlanes.cend() );
		// bspNodes
		// bspLeafSurfaces
		// bspLeafBrushes
		bspBrushes.insert( bspBrushes.cend(), bsp.bspBrushes.cbegin(), bsp.bspBrushes.cend() );
		bspBrushSides.insert( bspBrushSides.cend(), bsp.bspBrushSides.cbegin(), bsp.bspBrushSides.cend() );
		bspLightBytes.insert( bspLightBytes.cend(), bsp.bspLightBytes.cbegin(), bsp.bspLightBytes.cend() );
		// bspGridPoints
		// bspVisBytes
		bspDrawVerts.insert( bspDrawVerts.cend(), bsp.bspDrawVerts.cbegin(), bsp.bspDrawVerts.cend() );
		bspDrawIndexes.insert( bspDrawIndexes.cend(), bsp.bspDrawIndexes.cbegin(), bsp.bspDrawIndexes.cend() );
		bspDrawSurfaces.insert( bspDrawSurfaces.cend(), bsp.bspDrawSurfaces.cbegin(), bsp.bspDrawSurfaces.cend() );
		bspFogs.insert( bspFogs.cend(), bsp.bspFogs.cbegin(), bsp.bspFogs.cend() );
	}

	if( addworld ){
		/* insert new world surfaces */
		const std::vector<bspDrawSurface_t> surfs( bspDrawSurfaces.cbegin() + bsp.bspModels[0].firstBSPSurface,
		                                           bspDrawSurfaces.cbegin() + bsp.bspModels[0].firstBSPSurface + bsp.bspModels[0].numBSPSurfaces );
		bspDrawSurfaces.insert( bspDrawSurfaces.cbegin() + bspModels[0].firstBSPSurface + bspModels[0].numBSPSurfaces,
		                        surfs.cbegin(), surfs.cend() );
		// reindex
		for( auto&& index : bspLeafSurfaces )
			if( index >= bspModels[0].firstBSPSurface + bspModels[0].numBSPSurfaces )
				index += surfs.size();
		for( auto&& side : bspBrushSides )
			if( side.surfaceNum >= bspModels[0].firstBSPSurface + bspModels[0].numBSPSurfaces )
				side.surfaceNum += surfs.size();
		for( auto&& model : bspModels )
			if( model.firstBSPSurface >= bspModels[0].firstBSPSurface + bspModels[0].numBSPSurfaces )
				model.firstBSPSurface += surfs.size();
		bspModels[0].numBSPSurfaces += surfs.size();
		/* insert new world brushes */
		const std::vector<bspBrush_t> brushes( bspBrushes.cbegin() + bsp.bspModels[0].firstBSPBrush,
		                                       bspBrushes.cbegin() + bsp.bspModels[0].firstBSPBrush + bsp.bspModels[0].numBSPBrushes );
		bspBrushes.insert( bspBrushes.cbegin() + bspModels[0].firstBSPBrush + bspModels[0].numBSPBrushes,
		                   brushes.cbegin(), brushes.cend() );
		// reindex
		for( auto&& index : bspLeafBrushes )
			if( index >= bspModels[0].firstBSPBrush + bspModels[0].numBSPBrushes )
				index += brushes.size();
		for( auto&& fog : bspFogs )
			if( fog.brushNum >= bspModels[0].firstBSPBrush + bspModels[0].numBSPBrushes )
				fog.brushNum += brushes.size();
		for( auto&& model : bspModels )
			if( model.firstBSPBrush >= bspModels[0].firstBSPBrush + bspModels[0].numBSPBrushes )
				model.firstBSPBrush += brushes.size();
		bspModels[0].numBSPBrushes += brushes.size();
		/* reference surfaces */
		for( auto end = bspDrawSurfaces.cbegin() + bspModels[0].firstBSPSurface + bspModels[0].numBSPSurfaces,
		          surf = end - surfs.size(); surf != end; ++surf ){
			MinMax minmax; // cheap minmax test
			if( surf->surfaceType == MST_BAD ){
				continue;
			}
			else if( surf->surfaceType == MST_PATCH ){
				minmax = { surf->lightmapVecs[0], surf->lightmapVecs[1] };
			}
			else{
				for( const int i : Span( &bspDrawIndexes[surf->firstIndex], surf->numIndexes ) )
					minmax.extend( bspDrawVerts[surf->firstVert + i].xyz );
			}

			for( auto&& leaf : bspLeafs ){
				if( leaf.minmax.test( minmax ) ){
					for( auto&& l : bspLeafs )
						if( &l != &leaf && l.firstBSPLeafSurface >= leaf.firstBSPLeafSurface )
							++l.firstBSPLeafSurface;

					bspLeafSurfaces.insert( bspLeafSurfaces.cbegin() + leaf.firstBSPLeafSurface, int( std::distance( bspDrawSurfaces.cbegin(), surf ) ) );
					++leaf.numBSPLeafSurfaces;
				}
			}
		}
		/* reference brushes */
		/* convert bsp planes to map planes */
		mapplanes.resize( bspPlanes.size() );
		for ( size_t i = 0; i < bspPlanes.size(); ++i )
		{
			mapplanes[i].plane = bspPlanes[i];
		}

		for( auto end = bspBrushes.cbegin() + bspModels[0].firstBSPBrush + bspModels[0].numBSPBrushes,
		          brush = end - brushes.size(); brush != end; ++brush ){
			buildBrush.sides.clear();
			for( const bspBrushSide_t& side : Span( &bspBrushSides[ brush->firstSide ], brush->numSides ) ){
				auto& s = buildBrush.sides.emplace_back();
				s.planenum = side.planeNum;
			}
			if( CreateBrushWindings( buildBrush ) ){
				// cheap minmax test
				for( auto&& leaf : bspLeafs ){
					if( leaf.minmax.test( buildBrush.minmax ) ){
						for( auto&& l : bspLeafs )
							if( &l != &leaf && l.firstBSPLeafBrush >= leaf.firstBSPLeafBrush )
								++l.firstBSPLeafBrush;

						bspLeafBrushes.insert( bspLeafBrushes.cbegin() + leaf.firstBSPLeafBrush, int( std::distance( bspBrushes.cbegin(), brush ) ) );
						++leaf.numBSPLeafBrushes;
					}
				}
			}
		}
	}


	/* inject command line parameters */
	InjectCommandLine( "-mergebsp", argsToInject );

	/* write the bsp */
	UnparseEntities();
	path_set_extension( source, "_merged.bsp" );
	WriteBSPFile( source );

	/* return to sender */
	return 0;
}


/*
   PseudoCompileBSP()
   a stripped down ProcessModels
 */
static void PseudoCompileBSP( bool need_tree ){
	int models = 1;
	facelist_t faces;
	tree_t tree{};

	mapDrawSurfs = safe_calloc( sizeof( mapDrawSurface_t ) * max_map_draw_surfs );
	numMapDrawSurfs = 0;

	BeginBSPFile();
	for ( size_t entityNum = 0; entityNum < entities.size(); ++entityNum )
	{
		/* get entity */
		entity_t& entity = entities[ entityNum ];
		if ( entity.brushes.empty() && entity.patches.empty() ) {
			continue;
		}

		if ( entityNum != 0 ) {
			entity.setKeyValue( "model", models++, "*%i" );
		}

		/* process the model */
		Sys_FPrintf( SYS_VRB, "############### model %zu ###############\n", bspModels.size() );
		BeginModel( entity );

		entity.firstDrawSurf = numMapDrawSurfs;

		ClearMetaTriangles();
		PatchMapDrawSurfs( entity );

		if ( entityNum == 0 && need_tree ) {
			faces = MakeStructuralBSPFaceList( entities[0].brushes );
			tree = FaceBSP( faces );
		}
		else
		{
			tree.headnode = AllocNode();
			tree.headnode->planenum = PLANENUM_LEAF;
		}

		/* a minimized ClipSidesIntoTree */
		for ( const brush_t& brush : entity.brushes )
		{
			/* walk the brush sides */
			for ( const side_t& side : brush.sides )
			{
				if ( side.winding.empty() ) {
					continue;
				}
				/* shader? */
				if ( side.shaderInfo == nullptr ) {
					continue;
				}
				/* save this winding as a visible surface */
				DrawSurfaceForSide( entity, brush, side, side.winding );
			}
		}

		if ( meta ) {
			ClassifyEntitySurfaces( entity );
			MakeEntityDecals( entity );
			MakeEntityMetaTriangles( entity );
			SmoothMetaTriangles();
			MergeMetaTriangles();
		}
		FilterDrawsurfsIntoTree( entity, tree );

		FilterStructuralBrushesIntoTree( entity, tree );
		FilterDetailBrushesIntoTree( entity, tree );

		EmitBrushes( entity );
		EndModel( entity, tree.headnode );
	}
	EndBSPFile( false );
}

/*
   ConvertBSPMain()
   main argument processing function for bsp conversion
 */

int ConvertBSPMain( Args& args, bool decompile ){
	int meshPatchSteps=8;
	bool patchStepsSpecified=false;
	bool brushOrderSpecified=false;
	bool detailPolicySpecified=false, detailWorkSpecified=false;
	bool groupPolicySpecified=false, groupWorkSpecified=false;
	bool uvPolicySpecified=false;
	bool patchColorsSpecified=false, patchColorSubdivisionsSpecified=false;
	int ( *convertFunc )( char * );
	const game_t  *convertGame;
	bool map_allowed, force_bsp, force_map;


	/* set default */
	convertFunc = decompile ? ConvertBSPToMap_220 : ConvertBSPToASE;
	decompileOptions = {};
	decompileOptions.automaticReport = decompile;
	convertGame = nullptr;
	map_allowed = decompile;
	force_bsp = decompile;
	force_map = false;

	/* arg checking */
	if ( args.empty() ) {
		Sys_Printf( "Usage: q3map2 -convert [-format <ase|obj|map|map_bp|map_220|game name>] [-shadersasbitmap|-lightmapsastexcoord|-deluxemapsastexcoord] [-readbsp|-readmap [-meta|-patchmeta]] [-v] <mapname>\n" );
		return 0;
	}

	/* process arguments */
	const char *fileName = args.takeBack();
	{
		/* -format map|ase|... */
		while ( args.takeArg( "-format" ) ) {
			const char *fmt = args.takeNext();
			if ( striEqual( fmt, "ase" ) ) {
				convertFunc = ConvertBSPToASE;
				map_allowed = false;
			}
			else if ( striEqual( fmt, "obj" ) ) {
				convertFunc = ConvertBSPToOBJ;
				map_allowed = false;
			}
			else if ( striEqual( fmt, "map" ) ) {
				convertFunc = ConvertBSPToMap;
				map_allowed = true;
			}
			else if ( striEqual( fmt, "map_bp" ) ) {
				convertFunc = ConvertBSPToMap_BP;
				map_allowed = true;
			}
			else if ( striEqual( fmt, "map_220" ) ) {
				convertFunc = ConvertBSPToMap_220;
				map_allowed = true;
			}
			else
			{
				convertGame = GetGame( fmt );
				map_allowed = false;
				if ( convertGame == nullptr ) {
					Error( "Unknown conversion format '%s'", fmt );
				}
			}
		}
		while ( args.takeArg( "-ne" ) ) {
			normalEpsilon = args.takeDouble();
			Sys_Printf( "Normal epsilon set to %lf\n", normalEpsilon );
		}
		while ( args.takeArg( "-de" ) ) {
			distanceEpsilon = args.takeDouble();
			Sys_Printf( "Distance epsilon set to %lf\n", distanceEpsilon );
		}
		while ( args.takeArg( "-shaderasbitmap", "-shadersasbitmap" ) ) {
			shadersAsBitmap = true;
		}
		while ( args.takeArg( "-lightmapastexcoord", "-lightmapsastexcoord" ) ) {
			lightmapsAsTexcoord = true;
		}
		while ( args.takeArg( "-deluxemapastexcoord", "-deluxemapsastexcoord" ) ) {
			lightmapsAsTexcoord = true;
			deluxemap = true;
		}
		while ( args.takeArg( "-readbsp" ) ) {
			force_bsp = true;
		}
		while ( args.takeArg( "-readmap" ) ) {
			force_map = true;
		}
		while ( args.takeArg( "-meta" ) ) {
			meta = true;
		}
		while ( args.takeArg( "-patchmeta" ) ) {
			meta = true;
			patchMeta = true;
		}
		while ( args.takeArg( "-fast" ) ) {
			fast = true;
		}
		while ( args.takeArg( "-patchsteps" ) ) {
			meshPatchSteps=args.takeInt(); patchStepsSpecified=true;
			if(meshPatchSteps<1 || meshPatchSteps>32) Error("Mesh patch steps must be between 1 and 32");
		}
		while ( args.takeArg( "-modelclip" ) ) {
			g_decompile_modelClip = true;
		}
		while ( args.takeArg( "-wtf" ) ) {
			g_decompile_wtf = true;
		}
		while ( args.takeArg( "-o" ) ) {
			decompileOptions.output = args.takeNext();
			if ( !path_extension_is( decompileOptions.output, "map" ) ) Error( "Decompile output must have a .map extension" );
		}
		while ( args.takeArg( "-report" ) ) {
			decompileOptions.report = args.takeNext();
			if ( !path_extension_is( decompileOptions.report, "json" ) ) Error( "Recovery report must have a .json extension" );
		}
		while ( args.takeArg( "-light-proposals" ) ) {
			if ( decompileOptions.lightProposals ) Error( "Only one light proposal report can be applied per export" );
			decompileOptions.lightProposals = args.takeNext();
		}
		while ( args.takeArg( "-brush-order" ) ) {
			brushOrderSpecified = true;
			const char* order = args.takeNext();
			if ( striEqual( order, "bsp" ) ) decompileOptions.brushOrder = DecompileOptions::BrushOrder::Bsp;
			else if ( striEqual( order, "rebuild" ) ) decompileOptions.brushOrder = DecompileOptions::BrushOrder::Rebuild;
			else Error( "Brush order must be bsp or rebuild" );
		}
		while ( args.takeArg( "-detail-policy" ) ) {
			detailPolicySpecified = true;
			const char* policy = args.takeNext();
			if ( striEqual( policy, "legacy" ) ) decompileOptions.detailPolicy = DecompileOptions::DetailPolicy::Legacy;
			else if ( striEqual( policy, "cells" ) ) decompileOptions.detailPolicy = DecompileOptions::DetailPolicy::Cells;
			else Error( "Detail policy must be legacy or cells" );
		}
		while ( args.takeArg( "-detail-max-work" ) ) {
			detailWorkSpecified = true;
			const int work = args.takeInt();
			if ( work < 1 || work > 100'000'000 ) Error( "Detail analysis work must be between 1 and 100000000" );
			decompileOptions.detailWorkLimit = unsigned( work );
		}
		while ( args.takeArg( "-group-policy" ) ) {
			groupPolicySpecified = true;
			const char* policy = args.takeNext();
			if ( striEqual( policy, "none" ) ) decompileOptions.groupPolicy = DecompileOptions::GroupPolicy::None;
			else if ( striEqual( policy, "surfaces" ) ) decompileOptions.groupPolicy = DecompileOptions::GroupPolicy::Surfaces;
			else Error( "Group policy must be none or surfaces" );
		}
		while ( args.takeArg( "-group-max-work" ) ) {
			groupWorkSpecified = true;
			decompileOptions.groupWorkLimit = unsigned( args.takeInt( 1, 100'000'000 ) );
		}
		while ( args.takeArg( "-uv-policy" ) ) {
			uvPolicySpecified = true;
			const char* policy = args.takeNext();
			if ( striEqual( policy, "consensus" ) ) decompileOptions.uvPolicy = DecompileOptions::UVPolicy::Consensus;
			else if ( striEqual( policy, "triangle" ) ) decompileOptions.uvPolicy = DecompileOptions::UVPolicy::Triangle;
			else Error( "UV policy must be consensus or triangle" );
		}
		while ( args.takeArg( "-patch-colors" ) ) {
			patchColorsSpecified = true;
			const char* policy = args.takeNext();
			if ( striEqual( policy, "none" ) ) decompileOptions.patchColors = DecompileOptions::PatchColors::None;
			else if ( striEqual( policy, "alpha" ) ) decompileOptions.patchColors = DecompileOptions::PatchColors::Alpha;
			else if ( striEqual( policy, "rgba" ) ) decompileOptions.patchColors = DecompileOptions::PatchColors::RGBA;
			else Error( "Patch colors must be none, alpha or rgba" );
		}
		while ( args.takeArg( "-patch-color-subdivisions" ) ) {
			patchColorSubdivisionsSpecified = true;
			const int n = args.takeInt( 1, 32 );
			if ( n & ( n - 1 ) ) Error( "Patch color subdivisions must be 1, 2, 4, 8, 16 or 32" );
			decompileOptions.patchColorSubdivisions = n;
		}
	}
	if ( !args.empty() ) Error( "Unknown conversion option '%s'", args.takeFront() );
	if ( patchColorsSpecified && ( !map_allowed || convertGame ) ) Error( "-patch-colors requires MAP export" );
	if ( patchColorSubdivisionsSpecified && decompileOptions.patchColors == DecompileOptions::PatchColors::None )
		Error( "-patch-color-subdivisions requires -patch-colors alpha or rgba" );
	if ( decompileOptions.patchColors != DecompileOptions::PatchColors::None ) {
		if ( !g_game->write ) Error( "Patch color recovery requires a BSP-writing game profile" );
		if ( force_map || ( !force_bsp && path_extension_is( fileName, "map" ) ) ) Error( "Patch color recovery requires a compiled BSP input" );
		if ( g_decompile_wtf ) Error( "Patch color recovery cannot be combined with -wtf material replacement" );
		decompileOptions.automaticReport = true;
	}
	if ( decompileOptions.lightProposals ) {
		if ( !map_allowed || convertGame || !g_game->write ) Error( "Light proposal export requires MAP export and a BSP-writing profile" );
		if ( force_map || ( !force_bsp && path_extension_is( fileName, "map" ) ) ) Error( "Light proposal export requires a compiled BSP input" );
		if ( g_decompile_wtf ) Error( "Light proposal export cannot be combined with -wtf material replacement" );
		decompileOptions.automaticReport = true;
	}
	if ( uvPolicySpecified && ( !map_allowed || convertGame || fast ) )
		Error( "-uv-policy requires MAP export without -fast" );
	if ( brushOrderSpecified && ( !map_allowed || convertGame ) )
		Error( "-brush-order requires map, map_bp or map_220 export" );
	if ( detailPolicySpecified && ( !map_allowed || convertGame ) )
		Error( "-detail-policy requires map, map_bp or map_220 export" );
	if ( groupPolicySpecified && ( !map_allowed || convertGame ) ) Error( "-group-policy requires MAP export" );
	if ( groupWorkSpecified && decompileOptions.groupPolicy != DecompileOptions::GroupPolicy::Surfaces )
		Error( "-group-max-work requires -group-policy surfaces" );
	if ( decompileOptions.groupPolicy == DecompileOptions::GroupPolicy::Surfaces ) {
		if ( !g_game->write ) Error( "Group inference requires a BSP-writing game profile" );
		if ( g_decompile_wtf ) Error( "Group inference cannot be combined with -wtf material replacement" );
		if ( force_map || ( !force_bsp && path_extension_is( fileName, "map" ) ) ) Error( "Group inference requires a compiled BSP input" );
		if ( brushOrderSpecified && decompileOptions.brushOrder != DecompileOptions::BrushOrder::Rebuild )
			Error( "Group inference requires rebuild brush order" );
		decompileOptions.brushOrder = DecompileOptions::BrushOrder::Rebuild;
		decompileOptions.automaticReport = true;
	}
	if ( detailWorkSpecified && decompileOptions.detailPolicy != DecompileOptions::DetailPolicy::Cells )
		Error( "-detail-max-work requires -detail-policy cells" );
	if ( decompileOptions.detailPolicy == DecompileOptions::DetailPolicy::Cells ) {
		if ( !g_game->write ) Error( "Cell detail inference requires a BSP-writing game profile" );
		if ( g_decompile_wtf ) Error( "Cell detail inference cannot be combined with -wtf material replacement" );
		if ( force_map || ( !force_bsp && path_extension_is( fileName, "map" ) ) ) Error( "Cell detail inference requires a compiled BSP input" );
		decompileOptions.automaticReport = true;
	}
	if ( decompileOptions.brushOrder == DecompileOptions::BrushOrder::Rebuild ) {
		if ( !g_game->write ) Error( "Rebuild brush order requires a BSP-writing game profile; native recovery-only profiles are unsupported" );
		if ( g_decompile_wtf ) Error( "Rebuild brush order cannot be combined with -wtf material replacement" );
	}
	const bool meshExport=!convertGame && (convertFunc==ConvertBSPToOBJ || convertFunc==ConvertBSPToASE);
	if(patchStepsSpecified && !meshExport) Error("-patchsteps requires OBJ or ASE export");
	if (convertGame && (!convertGame->write || !g_game->write))
		Error("Native recovery-only profiles cannot be BSP conversion sources or targets; use MAP, OBJ or ASE export");
	if (!g_game->write && (force_map || path_extension_is(fileName,"map")))
		Error("Native recovery-only profiles require a BSP input");
	if (!g_game->write && map_allowed) decompileOptions.automaticReport=true;
	if ( !map_allowed && ( decompileOptions.output || decompileOptions.report || decompile ) ) {
		Error( "Decompilation output/report options require map, map_bp or map_220 format" );
	}

	LoadShaderInfo();

	/* clean up map name */
	strcpy( source, ExpandArg( fileName ) );

	if ( !map_allowed && !force_map ) {
		force_bsp = true;
	}

	if ( force_map || ( !force_bsp && path_extension_is( source, "map" ) && map_allowed ) ) {
		if ( !map_allowed ) {
			Sys_Warning( "the requested conversion should not be done from .map files. Compile a .bsp first.\n" );
		}
		path_set_extension( source, ".map" );
		Sys_Printf( "Loading %s\n", source );
		LoadMapFile( source, false, convertGame == nullptr );
		PseudoCompileBSP( convertGame != nullptr );
	}
	else
	{
		path_set_extension( source, ".bsp" );
		Sys_Printf( "Loading %s\n", source );
		if ( decompileOptions.lightProposals ) {
			try { decompileOptions.lightRecovery = q3mapx::LightRecovery::load( source, decompileOptions.lightProposals ); }
			catch ( const std::exception& error ) { Error( "Light proposal export: %s", error.what() ); }
		}
		LoadBSPFile( source );
		ParseEntities();
	}

	/* bsp format convert? */
	if ( convertGame != nullptr ) {
		/* set global game */
		g_game = convertGame;

		/* write bsp */
		path_set_extension( source, "_c.bsp" );
		WriteBSPFile( source );

		/* return to sender */
		return 0;
	}

	/* normal convert */
	if(meshExport) PrepareBSPMeshExport(meshPatchSteps);
	return convertFunc( source );
}
