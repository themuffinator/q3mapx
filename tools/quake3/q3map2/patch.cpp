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
#include "map_input.h"
#include "authoring/patch_paint.h"
#include <charconv>
#include <limits>



/*
   ExpandLongestCurve() - ydnar
   finds length of quadratic curve specified and determines if length is longer than the supplied max
 */

#define APPROX_SUBDIVISION  8

static void ExpandLongestCurve( float *longestCurve, const Vector3& a, const Vector3& b, const Vector3& c ){
	int i;
	float t, len;
	Vector3 ab, bc, ac, pt, last, delta;


	/* calc vectors */
	ab = b - a;
	if ( VectorNormalize( ab ) < 0.125f ) {
		return;
	}
	bc = c - b;
	if ( VectorNormalize( bc ) < 0.125f ) {
		return;
	}
	ac = c - a;
	if ( VectorNormalize( ac ) < 0.125f ) {
		return;
	}

	/* if all 3 vectors are the same direction, then this edge is linear, so we ignore it */
	if ( vector3_dot( ab, bc ) > 0.99f && vector3_dot( ab, ac ) > 0.99f ) {
		return;
	}

	/* recalculate vectors */
	ab = b - a;
	bc = c - b;

	/* determine length */
	last = a;
	for ( i = 0, len = 0, t = 0; i < APPROX_SUBDIVISION; ++i, t += ( 1.0f / APPROX_SUBDIVISION ) )
	{
		/* calculate delta */
		delta = ab * ( 1.0f - t ) + bc * t;

		/* add to first point and calculate pt-pt delta */
		pt = a + delta;
		delta = pt - last;

		/* add it to length and store last point */
		len += vector3_length( delta );
		last = pt;
	}

	/* longer? */
	value_maximize( *longestCurve, len );
}



float PatchLongestCurve(mesh_view_t mesh){
	float length=0;
	for(int j=0;j+2<mesh.width;j+=2) for(int i=0;i+2<mesh.height;i+=2) {
		ExpandLongestCurve(&length,mesh[i][j].xyz,mesh[i][j+1].xyz,mesh[i][j+2].xyz);
		ExpandLongestCurve(&length,mesh[i][j].xyz,mesh[i+1][j].xyz,mesh[i+2][j].xyz);
	}
	return length;
}

/*
   ExpandMaxIterations() - ydnar
   determines how many iterations a quadratic curve needs to be subdivided with to fit the specified error
 */

static void ExpandMaxIterations( int *maxIterations, int maxError, const Vector3& a, const Vector3& b, const Vector3& c ){
	int i, j;
	Vector3 prev, next, mid;
	int numPoints, iterations;
	Vector3 points[ MAX_EXPANDED_AXIS ];


	/* initial setup */
	numPoints = 3;
	points[ 0 ] = a;
	points[ 1 ] = b;
	points[ 2 ] = c;

	/* subdivide */
	for ( i = 0; i + 2 < numPoints; i += 2 )
	{
		/* check subdivision limit */
		if ( numPoints + 2 >= MAX_EXPANDED_AXIS ) {
			break;
		}

		/* calculate new curve deltas */
		prev = points[ i + 1 ] - points[ i ];
		next = points[ i + 2 ] - points[ i + 1 ];
		mid = ( points[ i ] + points[ i + 1 ] * 2.0f + points[ i + 2 ] ) * 0.25f;

		/* see if this midpoint is off far enough to subdivide */
		if ( vector3_length( points[ i + 1 ] - mid ) < maxError ) {
			continue;
		}

		/* subdivide */
		numPoints += 2;

		/* create new points */
		prev = ( points[ i ] + points[ i + 1 ] ) * 0.5f;
		next = ( points[ i + 1 ] + points[ i + 2 ] ) * 0.5f;
		mid = ( prev + next ) * 0.5f;

		/* push points out */
		for ( j = numPoints - 1; j > i + 3; --j )
			points[ j ] = points[ j - 2 ];

		/* insert new points */
		points[ i + 1 ] = prev;
		points[ i + 2 ] = mid;
		points[ i + 3 ] = next;

		/* back up and recheck this set again, it may need more subdivision */
		i -= 2;
	}

	/* put the line on the curve */
	for ( i = 1; i < numPoints; i += 2 )
	{
		prev = ( points[ i ] + points[ i + 1 ] ) * 0.5f;
		next = ( points[ i ] + points[ i - 1 ] ) * 0.5f;
		points[ i ] = ( prev + next ) * 0.5f;
	}

	/* eliminate linear sections */
	for ( i = 0; i + 2 < numPoints; ++i )
	{
		/* create vectors */
		Vector3 delta = points[ i + 1 ] - points[ i ];
		const float len = VectorNormalize( delta );
		Vector3 delta2 = points[ i + 2 ] - points[ i + 1 ];
		const float len2 = VectorNormalize( delta2 );

		/* if either edge is degenerate, then eliminate it */
		if ( len < 0.0625f || len2 < 0.0625f || vector3_dot( delta, delta2 ) >= 1 ) {
			for ( j = i + 1; j + 1 < numPoints; ++j )
				points[ j ] = points[ j + 1 ];
			numPoints--;
			continue;
		}
	}

	/* the number of iterations is 2^(points - 1) - 1 */
	numPoints >>= 1;
	iterations = 0;
	while ( numPoints > 1 )
	{
		numPoints >>= 1;
		iterations++;
	}

	/* more? */
	value_maximize( *maxIterations, iterations );
}



/*
   ParsePatch()
   creates a mapDrawSurface_t from the patch text
 */

namespace {
struct PatchReader : MapInputReader
{
	PatchReader( int entity, int primitive ) : MapInputReader{ "patch", entity, primitive } {}
	int dimension( const char *field ) const {
		const double value = number( field );
		// Validate before float-to-int conversion, multiplication or allocation.
		if ( value < 3 || value > MAX_PATCH_SIZE || std::trunc( value ) != value ) fail( field );
		const int size = int( value );
		if ( size % 2 == 0 ) fail( field );
		// A long fractional spelling can round to an integer even in double.
		// Normalize the decimal digits exactly; retain integral 3.0 / 3e0 forms.
		const char *exponent = strpbrk( token, "eE" );
		const char *end = exponent ? exponent : token + strlen( token );
		int scale = 0;
		if ( exponent ) {
			const char *begin = exponent + 1 + ( exponent[1] == '+' );
			const auto parsed = std::from_chars( begin, token + strlen( token ), scale );
			if ( parsed.ec != std::errc{} ) fail( field );
		}
		std::string digits;
		bool fractional = false;
		for ( const char *p = token + ( token[0] == '+' ); p != end; ++p ) {
			if ( *p == '.' ) fractional = true;
			else {
				digits += *p;
				if ( fractional ) --scale;
			}
		}
		digits.erase( 0, digits.find_first_not_of( '0' ) );
		while ( !digits.empty() && digits.back() == '0' ) {
			digits.pop_back();
			++scale;
		}
		if ( scale != 0 || digits != std::to_string( size ) ) fail( field );
		return size;
	}
};
}

void ParsePatch( bool onlyLights, entity_t& mapEnt, int mapPrimitiveNum, int authoringVersion ){
	const PatchReader reader{ mapEnt.mapEntityNum, mapPrimitiveNum };
	bool degenerate;
	float longestCurve;
	int maxIterations;

	reader.match( "{" );

	/* get shader name */
	reader.next( true, "material name" );
	if ( strEmpty( token ) ) reader.fail( "nonempty material name" );
	const String64 shader( "textures/", token );

	reader.match( "(" );
	const int width = reader.dimension( "odd integer patch width in 3..31" );
	const int height = reader.dimension( "odd integer patch height in 3..31" );
	// The three legacy header fields are unused, but must still be numeric.
	for ( int i = 0; i < 3; ++i ) reader.number( "finite patch header number" );
	reader.match( ")" );
	const int authoredSampleSize = authoringVersion ? reader.surfaceSampleSize() : 0;
	int paintMode = 0, paintSubdivisions = 0;
	if ( authoringVersion >= 2 ) {
		reader.match( "vertexRGB" );
		reader.next( false, "vertex RGB mode: lighting or material" );
		if ( !q3mapx::authoring::parsePaintMode( token, paintMode ) ) reader.fail( "vertex RGB mode: lighting or material" );
		reader.match( "paintSubdivisions" );
		reader.next( false, "paint subdivisions: 1, 2, 4, 8, 16 or 32" );
		if ( !q3mapx::authoring::parsePaintSubdivisions( token, paintSubdivisions ) )
			reader.fail( "paint subdivisions: 1, 2, 4, 8, 16 or 32" );
		if ( !q3mapx::authoring::paintMeshFits( width, height, paintSubdivisions ) ) reader.fail( "paint tessellation within 65536 vertices" );
	}
	mesh_t m( width, height );
	const int numVerts = m.numVerts();

	reader.match( "(" );
	for ( int j = 0; j < m.width; ++j )
	{
		reader.match( "(" );
		for ( int i = 0; i < m.height; ++i )
		{
			// Initialize every field before interpolation or BSP publication.
			m[ i ][ j ] = c_bspDrawVert_t0;
			reader.match( "(" );
			for ( int axis = 0; axis < 3; ++axis )
				m[ i ][ j ].xyz[axis] = reader.coordinate( "finite patch position in -65536..65536", MAX_WORLD_COORD );
			for ( int axis = 0; axis < 2; ++axis )
				m[ i ][ j ].st[axis] = reader.coordinate( "finite representable patch texture coordinate", std::numeric_limits<float>::max() );
			Color4b color( 255 );
			if ( paintMode ) for ( int channel = 0; channel < 4; ++channel ) {
				reader.next( false, "RGBA byte: decimal integer in 0..255" );
				int value;
				if ( !q3mapx::authoring::parsePaintByte( token, value ) ) reader.fail( "RGBA byte: decimal integer in 0..255" );
				if ( paintMode == q3mapx::authoring::alphaPaint && channel < 3 && value != 255 )
					reader.fail( "RGB 255 255 255 in lighting mode (use material mode for RGB paint)" );
				color[channel] = value;
			}
			reader.match( ")" );
			m[ i ][ j ].color.fill( color );
		}
		reader.match( ")" );
	}
	reader.match( ")" );

	// if brush primitives format, we may have some epairs to ignore here
	reader.next( true, "closing brace or patch metadata" );
	if ( !authoringVersion && !TokenIs( "}" ) && ( g_brushType == EBrushType::Bp || g_brushType == EBrushType::Undefined ) ) {
		std::list<epair_t> dummy;
		ParseEPair( dummy );
	}
	else{
		UnGetToken();
	}

	reader.match( "}" );
	reader.match( "}" );

	/* short circuit */
	if ( noCurveBrushes || onlyLights ) {
		return;
	}


	/* ydnar: delete and warn about degenerate patches */
	Vector4 delta( 0 );
	degenerate = true;

	/* find first valid vector */
	for ( int i = 1; i < numVerts && delta[ 3 ] == 0; ++i )
	{
		delta.vec3() = m.verts()[ 0 ].xyz - m.verts()[ i ].xyz;
		delta[ 3 ] = VectorNormalize( delta.vec3() );
	}

	/* secondary degenerate test */
	if ( delta[ 3 ] == 0 ) {
		degenerate = true;
	}
	else
	{
		/* if all vectors match this or are zero, then this is a degenerate patch */
		for ( int i = 1; i < numVerts && degenerate; ++i )
		{
			Vector4 delta2( m.verts()[ 0 ].xyz - m.verts()[ i ].xyz, 0 );
			delta2[ 3 ] = VectorNormalize( delta2.vec3() );
			if ( delta2[ 3 ] != 0 ) {
				/* create inverse vector */
				Vector4 delta3( delta2 );
				vector3_negate( delta3.vec3() );

				/* compare */
				if ( !VectorCompare( delta.vec3(), delta2.vec3() ) && !VectorCompare( delta.vec3(), delta3.vec3() ) ) {
					degenerate = false;
				}
			}
		}
	}

	/* warn and select degenerate patch */
	if ( degenerate ) {
		xml_Select( "degenerate patch", mapEnt.mapEntityNum, mapPrimitiveNum, false );
		return;
	}

	/* find longest curve on the mesh */
	longestCurve = 0;
	maxIterations = 0;
	for ( int j = 0; j + 2 < m.width; j += 2 )
	{
		for ( int i = 0; i + 2 < m.height; i += 2 )
		{
			ExpandLongestCurve( &longestCurve, m[ i ][ j ].xyz, m[ i ][ j + 1 ].xyz, m[ i ][ j + 2 ].xyz );      /* row */
			ExpandLongestCurve( &longestCurve, m[ i ][ j ].xyz, m[ i + 1 ][ j ].xyz, m[ i + 2 ][ j ].xyz );      /* col */
			ExpandMaxIterations( &maxIterations, patchSubdivisions, m[ i ][ j ].xyz, m[ i ][ j + 1 ].xyz, m[ i ][ j + 2 ].xyz );     /* row */
			ExpandMaxIterations( &maxIterations, patchSubdivisions, m[ i ][ j ].xyz, m[ i + 1 ][ j ].xyz, m[ i + 2 ][ j ].xyz  );    /* col */
		}
	}

	/* allocate patch mesh */
	parseMesh_t& pm = mapEnt.patches.emplace_front(); // zero initialize

	/* ydnar: add entity/brush numbering */
	pm.entityNum = mapEnt.mapEntityNum;
	pm.brushNum = mapPrimitiveNum;
	pm.lightmapSampleSizeOverride = authoredSampleSize;
	pm.paintMode = paintMode;
	pm.paintSubdivisions = paintSubdivisions;

	/* set shader */
	pm.shaderInfo = &ShaderInfoForShader( shader );
	if ( paintMode && ( pm.shaderInfo->indexed || pm.shaderInfo->autosprite ) )
		reader.fail( "paint material without indexed blending or autosprite geometry" );

	/* set mesh */
	pm.mesh = std::move( m );

	/* set longest curve */
	pm.longestCurve = longestCurve;
	pm.maxIterations = maxIterations;
}



struct groupMesh_t
{
	parseMesh_t& mesh;
	bool *bordering;
	bool grouped;
	bool group;
};

/*
   GrowGroup_r()
   recursively adds patches to a lod group
 */

static void GrowGroup_r( groupMesh_t& mesh, groupMesh_t& other, std::vector<groupMesh_t>& meshes ){
	/* early out check */
	if ( other.group ) {
		return;
	}

	/* set it */
	other.group = true;

	/* check maximums */
	value_maximize( mesh.mesh.longestCurve, other.mesh.longestCurve );
	value_maximize( mesh.mesh.maxIterations, other.mesh.maxIterations );

	/* walk other patches */
	for ( size_t i = 0; i < meshes.size(); ++i )
	{
		if ( other.bordering[i] ) {
			GrowGroup_r( mesh, meshes[i], meshes );
		}
	}
}


/*
   PatchMapDrawSurfs()
   any patches that share an edge need to choose their
   level of detail as a unit, otherwise the edges would
   pull apart.
 */

void PatchMapDrawSurfs( entity_t& e ){
	/* note it */
	Sys_FPrintf( SYS_VRB, "--- PatchMapDrawSurfs ---\n" );

	std::vector<groupMesh_t> meshes;
	for ( parseMesh_t& pm : e.patches ){
		meshes.push_back( { .mesh = pm, .grouped = false } );
	}
	if ( meshes.empty() ) {
		return;
	}

	std::unique_ptr<bool[]> bordering( new bool[meshes.size() * meshes.size()] ); // mesh<->mesh bordering matrix
	for( size_t i = 0; i < meshes.size(); ++i ){
		meshes[i].bordering = &bordering[meshes.size() * i]; // reference matrix portion relevant to this mesh; this->bool[meshes.size()]
	}

	// build the bordering matrix
	for ( size_t m1 = 0; m1 < meshes.size(); ++m1 ) {
		meshes[m1].bordering[m1] = true; // mark mesh as bordered with self

		for ( size_t m2 = m1 + 1; m2 < meshes.size(); ++m2 ) {
			const mesh_t& mesh1 = meshes[m1].mesh.mesh;
			const mesh_t& mesh2 = meshes[m2].mesh.mesh;

			meshes[m1].bordering[m2] =
			meshes[m2].bordering[m1] =
				std::ranges::any_of( mesh1, [mesh2]( const bspDrawVert_t& v1 ){
					return std::ranges::any_of( mesh2, [v1]( const bspDrawVert_t& v2 ){
						return vector3_equal_epsilon( v1.xyz, v2.xyz, 1.f );
					} );
				} );
		}
	}

	/* build groups */
	int groupCount = 0;
	for ( auto& mesh : meshes )
	{
		/* start a new group */
		if ( !mesh.grouped ) {
			groupCount++;
		}

		/* recursively find all patches that belong in the same group */
		for( auto& m : meshes )
			m.group = false;
		GrowGroup_r( mesh, mesh, meshes );

		/* bound them */
		MinMax bounds;
		for ( auto& m : meshes )
		{
			if ( m.group ) {
				m.grouped = true;
				for( const bspDrawVert_t& v : m.mesh.mesh )
					bounds.extend( v.xyz );
			}
		}

		/* debug code */
		//%	Sys_Printf( "Longest curve: %f Iterations: %d\n", mesh.mesh.longestCurve, mesh.mesh.maxIterations );

		/* create drawsurf */
		mapDrawSurface_t& ds = DrawSurfaceForMesh( e, mesh.mesh );   /* ydnar */
		ds.bounds = bounds;
	}

	/* emit some statistics */
	Sys_FPrintf( SYS_VRB, "%9zu patches\n", meshes.size() );
	Sys_FPrintf( SYS_VRB, "%9d patch LOD groups\n", groupCount );
}
