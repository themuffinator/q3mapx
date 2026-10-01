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
#include "surface_extra.h"
#include "patch_source.h"
#include "bspfile_rbsp.h"
#include "authoring/surface.h"
#include "authoring/patch_paint.h"
#include <glib.h>
#include <bit>
#include <charconv>
#include <memory>
#include <set>


/* -------------------------------------------------------------------------------

   ydnar: srf file module

   ------------------------------------------------------------------------------- */


static std::vector<surfaceExtra_t> surfaceExtras;
static surfaceExtra_t seDefault;
static std::string paintBinding;

namespace {
bool validPaintBinding( std::string_view value ) {
	return value.size() == 64 && std::ranges::all_of( value, []( char c ) { return ( c >= '0' && c <= '9' ) || ( c >= 'a' && c <= 'f' ); } );
}

// Bind modes and their immutable output channels to exact output topology.
// Exclude lightmaps, baked RGB and generated shader names: LIGHT changes them.
// Canonical little-endian words avoid host layout/padding in the digest.
std::string patchPaintDigest() {
	std::unique_ptr<GChecksum, decltype( &g_checksum_free )> hash( g_checksum_new( G_CHECKSUM_SHA256 ), g_checksum_free );
	if ( !hash ) Error( "Cannot allocate patch paint checksum" );
	std::array<uint32_t, 1024> buffer;
	size_t used = 0;
	auto flush = [&] {
		g_checksum_update( hash.get(), reinterpret_cast<const guchar*>( buffer.data() ), used * sizeof( uint32_t ) );
		used = 0;
	};
	auto word = [&]( uint32_t n ) {
		buffer[used++] = GUINT32_TO_LE( n );
		if ( used == buffer.size() ) flush();
	};
	word( 1 );
	const int styles = g_game->load == LoadRBSPFile ? MAX_LIGHTMAPS : 1;
	word( styles );
	word( bspDrawSurfaces.size() );
	for ( size_t index = 0; index < bspDrawSurfaces.size(); ++index ) {
		const auto& ds = bspDrawSurfaces[index];
		const int mode = GetSurfaceExtra( index ).paintMode;
		word( mode ); word( ds.surfaceType ); word( ds.firstVert ); word( ds.numVerts ); word( ds.firstIndex ); word( ds.numIndexes );
		word( GetSurfaceExtra( index ).parentSurfaceNum );
		word( ds.patchWidth ); word( ds.patchHeight );
		for ( int v = 0; v < ds.numVerts; ++v ) {
			const auto& vert = bspDrawVerts[ds.firstVert + v];
			for ( int axis = 0; axis < 3; ++axis ) word( std::bit_cast<uint32_t>( vert.xyz[axis] ) );
			for ( int axis = 0; axis < 2; ++axis ) word( std::bit_cast<uint32_t>( vert.st[axis] ) );
			if ( mode ) for ( int style = 0; style < styles; ++style ) {
				word( vert.color[style].alpha() );
				if ( mode == q3mapx::authoring::materialPaint )
					for ( int channel = 0; channel < 3; ++channel ) word( vert.color[style][channel] );
			}
		}
		for ( int i = 0; i < ds.numIndexes; ++i ) word( bspDrawIndexes[ds.firstIndex + i] );
	}
	if ( used ) flush();
	return g_checksum_get_string( hash.get() );
}
}

void BindPatchPaint() {
	paintBinding.clear();
	entities[0].epairs.remove_if( []( const epair_t& ep ) { return striEqual( ep.key.c_str(), q3mapx::authoring::paintBindingKey ); } );
	if ( std::ranges::any_of( surfaceExtras, []( const auto& se ) { return se.paintMode != 0; } ) ) {
		paintBinding = patchPaintDigest();
		entities[0].setKeyValue( q3mapx::authoring::paintBindingKey, paintBinding.c_str() );
	}
}

void ValidatePatchPaintBinding() {
	for ( size_t index = 0; index < surfaceExtras.size(); ++index ) {
		const int parent = surfaceExtras[index].parentSurfaceNum;
		if ( parent < -1 || ( parent >= 0 && ( size_t( parent ) >= bspDrawSurfaces.size()
		  || size_t( parent ) >= index || bspDrawSurfaces[parent].numVerts != bspDrawSurfaces[index].numVerts ) ) )
			Error( "Invalid SRF parent surface for surface %zu", index );
	}
	const std::string_view stored = entities[0].valueForKey( q3mapx::authoring::paintBindingKey );
	const bool painted = std::ranges::any_of( surfaceExtras, []( const auto& se ) { return se.paintMode != 0; } );
	if ( stored.empty() && paintBinding.empty() && !painted ) return;
	if ( !painted || !validPaintBinding( stored ) || stored != paintBinding || stored != patchPaintDigest() )
		Error( "Patch paint BSP/SRF binding mismatch; rebuild BSP and keep its matching SRF before LIGHT" );
}

void ResolveSurfaceExtraShaders() {
	if ( !seDefault.shaderName.empty() ) seDefault.si = &ShaderInfoForShader( seDefault.shaderName.c_str() );
	for ( auto& se : surfaceExtras )
		if ( !se.shaderName.empty() ) se.si = &ShaderInfoForShader( se.shaderName.c_str() );
}



/*
   SetDefaultSampleSize()
   sets the default lightmap sample size
 */

void SetDefaultSampleSize( int sampleSize ){
	seDefault.sampleSize = sampleSize;
}

void SetDefaultAmbientColor( const Vector3& color ){
	seDefault.ambientColor = color;
}



/*
   SetSurfaceExtra()
   stores extra (q3map2) data for the specific numbered drawsurface
 */

void SetSurfaceExtra( const mapDrawSurface_t& ds ){
	q3mapx::LinkPatchSourceSurface( ds );
	/* get a new extra */
	surfaceExtra_t& se = surfaceExtras.emplace_back( seDefault );

	/* copy out the relevant bits */
	se.mds = &ds;
	se.si           = ds.shaderInfo;
	se.parentSurfaceNum = ds.parent != nullptr ? ds.parent->outputNum : -1;
	se.entityNum    = ds.entityNum;
	se.castShadows  = ds.castShadows;
	se.recvShadows  = ds.recvShadows;
	se.sampleSize   = ds.sampleSize;
	se.authoredSampleSize = ds.lightmapSampleSizeOverride;
	se.paintMode = ds.paintMode;
	se.ambientColor = ds.ambientColor;
	se.longestCurve = ds.longestCurve;
	se.lightmapAxis = ds.lightmapAxis;

	/* debug code */
	//%	Sys_FPrintf( SYS_VRB, "SetSurfaceExtra(): entityNum = %d\n", ds.entityNum );
}



/*
   GetSurfaceExtra*()
   getter functions for extra surface data
 */

const surfaceExtra_t& GetSurfaceExtra( int num ){
	if ( num < 0 || size_t( num ) >= surfaceExtras.size() ) {
		return seDefault;
	}
	return surfaceExtras[ num ];
}



/*
   WriteSurfaceExtraFile()
   writes out a surface info file (<map>.srf)
 */

void WriteSurfaceExtraFile( const char *path ){
	/* dummy check */
	if ( strEmptyOrNull( path ) ) {
		return;
	}

	/* note it */
	Sys_Printf( "--- WriteSurfaceExtraFile ---\n" );

	/* open the file */
	const auto srfPath = StringStream( path, ".srf" );
	Sys_Printf( "Writing %s\n", srfPath.c_str() );
	FILE *sf = SafeOpenWrite( srfPath, "wt" );

	/* lap through the extras list */
	for ( int i = -1, size = surfaceExtras.size(); i < size; ++i )
	{
		/* get extra */
		const surfaceExtra_t& se = GetSurfaceExtra( i );

		/* default or surface num? */
		if ( i < 0 ) {
			fprintf( sf, "default" );
		}
		else{
			fprintf( sf, "%d", i );
		}

		/* valid map drawsurf? */
		if ( se.mds == nullptr ) {
			fprintf( sf, "\n" );
		}
		else
		{
			fprintf( sf, " // %s V: %zu I: %zu %s\n",
			         surfaceTypeName( se.mds->type ),
			         se.mds->verts.size(),
			         se.mds->indexes.size(),
			         ( se.mds->planar ? "planar" : "" ) );
		}

		/* open braces */
		fprintf( sf, "{\n" );
		if ( i < 0 && !paintBinding.empty() ) fprintf( sf, "\tpatchPaintBinding1 %s\n", paintBinding.c_str() );
		if ( se.paintMode ) fprintf( sf, "\tpatchPaintMode %d\n", se.paintMode );

		/* shader */
		if ( se.si != nullptr ) {
			fprintf( sf, "\tshader %s\n", se.si->shader.c_str() );
		}

		/* parent surface number */
		if ( se.parentSurfaceNum != seDefault.parentSurfaceNum ) {
			fprintf( sf, "\tparent %d\n", se.parentSurfaceNum );
		}

		/* entity number */
		if ( se.entityNum != seDefault.entityNum ) {
			fprintf( sf, "\tentity %d\n", se.entityNum );
		}

		/* cast shadows */
		if ( se.castShadows != seDefault.castShadows || &se == &seDefault ) {
			fprintf( sf, "\tcastShadows %d\n", se.castShadows );
		}

		/* recv shadows */
		if ( se.recvShadows != seDefault.recvShadows || &se == &seDefault ) {
			fprintf( sf, "\treceiveShadows %d\n", se.recvShadows );
		}

		/* lightmap sample size */
		if ( se.sampleSize != seDefault.sampleSize || &se == &seDefault ) {
			fprintf( sf, "\tsampleSize %d\n", se.sampleSize );
		}
		if ( se.authoredSampleSize > 0 ) {
			fprintf( sf, "\tauthoredSampleSize %d\n", se.authoredSampleSize );
		}

		if ( ( se.ambientColor != g_vector3_identity && se.ambientColor != seDefault.ambientColor ) || &se == &seDefault ) { // 0 == use global
			fprintf( sf, "\tambientColor ( %f %f %f )\n", se.ambientColor[0], se.ambientColor[1], se.ambientColor[2] );
		}

		/* longest curve */
		if ( se.longestCurve != seDefault.longestCurve || &se == &seDefault ) {
			fprintf( sf, "\tlongestCurve %f\n", se.longestCurve );
		}

		/* lightmap axis vector */
		if ( !VectorCompare( se.lightmapAxis, seDefault.lightmapAxis ) ) {
			fprintf( sf, "\tlightmapAxis ( %f %f %f )\n", se.lightmapAxis[ 0 ], se.lightmapAxis[ 1 ], se.lightmapAxis[ 2 ] );
		}

		/* close braces */
		fprintf( sf, "}\n\n" );
	}

	/* close the file */
	fclose( sf );
}



/*
   LoadSurfaceExtraFile()
   reads a surface info file (<map>.srf)
 */

void LoadSurfaceExtraFile( const char *path ){
	/* dummy check */
	if ( strEmptyOrNull( path ) ) {
		return;
	}

	/* load the file */
	const auto srfPath = StringStream( PathExtensionless( path ), ".srf" );

	/* parse the file */
	if( !LoadScriptFile( srfPath, -1 ) )
		Error( "" );
	surfaceExtras.clear();
	paintBinding.clear();
	std::set<int> records;

	/* tokenize it */
	while ( GetToken( true ) ) /* test for end of file */
	{
		surfaceExtra_t  *se;
		int record = -1;
		/* default? */
		if ( striEqual( token, "default" ) ) {
			se = &seDefault;
		}

		/* surface number */
		else
		{
			int surfaceNum = -1;
			const char* end = token + std::strlen( token );
			const auto result = std::from_chars( token, end, surfaceNum );
			if ( result.ec != std::errc{} || result.ptr != end || surfaceNum < 0 || size_t( surfaceNum ) >= bspDrawSurfaces.size() )
				Error( "ReadSurfaceExtraFile(): %s, line %d: surface index outside loaded BSP", srfPath.c_str(), scriptline );
			record = surfaceNum;
			if( size_t( surfaceNum ) >= surfaceExtras.size() ){
				if( size_t( surfaceNum ) >= surfaceExtras.capacity() ) // ensure that capacity grows efficiently, as it's not guaranteed for vector::resize()
					surfaceExtras.reserve( surfaceExtras.capacity() << 1 );
				surfaceExtras.resize( surfaceNum + 1, seDefault );
			}
			se = &surfaceExtras[ surfaceNum ];
		}
		if ( !records.insert( record ).second ) Error( "Duplicate SRF surface record at line %d", scriptline );
		bool hasPaintMode = false;

		/* handle { } section */
		if ( !( GetToken( true ) && strEqual( token, "{" ) ) ) {
			Error( "ReadSurfaceExtraFile(): %s, line %d: { not found", srfPath.c_str(), scriptline );
		}
		while ( true )
		{
			if ( !GetToken( true ) ) Error( "Incomplete SRF surface record at line %d", scriptline );
			if ( strEqual( token, "}" ) ) break;
			if ( strEqual( token, "patchPaintMode" ) ) {
				if ( record < 0 || hasPaintMode || !GetToken( false ) || !( strEqual( token, "1" ) || strEqual( token, "2" ) ) )
					Error( "Invalid patchPaintMode at line %d", scriptline );
				se->paintMode = token[0] - '0';
				hasPaintMode = true;
				if ( TokenAvailable() ) Error( "Trailing patchPaintMode data at line %d", scriptline );
				continue;
			}
			else if ( strEqual( token, "patchPaintBinding1" ) ) {
				if ( record >= 0 || !paintBinding.empty() || !GetToken( false ) || !validPaintBinding( token ) )
					Error( "Invalid patchPaintBinding1 at line %d", scriptline );
				paintBinding = token;
				if ( TokenAvailable() ) Error( "Trailing patchPaintBinding1 data at line %d", scriptline );
				continue;
			}
			/* shader */
			else if ( striEqual( token, "shader" ) ) {
				GetToken( false );
				se->shaderName = token;
			}

			/* parent surface number */
			else if ( striEqual( token, "parent" ) ) {
				if ( !GetToken( false ) ) Error( "Missing SRF parent index at line %d", scriptline );
				const char* end = token + std::strlen( token );
				const auto result = std::from_chars( token, end, se->parentSurfaceNum );
				if ( result.ec != std::errc{} || result.ptr != end ) Error( "Invalid SRF parent index at line %d", scriptline );
			}

			/* entity number */
			else if ( striEqual( token, "entity" ) ) {
				GetToken( false );
				se->entityNum = atoi( token );
			}

			/* cast shadows */
			else if ( striEqual( token, "castShadows" ) ) {
				GetToken( false );
				se->castShadows = atoi( token );
			}

			/* recv shadows */
			else if ( striEqual( token, "receiveShadows" ) ) {
				GetToken( false );
				se->recvShadows = atoi( token );
			}

			/* lightmap sample size */
			else if ( striEqual( token, "sampleSize" ) ) {
				GetToken( false );
				se->sampleSize = atoi( token );
			}
			else if ( striEqual( token, "authoredSampleSize" ) ) {
				if ( !GetToken( false ) || !q3mapx::authoring::parseSampleSize( token, se->authoredSampleSize ) )
					Error( "Invalid authoredSampleSize in surface extra file at line %d", scriptline );
			}

			else if ( striEqual( token, "ambientColor" ) ) {
				Parse1DMatrix( 3, se->ambientColor.data() );
			}

			/* longest curve */
			else if ( striEqual( token, "longestCurve" ) ) {
				GetToken( false );
				se->longestCurve = atof( token );
			}

			/* lightmap axis vector */
			else if ( striEqual( token, "lightmapAxis" ) ) {
				Parse1DMatrix( 3, se->lightmapAxis.data() );
			}

			/* ignore all other tokens on the line */
			while ( TokenAvailable() )
				GetToken( false );
		}
	}
}
