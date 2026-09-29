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
#include "arguments.h"
#include "q3mapx/columns.h"
#include "timer.h"

/* minimap stuff */

struct minimap_t
{
	const bspModel_t *model;
	int width;
	int height;
	int samples;
	float *sample_offsets;
	float sharpen_boxmult;
	float sharpen_centermult;
	float boost, brightness, contrast;
	float *data1f;
	float *sharpendata1f;
	Vector3 mins, size;
};

static minimap_t minimap;

static q3mapx::ColumnScene columns;
static bool referenceSampling = false;
static uint32_t randomSeed = 0;

static float MiniMapSample( float x, float y ){
	return columns.sample(x, y, !referenceSampling);
}

inline void RandomVector2f( float v[2], uint32_t& state ){
	do {
		v[0] = 2.0f * q3mapx::columnRandom(state) - 1.0f;
		v[1] = 2.0f * q3mapx::columnRandom(state) - 1.0f;
	} while ( v[0] * v[0] + v[1] * v[1] > 1.0f );
}

static void MiniMapRandomlySupersampled( int y ){
	int x, i;
	float *p = &minimap.data1f[y * minimap.width];
	float ymin = minimap.mins[1] + minimap.size[1] * ( y / (float) minimap.height );
	float dx   =                   minimap.size[0]       / (float) minimap.width;
	float dy   =                   minimap.size[1]       / (float) minimap.height;
	float uv[2];
	float thisval;

	for ( x = 0; x < minimap.width; ++x )
	{
		float xmin = minimap.mins[0] + minimap.size[0] * ( x / (float) minimap.width );
		float val = 0;
		uint32_t state = uint32_t(y * minimap.width + x) ^ randomSeed;

		for ( i = 0; i < minimap.samples; ++i )
		{
			RandomVector2f( uv, state );
			thisval = MiniMapSample(
			              xmin + ( uv[0] + 0.5 ) * dx, /* exaggerated random pattern for better results */
			              ymin + ( uv[1] + 0.5 ) * dy  /* exaggerated random pattern for better results */
			          );
			val += thisval;
		}
		val /= minimap.samples * minimap.size[2];
		*p++ = val;
	}
}

static void MiniMapSupersampled( int y ){
	int x, i;
	float *p = &minimap.data1f[y * minimap.width];
	float ymin = minimap.mins[1] + minimap.size[1] * ( y / (float) minimap.height );
	float dx   =                   minimap.size[0]       / (float) minimap.width;
	float dy   =                   minimap.size[1]       / (float) minimap.height;

	for ( x = 0; x < minimap.width; ++x )
	{
		float xmin = minimap.mins[0] + minimap.size[0] * ( x / (float) minimap.width );
		float val = 0;

		for ( i = 0; i < minimap.samples; ++i )
		{
			float thisval = MiniMapSample(
			                    xmin + minimap.sample_offsets[2 * i + 0] * dx,
			                    ymin + minimap.sample_offsets[2 * i + 1] * dy
			                );
			val += thisval;
		}
		val /= minimap.samples * minimap.size[2];
		*p++ = val;
	}
}

static void MiniMapNoSupersampling( int y ){
	int x;
	float *p = &minimap.data1f[y * minimap.width];
	float ymin = minimap.mins[1] + minimap.size[1] * ( ( y + 0.5 ) / minimap.height );

	for ( x = 0; x < minimap.width; ++x )
	{
		float xmin = minimap.mins[0] + minimap.size[0] * ( ( x + 0.5 ) / minimap.width );
		*p++ = MiniMapSample( xmin, ymin ) / minimap.size[2];
	}
}

static void MiniMapSharpen( int y ){
	int x;
	const bool up = ( y > 0 );
	const bool down = ( y < minimap.height - 1 );
	float *p = &minimap.data1f[y * minimap.width];
	float *q = &minimap.sharpendata1f[y * minimap.width];

	for ( x = 0; x < minimap.width; ++x )
	{
		const bool left = ( x > 0 );
		const bool right = ( x < minimap.width - 1 );
		float val = p[0] * minimap.sharpen_centermult;

		if ( left && up ) {
			val += p[-1 - minimap.width] * minimap.sharpen_boxmult;
		}
		if ( left && down ) {
			val += p[-1 + minimap.width] * minimap.sharpen_boxmult;
		}
		if ( right && up ) {
			val += p[+1 - minimap.width] * minimap.sharpen_boxmult;
		}
		if ( right && down ) {
			val += p[+1 + minimap.width] * minimap.sharpen_boxmult;
		}

		if ( left ) {
			val += p[-1] * minimap.sharpen_boxmult;
		}
		if ( right ) {
			val += p[+1] * minimap.sharpen_boxmult;
		}
		if ( up ) {
			val += p[-minimap.width] * minimap.sharpen_boxmult;
		}
		if ( down ) {
			val += p[+minimap.width] * minimap.sharpen_boxmult;
		}

		++p;
		*q++ = val;
	}
}

static void MiniMapContrastBoost( int y ){
	int x;
	float *q = &minimap.data1f[y * minimap.width];
	for ( x = 0; x < minimap.width; ++x )
	{
		*q = *q * minimap.boost / ( ( minimap.boost - 1 ) * *q + 1 );
		++q;
	}
}

static void MiniMapBrightnessContrast( int y ){
	int x;
	float *q = &minimap.data1f[y * minimap.width];
	for ( x = 0; x < minimap.width; ++x )
	{
		*q = *q * minimap.contrast + minimap.brightness;
		++q;
	}
}

static void MiniMapMakeMinsMaxs( Vector3& mins, Vector3& maxs, float border, bool keepaspect ){
	// line compatible to nexuiz mapinfo
	Sys_Printf( "size %f %f %f %f %f %f\n", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2] );

	if ( keepaspect ) {
		const Vector3 extend = maxs - mins;
		if ( extend[1] > extend[0] ) {
			mins[0] -= ( extend[1] - extend[0] ) * 0.5;
			maxs[0] += ( extend[1] - extend[0] ) * 0.5;
		}
		else
		{
			mins[1] -= ( extend[0] - extend[1] ) * 0.5;
			maxs[1] += ( extend[0] - extend[1] ) * 0.5;
		}
	}

	/* border: amount of black area around the image */
	/* input: border, 1-2*border, border but we need border/(1-2*border) */

	const Vector3 extend = ( maxs - mins ) * ( border / ( 1 - 2 * border ) );

	mins -= extend;
	maxs += extend;

	minimap.mins = mins;
	minimap.size = maxs - mins;

	// line compatible to nexuiz mapinfo
	Sys_Printf( "size_texcoords %f %f %f %f %f %f\n", mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2] );
}

/*
   MiniMapSetupBrushes()
   determines solid non-sky brushes in the world
 */

static void MiniMapSetupBrushes(){
	SetupBrushesFlags( C_SOLID | C_SKY, C_SOLID, 0, 0 );
	Timer timer;
	columns = {};
	for ( int i = 0; i < minimap.model->numBSPBrushes; ++i ) {
		const int index = minimap.model->firstBSPBrush + i;
		if ( !opaqueBrushes[index] ) continue;
		const auto& brush = bspBrushes[index];
		q3mapx::ColumnBrush item{};
		item.first = uint32_t(columns.planes.size());
		item.count = uint32_t(brush.numSides);
		for ( int j = 0; j < brush.numSides; ++j ) {
			const auto& p = bspPlanes[bspBrushSides[brush.firstSide + j].planeNum];
			columns.planes.push_back({p.normal().x(), p.normal().y(), p.normal().z(), p.dist()});
		}
		columns.brushes.push_back(item);
	}
	try { columns.buildIndex(minimap.mins.x(), minimap.mins.y(), minimap.mins.x() + minimap.size.x(), minimap.mins.y() + minimap.size.y()); }
	catch ( const std::exception& error ) { Error("Minimap index: %s", error.what()); }
	Sys_Printf("Column index: %zu brushes, %ux%u cells, %zu references (%.3f s)\n",
	    columns.brushes.size(), columns.grid, columns.grid, columns.references.size(), timer.elapsed_sec());
}

static bool MiniMapEvaluateSampleOffsets( int *bestj, int *bestk, float *bestval ){
	float val, dx, dy;
	int j, k;

	*bestj = *bestk = -1;
	*bestval = 3; /* max possible val is 2 */

	for ( j = 0; j < minimap.samples; ++j )
		for ( k = j + 1; k < minimap.samples; ++k )
		{
			dx = minimap.sample_offsets[2 * j + 0] - minimap.sample_offsets[2 * k + 0];
			dy = minimap.sample_offsets[2 * j + 1] - minimap.sample_offsets[2 * k + 1];
			if ( dx > +0.5f ) {
				dx -= 1;
			}
			if ( dx < -0.5f ) {
				dx += 1;
			}
			if ( dy > +0.5f ) {
				dy -= 1;
			}
			if ( dy < -0.5f ) {
				dy += 1;
			}
			val = dx * dx + dy * dy;
			if ( val < *bestval ) {
				*bestj = j;
				*bestk = k;
				*bestval = val;
			}
		}

	return *bestval < 3;
}

static void MiniMapMakeSampleOffsets(){
	int i, j, k, jj, kk;
	float val, valj, valk, sx, sy, rx, ry;

	Sys_Printf( "Generating good sample offsets (this may take a while)...\n" );

	/* start with entirely random samples */
	for ( i = 0; i < minimap.samples; ++i )
	{
		minimap.sample_offsets[2 * i + 0] = Random();
		minimap.sample_offsets[2 * i + 1] = Random();
	}

	for ( i = 0; i < 1000; ++i )
	{
		if ( MiniMapEvaluateSampleOffsets( &j, &k, &val ) ) {
			sx = minimap.sample_offsets[2 * j + 0];
			sy = minimap.sample_offsets[2 * j + 1];
			minimap.sample_offsets[2 * j + 0] = rx = Random();
			minimap.sample_offsets[2 * j + 1] = ry = Random();
			if ( !MiniMapEvaluateSampleOffsets( &jj, &kk, &valj ) ) {
				valj = -1;
			}
			minimap.sample_offsets[2 * j + 0] = sx;
			minimap.sample_offsets[2 * j + 1] = sy;

			sx = minimap.sample_offsets[2 * k + 0];
			sy = minimap.sample_offsets[2 * k + 1];
			minimap.sample_offsets[2 * k + 0] = rx;
			minimap.sample_offsets[2 * k + 1] = ry;
			if ( !MiniMapEvaluateSampleOffsets( &jj, &kk, &valk ) ) {
				valk = -1;
			}
			minimap.sample_offsets[2 * k + 0] = sx;
			minimap.sample_offsets[2 * k + 1] = sy;

			if ( valj > valk ) {
				if ( valj > val ) {
					/* valj is the greatest */
					minimap.sample_offsets[2 * j + 0] = rx;
					minimap.sample_offsets[2 * j + 1] = ry;
					i = -1;
				}
				else
				{
					/* valj is the greater and it is useless - forget it */
				}
			}
			else
			{
				if ( valk > val ) {
					/* valk is the greatest */
					minimap.sample_offsets[2 * k + 0] = rx;
					minimap.sample_offsets[2 * k + 1] = ry;
					i = -1;
				}
				else
				{
					/* valk is the greater and it is useless - forget it */
				}
			}
		}
		else{
			break;
		}
	}
}

static void MergeRelativePath( char *out, const char *absolute, const char *relative ){
	const char *endpos = absolute + strlen( absolute );
	while ( endpos != absolute && path_separator( endpos[-1] ) )
		--endpos;
	while ( strEqualPrefix( relative, "../" ) || strEqualPrefix( relative, "..\\" ) )
	{
		relative += 3;
		while ( endpos != absolute )
		{
			--endpos;
			if ( path_separator( *endpos ) ) {
				break;
			}
		}
		while ( endpos != absolute && path_separator( endpos[-1] ) )
			--endpos;
	}
	memcpy( out, absolute, endpos - absolute );
	out[endpos - absolute] = '/';
	strcpy( out + ( endpos - absolute + 1 ), relative );
}

int MiniMapBSPMain( Args& args ){
	char minimapFilename[1024];
	bool autolevel;
	float minimapSharpen;
	float border;
	byte *data4b, *p;
	float *q;
	int x, y;
	EMiniMapMode mode;
	bool keepaspect;

	/* arg checking */
	if ( args.empty() ) {
		Sys_Printf( "Usage: q3map2 [-v] -minimap [-size n] [-sharpen f] [-samples n | -random n] [-o filename.tga] [-minmax Xmin Ymin Zmin Xmax Ymax Zmax] <mapname>\n" );
		return 0;
	}

	/* load the BSP first */
	const char *fileName = args.takeBack();
	strcpy( source, ExpandArg( fileName ) );
	path_set_extension( source, ".bsp" );
	Sys_Printf( "Loading %s\n", source );
	LoadShaderInfo();
	LoadBSPFile( source );

	minimap.model = &bspModels[0];
	Vector3 mins = minimap.model->minmax.mins;
	Vector3 maxs = minimap.model->minmax.maxs;

	strClear( minimapFilename );
	minimapSharpen = g_game->miniMapSharpen;
	minimap.width  =
	minimap.height = g_game->miniMapSize;
	border         = g_game->miniMapBorder;
	keepaspect     = g_game->miniMapKeepAspect;
	mode           = g_game->miniMapMode;

	autolevel = false;
	minimap.samples = 1;
	minimap.sample_offsets = nullptr;
	minimap.boost = 1;
	minimap.brightness = 0;
	minimap.contrast = 1;

	/* process arguments */
	{
		while( args.takeArg( "-size" ) ) {
			minimap.width = minimap.height = ParseIntegerOption("-size", args.takeNext(), 1, 8192);
			Sys_Printf( "Image size set to %i\n", minimap.width );
		}
		while( args.takeArg( "-sharpen" ) ) {
			minimapSharpen = ParseFloatOption("-sharpen", args.takeNext(), -1, 16);
			Sys_Printf( "Sharpening coefficient set to %f\n", minimapSharpen );
		}
		while( args.takeArg( "-samples" ) ) {
			minimap.samples = ParseIntegerOption("-samples", args.takeNext(), 1, 256);
			Sys_Printf( "Samples set to %i\n", minimap.samples );
			free( minimap.sample_offsets );
			minimap.sample_offsets = safe_malloc( 2 * sizeof( *minimap.sample_offsets ) * minimap.samples );
			MiniMapMakeSampleOffsets();
		}
		while( args.takeArg( "-random" ) ) {
			minimap.samples = ParseIntegerOption("-random", args.takeNext(), 1, 4096);
			Sys_Printf( "Random samples set to %i\n", minimap.samples );
			free( minimap.sample_offsets );
			minimap.sample_offsets = nullptr;
		}
		while( args.takeArg( "-border" ) ) {
			border = ParseFloatOption("-border", args.takeNext(), 0, 0.49f);
			Sys_Printf( "Border set to %f\n", border );
		}
		while( args.takeArg( "-keepaspect" ) ) {
			keepaspect = true;
			Sys_Printf( "Keeping aspect ratio by letterboxing\n", border );
		}
		while( args.takeArg( "-nokeepaspect" ) ) {
			keepaspect = false;
			Sys_Printf( "Not keeping aspect ratio\n", border );
		}
		while( args.takeArg( "-o" ) ) {
			const char* output = args.takeNext();
			if ( strlen(output) >= sizeof(minimapFilename) ) Error("Minimap output path too long");
			strcpy( minimapFilename, output );
			Sys_Printf( "Output file name set to %s\n", minimapFilename );
		}
		while( args.takeArg( "-minmax" ) ) {
			mins[0] = ParseFloatOption("-minmax", args.takeNext(), -1e9f, 1e9f);
			mins[1] = ParseFloatOption("-minmax", args.takeNext(), -1e9f, 1e9f);
			mins[2] = ParseFloatOption("-minmax", args.takeNext(), -1e9f, 1e9f);
			maxs[0] = ParseFloatOption("-minmax", args.takeNext(), -1e9f, 1e9f);
			maxs[1] = ParseFloatOption("-minmax", args.takeNext(), -1e9f, 1e9f);
			maxs[2] = ParseFloatOption("-minmax", args.takeNext(), -1e9f, 1e9f);
			Sys_Printf( "Map mins/maxs overridden\n" );
		}
		while( args.takeArg( "-gray" ) ) {
			mode = EMiniMapMode::Gray;
			Sys_Printf( "Writing as white-on-black image\n" );
		}
		while( args.takeArg( "-black" ) ) {
			mode = EMiniMapMode::Black;
			Sys_Printf( "Writing as black alpha image\n" );
		}
		while( args.takeArg( "-white" ) ) {
			mode = EMiniMapMode::White;
			Sys_Printf( "Writing as white alpha image\n" );
		}
		while( args.takeArg( "-boost" ) ) {
			minimap.boost = ParseFloatOption("-boost", args.takeNext(), 0.001f, 1000);
			Sys_Printf( "Contrast boost set to %f\n", minimap.boost );
		}
		while( args.takeArg( "-brightness" ) ) {
			minimap.brightness = ParseFloatOption("-brightness", args.takeNext(), -1000, 1000);
			Sys_Printf( "Brightness set to %f\n", minimap.brightness );
		}
		while( args.takeArg( "-contrast" ) ) {
			minimap.contrast = ParseFloatOption("-contrast", args.takeNext(), -1000, 1000);
			Sys_Printf( "Contrast set to %f\n", minimap.contrast );
		}
		while( args.takeArg( "-autolevel" ) ) {
			autolevel = true;
			Sys_Printf( "Auto level enabled\n", border );
		}
		while( args.takeArg( "-noautolevel" ) ) {
			autolevel = false;
			Sys_Printf( "Auto level disabled\n", border );
		}
	}

	while ( args.takeArg("-backend") ) {
		const char* value = args.takeNext();
		if ( strEqual(value, "reference") ) referenceSampling = true;
		else if ( strEqual(value, "cpu") || strEqual(value, "auto") ) referenceSampling = false;
		else Error("Unknown minimap backend '%s'", value);
	}
	while ( args.takeArg("-seed") ) randomSeed = uint32_t(ParseIntegerOption("-seed", args.takeNext(), 0, INT_MAX));
	if ( !args.empty() ) Error("Unknown minimap option: %s", args.takeFront());
	for ( int axis = 0; axis < 3; ++axis )
		if ( !std::isfinite(mins[axis]) || !std::isfinite(maxs[axis]) || maxs[axis] <= mins[axis] )
			Error("Minimap bounds must have positive finite extent on every axis");
	MiniMapMakeMinsMaxs( mins, maxs, border, keepaspect );

	if ( strEmpty( minimapFilename ) ) {
		const CopiedString basename( PathFilename( source ) );
		const CopiedString path( PathFilenameless( source ) );
		char relativeMinimapFilename[1024];
		sprintf( relativeMinimapFilename, g_game->miniMapNameFormat, basename.c_str() );
		MergeRelativePath( minimapFilename, path.c_str(), relativeMinimapFilename );
		Sys_Printf( "Output file name automatically set to %s\n", minimapFilename );
	}
	Q_mkdir( CopiedString( PathFilenameless( minimapFilename ) ).c_str() );

	if ( minimapSharpen >= 0 ) {
		minimap.sharpen_centermult = 8 * minimapSharpen + 1;
		minimap.sharpen_boxmult    =    -minimapSharpen;
	}

	minimap.data1f = safe_malloc( minimap.width * minimap.height * sizeof( *minimap.data1f ) );
	data4b = safe_malloc( minimap.width * minimap.height * 4 );
	if ( minimapSharpen >= 0 ) {
		minimap.sharpendata1f = safe_malloc( minimap.width * minimap.height * sizeof( *minimap.data1f ) );
	}

	MiniMapSetupBrushes();

	if ( minimap.samples <= 1 ) {
		Sys_Printf( "\n--- MiniMapNoSupersampling (%d) ---\n", minimap.height );
		RunThreadsOnIndividual( minimap.height, true, MiniMapNoSupersampling, "MiniMapNoSupersampling" );
	}
	else
	{
		if ( minimap.sample_offsets ) {
			Sys_Printf( "\n--- MiniMapSupersampled (%d) ---\n", minimap.height );
			RunThreadsOnIndividual( minimap.height, true, MiniMapSupersampled, "MiniMapSupersampled" );
		}
		else
		{
			Sys_Printf( "\n--- MiniMapRandomlySupersampled (%d) ---\n", minimap.height );
			RunThreadsOnIndividual( minimap.height, true, MiniMapRandomlySupersampled, "MiniMapRandomlySupersampled" );
		}
	}

	if ( minimap.boost != 1 ) {
		Sys_Printf( "\n--- MiniMapContrastBoost (%d) ---\n", minimap.height );
		RunThreadsOnIndividual( minimap.height, true, MiniMapContrastBoost, "MiniMapContrastBoost", 0 );
	}

	if ( autolevel ) {
		Sys_Printf( "\n--- MiniMapAutoLevel (%d) ---\n", minimap.height );
		float mi = 1, ma = 0;
		float s, o;

		// TODO threads!
		q = minimap.data1f;
		for ( y = 0; y < minimap.height; ++y )
			for ( x = 0; x < minimap.width; ++x )
			{
				float v = *q++;
				value_minimize( mi, v );
				value_maximize( ma, v );
			}
		if ( ma > mi ) {
			s = 1 / ( ma - mi );
			o = mi / ( ma - mi );

			// equations:
			//   brightness + contrast * v
			// after autolevel:
			//   brightness + contrast * (v * s - o)
			// =
			//   (brightness - contrast * o) + (contrast * s) * v
			minimap.brightness = minimap.brightness - minimap.contrast * o;
			minimap.contrast *= s;

			Sys_Printf( "Auto level: Brightness changed to %f\n", minimap.brightness );
			Sys_Printf( "Auto level: Contrast changed to %f\n", minimap.contrast );
		}
		else{
			Sys_Printf( "Auto level: failed because all pixels are the same value\n" );
		}
	}

	if ( minimap.brightness != 0 || minimap.contrast != 1 ) {
		Sys_Printf( "\n--- MiniMapBrightnessContrast (%d) ---\n", minimap.height );
		RunThreadsOnIndividual( minimap.height, true, MiniMapBrightnessContrast, "MiniMapBrightnessContrast", 0 );
	}

	if ( minimap.sharpendata1f ) {
		Sys_Printf( "\n--- MiniMapSharpen (%d) ---\n", minimap.height );
		RunThreadsOnIndividual( minimap.height, true, MiniMapSharpen, "MiniMapSharpen", 0 );
		q = minimap.sharpendata1f;
	}
	else
	{
		q = minimap.data1f;
	}

	Sys_Printf( "\nConverting..." );

	switch ( mode )
	{
	case EMiniMapMode::Gray:
		p = data4b;
		for ( y = 0; y < minimap.height; ++y )
			for ( x = 0; x < minimap.width; ++x )
			{
				*p++ = std::clamp( *q++, 0.f, 255.f / 256.f ) * 256;
			}
		Sys_Printf( " writing to %s...", minimapFilename );
		WriteTGAGray( minimapFilename, data4b, minimap.width, minimap.height );
		break;
	case EMiniMapMode::Black:
		p = data4b;
		for ( y = 0; y < minimap.height; ++y )
			for ( x = 0; x < minimap.width; ++x )
			{
				*p++ = 0;
				*p++ = 0;
				*p++ = 0;
				*p++ = std::clamp( *q++, 0.f, 255.f / 256.f ) * 256;
			}
		Sys_Printf( " writing to %s...", minimapFilename );
		WriteTGA( minimapFilename, data4b, minimap.width, minimap.height );
		break;
	case EMiniMapMode::White:
		p = data4b;
		for ( y = 0; y < minimap.height; ++y )
			for ( x = 0; x < minimap.width; ++x )
			{
				*p++ = 255;
				*p++ = 255;
				*p++ = 255;
				*p++ = std::clamp( *q++, 0.f, 255.f / 256.f ) * 256;
			}
		Sys_Printf( " writing to %s...", minimapFilename );
		WriteTGA( minimapFilename, data4b, minimap.width, minimap.height );
		break;
	}

	Sys_Printf( " done.\n" );

	if( strEqual( g_game->arg, "unvanquished" ) ) {
		const auto minimapSidecarFilename = StringStream( PathExtensionless( minimapFilename ), ".minimap" );
		Sys_Printf( "Writing minimap sidecar to %s...", minimapSidecarFilename.c_str() );

		FILE *file = SafeOpenWrite( minimapSidecarFilename, "wt" );
		fprintf( file,
			"{\n"
			"\tbackgroundColor 0.0 0.0 0.0 0.333\n"
			"\tzone {\n"
			"\t\tbounds 0 0 0 0 0 0\n"
			"\t\timage \"minimaps/%s\" %f %f %f %f\n"
			"\t}\n"
			"}\n",
			CopiedString( PathFilename( source ) ).c_str(),
			mins[0], mins[1],
			maxs[0], maxs[1] );

		fclose( file );
		Sys_Printf( " done.\n" );
	}

	free(minimap.data1f);
	free(minimap.sharpendata1f);
	free(minimap.sample_offsets);
	free(data4b);
	/* return to sender */
	return 0;
}
