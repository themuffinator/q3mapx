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
#include "bspfile_rbsp.h"
#include "qspatial.h"
#include "decompile.h"
#include "q3mapx/affine.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <map>

struct DecompileStats {
	size_t brushes = 0, skippedBrushes = 0, faces = 0, matchedFaces = 0;
	size_t fallbackFaces = 0, degenerateUVs = 0, degenerateTriangles = 0;
	size_t patches = 0, approximateQuakeFaces = 0;
};
static DecompileStats recovery;
static std::vector<bool> detailBrushes;



/*
   ConvertBrush()
   exports a map brush
 */

struct BspTriangleRef
{
	bspSurfaceType_t surfaceType;
	TriRef tri;
	MinMax minmax; // X is on c_spatial_sort_direction

	BspTriangleRef( bspSurfaceType_t surfaceType, const bspDrawVert_t& v0, const bspDrawVert_t& v1, const bspDrawVert_t& v2 )
	:	surfaceType( surfaceType ),
		tri{ &v0, &v1, &v2 }
	{
		minmax.extend( Vector3( spatial_distance( v0.xyz ), v0.xyz.y(), v0.xyz.z() ) );
		minmax.extend( Vector3( spatial_distance( v1.xyz ), v1.xyz.y(), v1.xyz.z() ) );
		minmax.extend( Vector3( spatial_distance( v2.xyz ), v2.xyz.y(), v2.xyz.z() ) );
	}
	bool operator<( const BspTriangleRef& other ) const noexcept {
		return minmax.mins.x() < other.minmax.mins.x();
	}
};

class ModelTriangles
{
	struct ShaderTriangles {
		struct Node {
			MinMax bounds;
			size_t first = 0, count = 0, left = 0, right = 0;
		};
		std::vector<BspTriangleRef> triangles;
		std::vector<Node> nodes;
		size_t buildNode( size_t first, size_t count ){
			const size_t index = nodes.size();
			nodes.emplace_back();
			MinMax bounds;
			for ( size_t i = first; i < first + count; ++i ) bounds.extend( triangles[i].minmax );
			nodes[index].bounds = bounds;
			if ( count <= 8 ) {
				nodes[index].first = first;
				nodes[index].count = count;
			}
			else {
				const Vector3 size = bounds.maxs - bounds.mins;
				const int axis = size.x() > size.y() ? ( size.x() > size.z() ? 0 : 2 ) : ( size.y() > size.z() ? 1 : 2 );
				const size_t half = count / 2;
				std::nth_element( triangles.begin() + first, triangles.begin() + first + half, triangles.begin() + first + count,
				    [axis]( const auto& a, const auto& b ){
				        return ( double( a.minmax.mins[axis] ) + a.minmax.maxs[axis] )
				             < ( double( b.minmax.mins[axis] ) + b.minmax.maxs[axis] );
				    } );
				nodes[index].left = buildNode( first, half );
				nodes[index].right = buildNode( first + half, count - half );
			}
			return index;
		}
		void build(){
			if ( triangles.empty() ) return;
			nodes.reserve( triangles.size() );
			buildNode( 0, triangles.size() );
		}
		template<typename Visitor>
		void queryNode( size_t index, const MinMax& bounds, const Visitor& visitor ) const {
			const Node& node = nodes[index];
			if ( !bounds.test( node.bounds ) ) return;
			if ( node.count ) {
				for ( size_t i = node.first; i < node.first + node.count; ++i ) visitor( triangles[i] );
			}
			else {
				queryNode( node.left, bounds, visitor );
				queryNode( node.right, bounds, visitor );
			}
		}
		template<typename Visitor>
		void query( const MinMax& bounds, const Visitor& visitor ) const {
			if ( !nodes.empty() ) queryNode( 0, bounds, visitor );
		}
	};
	std::map<CopiedString, ShaderTriangles> m_modelTriangles;
public:
	ModelTriangles( const bspModel_t& model ){
		for ( int surface = 0; surface < model.numBSPSurfaces; ++surface )
		{
			const auto& s = bspDrawSurfaces[model.firstBSPSurface + surface];
			if ( s.surfaceType == MST_PLANAR || s.surfaceType == MST_TRIANGLE_SOUP ) {
				auto& vec = m_modelTriangles[bspShaders[s.shaderNum].shader].triangles;
				for ( int t = 0; t + 3 <= s.numIndexes; t += 3 )
				{
					BspTriangleRef triangle( s.surfaceType,
						bspDrawVerts[s.firstVert + bspDrawIndexes[s.firstIndex + t + 0]],
						bspDrawVerts[s.firstVert + bspDrawIndexes[s.firstIndex + t + 1]],
						bspDrawVerts[s.firstVert + bspDrawIndexes[s.firstIndex + t + 2]]
					);
					Plane3f plane;
					if ( !PlaneFromPoints( plane, triangle.tri[0]->xyz, triangle.tri[1]->xyz, triangle.tri[2]->xyz ) ) {
						++recovery.degenerateTriangles;
						continue;
					}
					vec.push_back( triangle );
				}
			}
		}

		for( auto& [ k, values ] : m_modelTriangles ) values.build();
	}
	TriRef GetBestSurfaceTriangleMatchForBrushside( const side_t& buildSide ) const {
		const float nepsilon = normalEpsilon * 100; // default target 0.005 - gives worthy results practically
		const float depsilon = 2;
		winding_t polygon;
		float bestarea = 0;
		float thisarea;
		const plane_t& buildPlane = mapplanes[buildSide.planenum];
		int matches = 0;

		// first, start out with NULLs
		TriRef bestVert{ nullptr };

		// Query triangles belonging to the face material.
		if( const auto triangles = m_modelTriangles.find( buildSide.shaderInfo->shader.c_str() ); triangles != m_modelTriangles.end() ){
			MinMax minmax;
			for( const Vector3& v : buildSide.winding )
				minmax.extend( Vector3( spatial_distance( v ), v.y(), v.z() ) );
			minmax.mins -= Vector3( 32, depsilon, depsilon ); // 32 helps to spot more triangles, when brush is noticeably smaller
			minmax.maxs += Vector3( 32, depsilon, depsilon ); // e.g. produced by original model autoclip

			triangles->second.query( minmax, [&]( const BspTriangleRef& triangle ) {
				const auto* tri = &triangle;
				if ( !minmax.test( tri->minmax ) ) {
					return;
				}
				const TriRef& vert = tri->tri;
				if ( tri->surfaceType == MST_PLANAR
				&& VectorCompare( vert[0]->normal, vert[1]->normal )
				&& VectorCompare( vert[1]->normal, vert[2]->normal ) ) {
					if ( !vector3_equal_epsilon( vert[0]->normal, buildPlane.normal(), float( nepsilon ) )
					  || !vector3_equal_epsilon( vert[1]->normal, buildPlane.normal(), float( nepsilon ) )
					  || !vector3_equal_epsilon( vert[2]->normal, buildPlane.normal(), float( nepsilon ) ) ) {
						return;
					}
				}
				else
				{
					// this is more prone to roundoff errors, but with embedded
					// models, there is no better way
					Plane3f plane;
					PlaneFromPoints( plane, vert[0]->xyz, vert[1]->xyz, vert[2]->xyz );
					if ( !vector3_equal_epsilon( plane.normal(), buildPlane.normal(), float( nepsilon ) ) ) {
						return;
					}
				}

				if ( std::fabs( plane3_distance_to_point( buildPlane.plane, vert[0]->xyz ) ) > depsilon
				  || std::fabs( plane3_distance_to_point( buildPlane.plane, vert[1]->xyz ) ) > depsilon
				  || std::fabs( plane3_distance_to_point( buildPlane.plane, vert[2]->xyz ) ) > depsilon ) {
					return;
				}
				// Okay. Correct surface type, correct shader, correct plane. Let's start with the business...
				// we now need to generate the plane spanned by normal and (v2 - v1).
				Plane3f planes[3]{
					{ VectorNormalized( vector3_cross( vert[ 0 ]->xyz - vert[ 2 ]->xyz, buildPlane.normal() ) ), 0 },
					{ VectorNormalized( vector3_cross( vert[ 1 ]->xyz - vert[ 0 ]->xyz, buildPlane.normal() ) ), 0 },
					{ VectorNormalized( vector3_cross( vert[ 2 ]->xyz - vert[ 1 ]->xyz, buildPlane.normal() ) ), 0 },
				};
				planes[ 0 ].dist() = vector3_dot( vert[ 2 ]->xyz, planes[ 0 ].normal() );
				planes[ 1 ].dist() = vector3_dot( vert[ 0 ]->xyz, planes[ 1 ].normal() );
				planes[ 2 ].dist() = vector3_dot( vert[ 1 ]->xyz, planes[ 2 ].normal() );

				polygon = buildSide.winding;
				for ( const Plane3f& plane : planes )
				{
					ChopWindingInPlace( polygon, plane, distanceEpsilon );
					if ( polygon.empty() ) {
						goto exwinding;
					}
				}
				thisarea = WindingArea( polygon );
				if ( thisarea > 0 ) {
					++matches;
				}
				if ( thisarea > bestarea ) {
					bestarea = thisarea;
					bestVert = vert;
				}
		exwinding:
				;
			} );
		}
		//if( !striEqualPrefix( buildSide.shaderInfo->shader, "textures/common/" ) )
		//	fprintf( stderr, "brushside with %s: %d matches (%f area)\n", buildSide.shaderInfo->shader, matches, bestarea );
		return bestVert;
	}
};

#define FRAC( x ) ( ( x ) - floor( x ) )
static void ConvertOriginBrush( FILE *f, int num, const Vector3& origin, EBrushType brushType ){
	const int ext = 8; // extent, box size is 2x
	const int size = ext * 2;
	const int texSize = 64; // can find out from shader
	const float texScale = float( size ) / texSize;
	static const char * const shader = strEqual( g_game->arg, "sof2" )
	                                || strEqual( g_game->arg, "ja" )
	                                || strEqual( g_game->arg, "jk2" )? "system/origin" : "common/origin";
	// 6: +Z +Y +X -Z -Y -X
	char pattern[6][7][4] = {
		{ "+++", "+-+", "-++", "-  ", " + ", " - ", "-  " },
		{ "+++", "-++", "++-", "-  ", "  +", "+  ", "  +" },
		{ "+++", "++-", "+-+", " - ", "  +", " - ", "  +" },
		{ "---", "+--", "-+-", "-  ", " + ", " - ", "+  " },
		{ "---", "--+", "+--", "-  ", "  +", "-  ", "  +" },
		{ "---", "-+-", "--+", " - ", "  +", " + ", "  +" }
	};
#define S( a, b, c ) ( pattern[a][b][c] == '+' ? +1 : pattern[a][b][c] == '-' ? -1 : 0 )

	/* start brush */
	fprintf( f, "\t// brush %d\n", num );
	fprintf( f, "\t{\n" );
	if ( brushType == EBrushType::Bp ) {
		fprintf( f, "\tbrushDef\n" );
		fprintf( f, "\t{\n" );
	}
	/* print brush side */
	/* ( 640 24 -224 ) ( 448 24 -224 ) ( 448 -232 -224 ) common/caulk         0            48      90 0.5 0.5 0 0 0 */
	/* ( 640 24 -224 ) ( 448 24 -224 ) ( 448 -232 -224 ) common/caulk [ 1 0 0 0 ] [ 0 -1 0 48 ]    90 0.5 0.5 0 0 0 */
	/* ( 640 24 -224 ) ( 448 24 -224 ) ( 448 -232 -224 ) ( ( 0 0.03125 0 ) ( -0.03125 0 0.75 ) ) common/caulk 0 0 0 */

	for ( int i = 0; i < 6; ++i )
	{
		fprintf( f, "\t\t( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ",
		         origin[0] + ext * S( i, 0, 0 ), origin[1] + ext * S( i, 0, 1 ), origin[2] + ext * S( i, 0, 2 ),
		         origin[0] + ext * S( i, 1, 0 ), origin[1] + ext * S( i, 1, 1 ), origin[2] + ext * S( i, 1, 2 ),
		         origin[0] + ext * S( i, 2, 0 ), origin[1] + ext * S( i, 2, 1 ), origin[2] + ext * S( i, 2, 2 )
		       );
		if ( brushType == EBrushType::Quake ){
			fprintf( f, "%s %.8f %.8f 0 %.8f %.8f 0 0 0\n",
			         shader,
			         FRAC( ( S( i, 3, 0 ) * origin[0] + S( i, 3, 1 ) * origin[1] + S( i, 3, 2 ) * origin[2] ) / size + 0.5 ) * texSize,
			         FRAC( ( S( i, 4, 0 ) * origin[0] + S( i, 4, 1 ) * origin[1] + S( i, 4, 2 ) * origin[2] ) / size + 0.5 ) * texSize,
			         texScale, texScale
			       );
		}
		else if ( brushType == EBrushType::Valve220 ){
			const Vector3 texX( S( i, 3, 0 ), S( i, 3, 1 ), S( i, 3, 2 ) );
			const Vector3 texY( S( i, 4, 0 ), S( i, 4, 1 ), S( i, 4, 2 ) );
			fprintf( f, "%s [ %.8f %.8f %.8f %.8f ] [ %.8f %.8f %.8f %.8f ] 0 %.8f %.8f 0 0 0\n",
			         shader,
			         texX.x(), texX.y(), texX.z(),
			         FRAC( ( S( i, 3, 0 ) * origin[0] + S( i, 3, 1 ) * origin[1] + S( i, 3, 2 ) * origin[2] ) / size + 0.5 ) * texSize,
			         texY.x(), texY.y(), texY.z(),
			         FRAC( ( S( i, 4, 0 ) * origin[0] + S( i, 4, 1 ) * origin[1] + S( i, 4, 2 ) * origin[2] ) / size + 0.5 ) * texSize,
			         texScale, texScale
			       );
		}
		else if ( brushType == EBrushType::Bp ) {
			fprintf( f, "( ( %.8f %.8f %.8f ) ( %.8f %.8f %.8f ) ) %s 0 0 0\n",
			         1.0f / size, 0.0f, FRAC( ( S( i, 5, 0 ) * origin[0] + S( i, 5, 1 ) * origin[1] + S( i, 5, 2 ) * origin[2] ) / size + 0.5 ),
			         0.0f, 1.0f / size, FRAC( ( S( i, 6, 0 ) * origin[0] + S( i, 6, 1 ) * origin[1] + S( i, 6, 2 ) * origin[2] ) / size + 0.5 ),
			         shader
			       );
		}
	}
#undef S

	/* end brush */
	if ( brushType == EBrushType::Bp ) {
		fprintf( f, "\t}\n" );
	}
	fprintf( f, "\t}\n\n" );
}

static void bspBrush_to_buildBrush( const bspBrush_t& brush ){
	/* clear out build brush */
	buildBrush.sides.clear();

	bool modelclip = false;
	/* try to guess if this is model clip */
	if ( g_decompile_modelClip ){
		int notNoShader = 0;
		modelclip = true;
		for ( int sideIndex = 0; sideIndex < brush.numSides; ++sideIndex )
		{
			const auto& side = bspBrushSides[brush.firstSide + sideIndex];
			/* get shader */
			if ( side.shaderNum < 0 || side.shaderNum >= int( bspShaders.size() ) ) {
				continue;
			}
			const bspShader_t& shader = bspShaders[ side.shaderNum ];
			//"noshader" happens on modelclip and unwanted sides ( usually breaking complex brushes )
			if( !striEqual( shader.shader, "noshader" ) ){
				notNoShader++;
			}
			if( notNoShader > 1 ){
				modelclip = false;
				break;
			}
		}
	}

	/* iterate through bsp brush sides */
	for ( int sideIndex = 0; sideIndex < brush.numSides; ++sideIndex )
	{
		const auto& side = bspBrushSides[brush.firstSide + sideIndex];
		/* get shader */
		if ( side.shaderNum < 0 || side.shaderNum >= int( bspShaders.size() ) ) {
			continue;
		}
		const bspShader_t& shader = bspShaders[ side.shaderNum ];
		//"noshader" happens on modelclip and unwanted sides ( usually breaking complex brushes )
		if( striEqual( shader.shader, "default" ) || ( striEqual( shader.shader, "noshader" ) && !modelclip ) )
			continue;

		/* add build side */
		buildBrush.sides.emplace_back();

		/* tag it */
		buildBrush.sides.back().shaderInfo = &ShaderInfoForShader( shader.shader );
		buildBrush.sides.back().planenum = side.planeNum;
	}
}

static void ConvertBrushFast( FILE *f, int bspBrushNum, const Vector3& origin, EBrushType brushType ){

	bspBrush_to_buildBrush( bspBrushes[bspBrushNum] );

	if ( !CreateBrushWindings( buildBrush ) ) {
		++recovery.skippedBrushes;
		return;
	}

	++recovery.brushes;
	/* start brush */
	fprintf( f, "\t// brush %d\n", bspBrushNum );
	fprintf( f, "\t{\n" );
	if ( brushType == EBrushType::Bp ) {
		fprintf( f, "\tbrushDef\n" );
		fprintf( f, "\t{\n" );
	}

	/* iterate through build brush sides */
	for ( side_t& buildSide : buildBrush.sides )
	{
		/* get plane */
		const plane_t& buildPlane = mapplanes[ buildSide.planenum ];

		/* dummy check */
		if ( buildSide.shaderInfo == nullptr || buildSide.winding.empty() ) {
			continue;
		}
		++recovery.faces;
		++recovery.fallbackFaces;

		/* get texture name */
		const char *texture = striEqualPrefix( buildSide.shaderInfo->shader, "textures/" )
		                      ? buildSide.shaderInfo->shader + 9
		                      : buildSide.shaderInfo->shader;

		Vector3 pts[ 3 ];
		{
			Vector3 vecs[ 2 ];
			MakeNormalVectors( buildPlane.normal(), vecs[ 0 ], vecs[ 1 ] );
			pts[ 0 ] = buildPlane.normal() * buildPlane.dist() + origin;
			pts[ 1 ] = pts[ 0 ] + vecs[ 0 ] * 256.0f;
			pts[ 2 ] = pts[ 0 ] + vecs[ 1 ] * 256.0f;
		}

		{
			fprintf( f, "\t\t( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ",
			         pts[ 0 ][ 0 ], pts[ 0 ][ 1 ], pts[ 0 ][ 2 ],
			         pts[ 1 ][ 0 ], pts[ 1 ][ 1 ], pts[ 1 ][ 2 ],
			         pts[ 2 ][ 0 ], pts[ 2 ][ 1 ], pts[ 2 ][ 2 ]
			       );
			if ( brushType == EBrushType::Quake ) {
				fprintf( f, "%s %.8f %.8f %.8f %.8f %.8f 0 0 0\n",
				         texture,
				         0.0f, 0.0f, 0.0f, 0.5f, 0.5f
				       );
			}
			else if ( brushType == EBrushType::Valve220 ) {
				Vector3 texX, texY;
				ComputeAxisBase( buildPlane.normal(), texX, texY );
				fprintf( f, "%s [ %.8f %.8f %.8f %.8f ] [ %.8f %.8f %.8f %.8f ] 0 0.5 0.5 0 0 0\n",
				         texture,
				         texX.x(), texX.y(), texX.z(), 0.f,
				         texY.x(), texY.y(), texY.z(), 0.f
				       );
			}
			else if ( brushType == EBrushType::Bp ) {
				fprintf( f, "( ( %.8f %.8f %.8f ) ( %.8f %.8f %.8f ) ) %s 0 0 0\n",
				         1.0f / 32.0f, 0.0f, 0.0f,
				         0.0f, 1.0f / 32.0f, 0.0f,
				         texture
				       );
			}
		}
	}

	/* end brush */
	if ( brushType == EBrushType::Bp ) {
		fprintf( f, "\t}\n" );
	}
	fprintf( f, "\t}\n\n" );
}

static void ConvertBrush( FILE *f, int bspBrushNum, const Vector3& origin, EBrushType brushType, const ModelTriangles& modelTriangles ){

	bspBrush_to_buildBrush( bspBrushes[bspBrushNum] );

	/* make brush windings */
	if ( !CreateBrushWindings( buildBrush ) ) {
		++recovery.skippedBrushes;
		return;
	}

	++recovery.brushes;
	/* start brush */
	fprintf( f, "\t// brush %d\n", bspBrushNum );
	fprintf( f, "\t{\n" );
	if ( brushType == EBrushType::Bp ) {
		fprintf( f, "\tbrushDef\n" );
		fprintf( f, "\t{\n" );
	}

	// Build this membership table once, instead of scanning every leaf for each brush.
	const int contentFlag = detailBrushes[bspBrushNum]
	    && !( bspShaders[bspBrushes[bspBrushNum].shaderNum].contentFlags
	          & GetRequiredSurfaceParm<"structural">().contentFlags ) ? C_DETAIL : 0;

	/* iterate through build brush sides */
	for ( side_t& buildSide : buildBrush.sides )
	{
		/* get plane */
		const plane_t& buildPlane = mapplanes[ buildSide.planenum ];

		/* dummy check */
		if ( buildSide.shaderInfo == nullptr || buildSide.winding.empty() ) {
			continue;
		}
		++recovery.faces;

		// st-texcoords -> texMat block
		// start out with dummy
		buildSide.texMat[0] = { 1 / 32.0, 0, 0 };
		buildSide.texMat[1] = { 0, 1 / 32.0, 0 };

		// Find the rendered triangle with the largest overlap on this brush side.
		// surface format:
		//   - meshverts point in pairs of three into verts
		//   - (triangles)
		//   - find the triangle that has most in common with our
		const TriRef vert = modelTriangles.GetBestSurfaceTriangleMatchForBrushside( buildSide );

		/* get texture name */
		const char *texture = striEqualPrefix( buildSide.shaderInfo->shader, "textures/" )
		                      ? buildSide.shaderInfo->shader + 9
		                      : buildSide.shaderInfo->shader;

		Vector3 pts[ 3 ];
		/* recheck and fix winding points, fails occur somehow */
		int match = 0;
		for ( const Vector3& p : buildSide.winding ){
			if ( std::fabs( plane3_distance_to_point( buildPlane.plane, p ) ) < distanceEpsilon ) {
				pts[ match ] = p;
				match++;
				/* got 3 fine points? */
				if( match > 2 )
					break;
			}
		}

		if( match > 2 ){
			//Sys_Printf( "pointsKK " );
			if ( Plane3f testplane; PlaneFromPoints( testplane, pts ) ){
				if( !PlaneEqual( buildPlane, testplane ) ){
					//Sys_Printf( "1: %f %f %f %f\n2: %f %f %f %f\n", buildPlane->normal[0], buildPlane->normal[1], buildPlane->normal[2], buildPlane->dist, testplane[0], testplane[1], testplane[2], testplane[3] );
					match--;
					//Sys_Printf( "planentEQ " );
				}
			}
			else{
				match--;
			}
		}


		if( match > 2 ){
			//Sys_Printf( "ok " );
			/* offset by origin */
			for ( Vector3& pt : pts )
				pt += origin;
		}
		else{
			Vector3 vecs[ 2 ];
			MakeNormalVectors( buildPlane.normal(), vecs[ 0 ], vecs[ 1 ] );
			pts[ 0 ] = buildPlane.normal() * buildPlane.dist() + origin;
			pts[ 1 ] = pts[ 0 ] + vecs[ 0 ] * 256.0f;
			pts[ 2 ] = pts[ 0 ] + vecs[ 1 ] * 256.0f;
			//Sys_Printf( "not\n" );
		}
		/* print planepoints */
		fprintf( f, "\t\t( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ",
		         pts[ 0 ][ 0 ], pts[ 0 ][ 1 ], pts[ 0 ][ 2 ],
		         pts[ 1 ][ 0 ], pts[ 1 ][ 1 ], pts[ 1 ][ 2 ],
		         pts[ 2 ][ 0 ], pts[ 2 ][ 1 ], pts[ 2 ][ 2 ]
		       );

		if ( vert[0] != nullptr && vert[1] != nullptr && vert[2] != nullptr ) {
			const DoubleVector3 verts[3] = { DoubleVector3( vert[0]->xyz ) + DoubleVector3( origin ),
			                                 DoubleVector3( vert[1]->xyz ) + DoubleVector3( origin ),
			                                 DoubleVector3( vert[2]->xyz ) + DoubleVector3( origin ) };
			const Vector2 sts[3] = { vert[0]->st, vert[1]->st, vert[2]->st };

			if ( brushType == EBrushType::Bp || brushType == EBrushType::Valve220 ) {
				DoubleVector3 texX, texY;
				ComputeAxisBase( buildPlane.normal(), texX, texY );
				std::array<q3mapx::Point2, 3> xy, uv;
				for ( int i = 0; i < 3; ++i ) {
					xy[i] = { vector3_dot( verts[i], texX ), vector3_dot( verts[i], texY ) };
					uv[i] = { sts[i][0], sts[i][1] };
				}
				q3mapx::Affine2 matrix;
				if ( q3mapx::solveAffine( xy, uv, matrix ) ) {
					for ( int i = 0; i < 2; ++i ) buildSide.texMat[i] = Vector3( matrix[i][0], matrix[i][1], matrix[i][2] );
					++recovery.matchedFaces;
				}
				else {
					++recovery.degenerateUVs;
					++recovery.fallbackFaces;
				}

				/* print brush side */
				if( brushType == EBrushType::Bp ){
					/* ( 640 24 -224 ) ( 448 24 -224 ) ( 448 -232 -224 ) ( ( 0 0.03125 0 ) ( -0.03125 0 0.75 ) ) common/caulk 0 0 0 */
					fprintf( f, "( ( %.8f %.8f %.8f ) ( %.8f %.8f %.8f ) ) %s %d 0 0\n",
					         buildSide.texMat[0][0], buildSide.texMat[0][1], FRAC( buildSide.texMat[0][2] ),
					         buildSide.texMat[1][0], buildSide.texMat[1][1], FRAC( buildSide.texMat[1][2] ),
					         texture,
					         contentFlag
					       );
				}
				else if( brushType == EBrushType::Valve220 ){
					// brush_primit.cpp Valve220_from_BP()
					const double scale[2]{ 1.0 / ( vector2_length( buildSide.texMat[0].vec2() ) * buildSide.shaderInfo->shaderWidth ),
					                       1.0 / ( vector2_length( buildSide.texMat[1].vec2() ) * buildSide.shaderInfo->shaderHeight ) };
					const double shift[2]{ FRAC( buildSide.texMat[0][2] ) * buildSide.shaderInfo->shaderWidth,
					                       FRAC( buildSide.texMat[1][2] ) * buildSide.shaderInfo->shaderHeight };

					const DoubleVector3 basis_s = vector3_normalised( texX * buildSide.texMat[0][0] + texY * buildSide.texMat[0][1] );
					const DoubleVector3 basis_t = vector3_normalised( texX * buildSide.texMat[1][0] + texY * buildSide.texMat[1][1] );

					/* ( 640 24 -224 ) ( 448 24 -224 ) ( 448 -232 -224 ) common/caulk [ 1 0 0 0 ] [ 0 -1 0 48 ] 90 0.5 0.5 0 0 0 */
					fprintf( f, "%s [ %.8f %.8f %.8f %.8f ] [ %.8f %.8f %.8f %.8f ] 0 %.8f %.8f %d 0 0\n",
					         texture,
					         basis_s.x(), basis_s.y(), basis_s.z(), shift[0],
					         basis_t.x(), basis_t.y(), basis_t.z(), shift[1],
					         scale[0], scale[1],
					         contentFlag
					       );
				}
			}
			else if ( brushType == EBrushType::Quake ) {
				// invert QuakeTextureVecs
				DoubleVector3 texMat[2];
				float shift[2], scale[2];
				float rotate;
				const auto vecs = TextureAxisFromPlane( buildPlane );
				const int sv = vecs[0][0] ? 0 : vecs[0][1] ? 1 : 2;
				const int tv = vecs[1][0] ? 0 : vecs[1][1] ? 1 : 2;
				std::array<q3mapx::Point2, 3> xy, uv;
				for ( int i = 0; i < 3; ++i ) {
					xy[i] = { verts[i][sv], verts[i][tv] };
					uv[i] = { double( sts[i][0] ) * buildSide.shaderInfo->shaderWidth,
					          double( sts[i][1] ) * buildSide.shaderInfo->shaderHeight };
				}
				q3mapx::Affine2 matrix;
				if ( q3mapx::solveAffine( xy, uv, matrix ) ) {
					for ( int i = 0; i < 2; ++i ) texMat[i] = DoubleVector3( matrix[i][0], matrix[i][1], matrix[i][2] );
					++recovery.matchedFaces;
					const double crossAxis = std::fabs( matrix[0][0] * matrix[1][0] + matrix[0][1] * matrix[1][1] );
					if ( crossAxis > 1e-5 * std::hypot( matrix[0][0], matrix[0][1] ) * std::hypot( matrix[1][0], matrix[1][1] ) ) {
						++recovery.approximateQuakeFaces;
					}
				}
				else {
					texMat[0] = { 2.0, 0.0, 0.0 };
					texMat[1] = { 0.0, -2.0, 0.0 };
					++recovery.degenerateUVs;
					++recovery.fallbackFaces;
				}

				// now we must solve:
				//	// now we must invert:
				//	ang = degrees_to_radians( rotate );
				//	sinv = sin( ang );
				//	cosv = cos( ang );
				//	ns = cosv * vecs[0][sv];
				//	nt = sinv * vecs[0][sv];
				//	vecsrotscaled[0][sv] = ns / scale[0];
				//	vecsrotscaled[0][tv] = nt / scale[0];
				//	ns = -sinv * vecs[1][tv];
				//	nt =  cosv * vecs[1][tv];
				//	vecsrotscaled[1][sv] = ns / scale[1];
				//	vecsrotscaled[1][tv] = nt / scale[1];
#if 0
				scale[0] = 1.0 / vector2_length( texMat[0].vec2() );
				scale[1] = 1.0 / vector2_length( texMat[1].vec2() );
				rotate = radians_to_degrees( atan2( texMat[0][1] * vecs[0][sv] - texMat[1][0] * vecs[1][tv], texMat[0][0] * vecs[0][sv] + texMat[1][1] * vecs[1][tv] ) );
				shift[0] = buildSide.shaderInfo->shaderWidth * FRAC( texMat[0][2] / buildSide.shaderInfo->shaderWidth );
				shift[1] = buildSide.shaderInfo->shaderHeight * FRAC( texMat[1][2] / buildSide.shaderInfo->shaderHeight );
#else			// Texdef_fromTransform() from brush_primit.cpp, flawless unlike upper
				scale[0] = 1.0 / vector2_length( texMat[0].vec2() );
				scale[1] = 1.0 / vector2_length( texMat[1].vec2() );

				rotate = -radians_to_degrees( atan2( -texMat[0][1], texMat[0][0] ) );

				if ( rotate == -180.0f ) {
					rotate = 180.0f;
				}

				shift[0] = buildSide.shaderInfo->shaderWidth * FRAC( texMat[0][2] / buildSide.shaderInfo->shaderWidth );
				shift[1] = buildSide.shaderInfo->shaderHeight * FRAC( texMat[1][2] / buildSide.shaderInfo->shaderHeight );

				// If the 2d cross-product of the x and y axes is positive, one of the axes has a negative scale.
				if ( vector2_cross( Vector2( texMat[0][0], texMat[0][1] ), Vector2( texMat[1][0], texMat[1][1] ) ) > 0 ) {
					if ( rotate >= 180.0f ) {
						rotate -= 180.0f;
						scale[0] = -scale[0];
					}
					else
					{
						scale[1] = -scale[1];
					}
				}
#endif
				/* print brush side */
				/* ( 640 24 -224 ) ( 448 24 -224 ) ( 448 -232 -224 ) common/caulk 0 48 0 0.500000 0.500000 0 0 0 */
				fprintf( f, "%s %.8f %.8f %.8f %.8f %.8f %d 0 0\n",
				         texture,
				         shift[0], shift[1], rotate, scale[0], scale[1],
				         contentFlag
				       );
			}
		}
		else
		{
			++recovery.fallbackFaces;
			if ( g_decompile_wtf
			  && !striEqualPrefix( buildSide.shaderInfo->shader, "textures/common/" )
			  && !striEqualPrefix( buildSide.shaderInfo->shader, "textures/system/" )
			  &&        !strEqual( buildSide.shaderInfo->shader, "noshader" )
			  &&        !strEqual( buildSide.shaderInfo->shader, "default" ) ) {
				//fprintf( stderr, "no matching triangle for brushside using %s (hopefully nobody can see this side anyway)\n", buildSide.shaderInfo->shader );
				texture = "common/WTF";
			}

			if ( brushType == EBrushType::Quake ) {
				fprintf( f, "%s %.8f %.8f %.8f %.8f %.8f %d 0 0\n",
				         texture,
				         0.0f, 0.0f, 0.0f, 0.25f, 0.25f,
				         contentFlag
				       );
			}
			else if ( brushType == EBrushType::Valve220 ) {
				Vector3 texX, texY;
				ComputeAxisBase( buildPlane.normal(), texX, texY );
				fprintf( f, "%s [ %.8f %.8f %.8f %.8f ] [ %.8f %.8f %.8f %.8f ] 0 0.5 0.5 %d 0 0\n",
				         texture,
				         texX.x(), texX.y(), texX.z(), 0.f,
				         texY.x(), texY.y(), texY.z(), 0.f,
				         contentFlag
				       );
			}
			else if ( brushType == EBrushType::Bp ) {
				fprintf( f, "( ( %.8f %.8f %.8f ) ( %.8f %.8f %.8f ) ) %s %d 0 0\n",
				         1.0f / 16.0f, 0.0f, 0.0f,
				         0.0f, 1.0f / 16.0f, 0.0f,
				         texture,
				         contentFlag
				       );
			}
		}
	}

	/* end brush */
	if ( brushType == EBrushType::Bp ) {
		fprintf( f, "\t}\n" );
	}
	fprintf( f, "\t}\n\n" );
}
#undef FRAC

#if 0
/* iterate through the brush sides (ignore the first 6 bevel planes) */
for ( i = 0; i < brush->numSides; ++i )
{
	/* get side */
	side = &bspBrushSides[ brush->firstSide + i ];

	/* get shader */
	if ( side->shaderNum < 0 || side->shaderNum >= int( bspShaders.size() ) ) {
		continue;
	}
	shader = &bspShaders[ side->shaderNum ];
	if ( striEqual( shader->shader, "default" ) || striEqual( shader->shader, "noshader" ) ) {
		continue;
	}

	/* get texture name */
	if ( striEqualPrefix( shader->shader, "textures/" ) ) {
		texture = shader->shader + 9;
	}
	else{
		texture = shader->shader;
	}

	/* get plane */
	plane = &bspPlanes[ side->planeNum ];

	/* make plane points */
	{
		vec3_t vecs[ 2 ];


		MakeNormalVectors( plane->normal, vecs[ 0 ], vecs[ 1 ] );
		VectorMA( vec3_origin, plane->dist, plane->normal, pts[ 0 ] );
		VectorMA( pts[ 0 ], 256.0f, vecs[ 0 ], pts[ 1 ] );
		VectorMA( pts[ 0 ], 256.0f, vecs[ 1 ], pts[ 2 ] );
	}

	/* offset by origin */
	for ( j = 0; j < 3; ++j )
		VectorAdd( pts[ j ], origin, pts[ j ] );

	/* print brush side */
	/* ( 640 24 -224 ) ( 448 24 -224 ) ( 448 -232 -224 ) common/caulk 0 48 0 0.500000 0.500000 0 0 0 */
	fprintf( f, "\t\t( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) %s 0 0 0 0.5 0.5 0 0 0\n",
	         pts[ 0 ][ 0 ], pts[ 0 ][ 1 ], pts[ 0 ][ 2 ],
	         pts[ 1 ][ 0 ], pts[ 1 ][ 1 ], pts[ 1 ][ 2 ],
	         pts[ 2 ][ 0 ], pts[ 2 ][ 1 ], pts[ 2 ][ 2 ],
	         texture );
}
#endif



/*
   ConvertPatch()
   converts a bsp patch to a map patch

    {
        patchDef2
        {
            base_wall/concrete
            ( 9 3 0 0 0 )
            (
                ( ( 168 168 -192 0 2 ) ( 168 168 -64 0 1 ) ( 168 168 64 0 0 ) ... )
                ...
            )
        }
    }

 */

static void ConvertPatch( FILE *f, int num, const bspDrawSurface_t& ds, const Vector3& origin ){
	/* only patches */
	if ( ds.surfaceType != MST_PATCH ) {
		return;
	}

	/* get shader */
	if ( ds.shaderNum < 0 || ds.shaderNum >= int( bspShaders.size() ) ) {
		return;
	}

	/* get texture name */
	const char      *texture;
	if ( const bspShader_t& shader = bspShaders[ ds.shaderNum ];
		striEqualPrefix( shader.shader, "textures/" ) ) {
		texture = shader.shader + 9;
	}
	else{
		texture = shader.shader;
	}

	++recovery.patches;
	/* start patch */
	fprintf( f, "\t// patch %d\n", num );
	fprintf( f, "\t{\n" );
	fprintf( f, "\t\tpatchDef2\n" );
	fprintf( f, "\t\t{\n" );
	fprintf( f, "\t\t\t%s\n", texture );
	fprintf( f, "\t\t\t( %d %d 0 0 0 )\n", ds.patchWidth, ds.patchHeight );
	fprintf( f, "\t\t\t(\n" );

	/* iterate through the verts */
	for ( int x = 0; x < ds.patchWidth; ++x )
	{
		/* start row */
		fprintf( f, "\t\t\t\t(" );

		/* iterate through the row */
		for ( int y = 0; y < ds.patchHeight; ++y )
		{
			/* get vert */
			const bspDrawVert_t& dv = bspDrawVerts[ ds.firstVert + ( y * ds.patchWidth ) + x ];

			/* offset it */
			const Vector3 xyz = dv.xyz + origin;

			/* print vertex */
			fprintf( f, " ( %f %f %f %f %f )", xyz[ 0 ], xyz[ 1 ], xyz[ 2 ], dv.st[ 0 ], dv.st[ 1 ] );
		}

		/* end row */
		fprintf( f, " )\n" );
	}

	/* end patch */
	fprintf( f, "\t\t\t)\n" );
	fprintf( f, "\t\t}\n" );
	fprintf( f, "\t}\n\n" );
}



/*
   ConvertModel()
   exports a bsp model to a map file
 */

static void ConvertModel( FILE *f, const bspModel_t& model, const Vector3& origin, EBrushType brushType ){
	if ( origin != g_vector3_identity ) {
		ConvertOriginBrush( f, -1, origin, brushType );
	}

	/* go through each brush in the model */
	if( fast ){
		for ( int i = 0; i < model.numBSPBrushes; ++i )
			ConvertBrushFast( f, model.firstBSPBrush + i, origin, brushType );
	}
	else{
		ModelTriangles modelTriangles( model );
		for ( int i = 0; i < model.numBSPBrushes; ++i )
			ConvertBrush( f, model.firstBSPBrush + i, origin, brushType, modelTriangles );
	}

	/* go through each drawsurf in the model */
	for ( int i = 0; i < model.numBSPSurfaces; ++i )
	{
		const int num = i + model.firstBSPSurface;
		const bspDrawSurface_t& ds = bspDrawSurfaces[ num ];

		/* we only love patches */
		if ( ds.surfaceType == MST_PATCH ) {
			ConvertPatch( f, num, ds, origin );
		}
	}
}



/*
   ConvertEPairs()
   exports entity key/value pairs to a map file
 */

static void ConvertEPairs( FILE *f, const entity_t& e, bool skip_origin ){
	/* walk epairs */
	for ( const auto& ep : e.epairs )
	{
		/* ignore empty keys/values */
		if ( ep.key.empty() || ep.value.empty() ) {
			continue;
		}

		/* ignore model keys with * prefixed values */
		if ( striEqual( ep.key.c_str(), "model" ) && ep.value.c_str()[ 0 ] == '*' ) {
			continue;
		}

		/* ignore origin keys if skip_origin is set */
		if ( skip_origin && striEqual( ep.key.c_str(), "origin" ) ) {
			continue;
		}

		/* emit the epair */
		fprintf( f, "\t\"%s\" \"%s\"\n", ep.key.c_str(), ep.value.c_str() );
	}
}



/*
   ConvertBSPToMap()
   exports an quake map file from the bsp
 */

static int ConvertBSPToMap_Ext( char *bspName, EBrushType brushType ){
	recovery = {};
	detailBrushes.assign( bspBrushes.size(), false );
	for ( const auto& leaf : bspLeafs ) {
		if ( leaf.cluster <= CLUSTER_OPAQUE ) continue;
		for ( int i = 0; i < leaf.numBSPLeafBrushes; ++i ) {
			detailBrushes[bspLeafBrushes[leaf.firstBSPLeafBrush + i]] = true;
		}
	}
	/* setup brush conversion prerequisites */
	{
		/* convert bsp planes to map planes */
		mapplanes.resize( bspPlanes.size() );
		for ( size_t i = 0; i < bspPlanes.size(); ++i )
		{
			plane_t& plane = mapplanes[i];
			plane.plane = bspPlanes[ i ];
			plane.type = PlaneTypeForNormal( plane.normal() );
			plane.hash_chain = 0;
		}

		/* allocate a build brush */
		buildBrush.sides.reserve( MAX_BUILD_SIDES );
		buildBrush.entityNum = 0;
		buildBrush.original = &buildBrush;
	}

	if( g_game->load == LoadRBSPFile )
		UnSetLightStyles();

	/* note it */
	Sys_Printf( "--- Convert BSP to MAP ---\n" );

	/* create map filename from the bsp name */
	const auto name = decompileOptions.output ? StringStream( decompileOptions.output )
	    : StringStream( PathExtensionless( bspName ), "_converted.map" );
	Sys_Printf( "writing %s\n", name.c_str() );

	/* open it */
	FILE *f = SafeOpenWrite( name );

	/* print header */
	fprintf( f, "// Recovered by q3mapx " Q3MAPX_VERSION "; original source metadata may be unavailable.\n" );

	/* walk entity list */
	for ( std::size_t i = 0; i < entities.size(); ++i )
	{
		/* get entity */
		const entity_t& e = entities[ i ];

		/* start entity */
		fprintf( f, "// entity %zu\n", i );
		fprintf( f, "{\n" );

		/* get model num */
		int modelNum;
		if ( i == 0 ) {
			modelNum = 0;
		}
		else
		{
			const char *value = e.valueForKey( "model" );
			if ( value[ 0 ] == '*' ) {
				modelNum = atoi( value + 1 );
			}
			else{
				modelNum = -1;
			}
		}

		/* export keys */
		ConvertEPairs( f, e, modelNum >= 0 );
		fprintf( f, "\n" );

		/* only handle bsp models */
		if ( modelNum >= 0 ) {
			/* convert model */
			ConvertModel( f, bspModels[ modelNum ], e.vectorForKey( "origin" ), brushType );
		}

		/* end entity */
		fprintf( f, "}\n\n" );
	}

	/* close the file and return */
	const bool failed = ferror( f ) != 0;
	if ( fclose( f ) != 0 || failed ) Error( "Failed to finish writing %s", name.c_str() );
	Sys_Printf( "Recovered %zu brushes, %zu patches; %zu/%zu faces matched texture coordinates, %zu used fallback\n",
	    recovery.brushes, recovery.patches, recovery.matchedFaces, recovery.faces, recovery.fallbackFaces );
	if ( recovery.skippedBrushes || recovery.degenerateUVs || recovery.approximateQuakeFaces ) {
		Sys_Warning( "Recovery: %zu invalid brushes skipped, %zu degenerate UV transforms, %zu approximate Quake texture transforms\n",
		    recovery.skippedBrushes, recovery.degenerateUVs, recovery.approximateQuakeFaces );
	}
	if ( decompileOptions.report || decompileOptions.automaticReport ) {
		const auto report = decompileOptions.report ? StringStream( decompileOptions.report ) : StringStream( name, ".recovery.json" );
		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer( buffer );
		writer.StartObject();
		writer.Key( "schema_version" ); writer.Int( 1 );
		writer.Key( "input" ); writer.String( bspName );
		writer.Key( "output" ); writer.String( name.c_str() );
		writer.Key( "format" ); writer.String( brushType == EBrushType::Valve220 ? "map_220" : brushType == EBrushType::Bp ? "map_bp" : "map" );
		writer.Key( "fast" ); writer.Bool( fast );
		const auto count = [&]( const char* key, size_t value ){ writer.Key( key ); writer.Uint64( value ); };
		count( "entities", entities.size() );
		count( "brushes", recovery.brushes );
		count( "skipped_brushes", recovery.skippedBrushes );
		count( "patches", recovery.patches );
		count( "faces", recovery.faces );
		count( "matched_uv_faces", recovery.matchedFaces );
		count( "fallback_uv_faces", recovery.fallbackFaces );
		count( "degenerate_uv_transforms", recovery.degenerateUVs );
		count( "degenerate_triangles", recovery.degenerateTriangles );
		count( "approximate_quake_uv_faces", recovery.approximateQuakeFaces );
		count( "triangle_soup_surfaces", std::count_if( bspDrawSurfaces.begin(), bspDrawSurfaces.end(),
		    []( const auto& surface ){ return surface.surfaceType == MST_TRIANGLE_SOUP; } ) );
		writer.Key( "limitations" );
		writer.StartArray();
		writer.String( "Original editor groups, removed entities and source model instances are not stored in the BSP." );
		writer.String( "Triangle soup geometry is not exported separately; collision brushes may approximate it." );
		writer.String( "Fallback texture axes are used on faces without a usable rendered triangle, including hidden faces." );
		writer.EndArray();
		writer.EndObject();
		SaveFile( report, buffer.GetString(), int( buffer.GetSize() ) );
		Sys_Printf( "Recovery report: %s\n", report.c_str() );
	}

	/* return to sender */
	return 0;
}

int ConvertBSPToMap( char *bspName ){
	return ConvertBSPToMap_Ext( bspName, EBrushType::Quake );
}

int ConvertBSPToMap_BP( char *bspName ){
	return ConvertBSPToMap_Ext( bspName, EBrushType::Bp );
}

int ConvertBSPToMap_220( char *bspName ){
	return ConvertBSPToMap_Ext( bspName, EBrushType::Valve220 );
}
