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
#include "bspfile_native.h"
#include "bspfile_early.h"
#include "bspfile_rbsp.h"
#include "qspatial.h"
#include "decompile.h"
#include "bsp_evidence.h"
#include "recovery_groups.h"
#include "q3mapx/affine.h"
#include "q3mapx/uv_fit.h"
#include "q3mapx/atomic_file.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <map>

struct DecompileStats {
	size_t brushes = 0, skippedBrushes = 0, faces = 0, matchedFaces = 0;
	size_t fallbackFaces = 0, degenerateUVs = 0, degenerateTriangles = 0;
	size_t patches = 0, approximateQuakeFaces = 0, inferredMaterials = 0;
	size_t brushesWithDetailFlag = 0;
	size_t unrepresentableUVOutputs = 0;
};
static DecompileStats recovery;
static bool PreciseTextureOutput(){
	return decompileOptions.uvPolicy == DecompileOptions::UVPolicy::Consensus;
}

static bool FloatTextureParameter( double value ){
	return std::isfinite( value ) && std::abs( value ) <= std::numeric_limits<float>::max();
}
struct UVRecoveryRecord {
	int brush, plane;
	size_t triangles;
	const char* status;
	q3mapx::UVFitResult error;
	std::vector<int> surfaces;
	size_t omittedSurfaces = 0;
};
static constexpr size_t maxUVRecoveryRecords = 10'000;
static std::vector<UVRecoveryRecord> uvRecoveryRecords;
static std::map<std::string, size_t> uvRecoveryCounts;
static size_t omittedUVRecoveryRecords = 0;
static std::vector<bool> detailBrushes;
struct DetailDecision {
	bool baseline = false, applied = false;
	bool materialEvaluated = false;
	int materialFlags = 0;
	const char* reason = "legacy_fallback";
};
static std::vector<DetailDecision> detailDecisions;
static std::unique_ptr<q3mapx::BSPEvidence> detailEvidence;

// One policy for every MAP writer, including fast texture recovery. This is a
// leaf-reference heuristic; it does not prove the author's original choice.
static int LegacyBrushDetailFlag( int brushNum ){
	return detailBrushes[brushNum]
	    && !( bspShaders[bspBrushes[brushNum].shaderNum].contentFlags
	          & GetRequiredSurfaceParm<"structural">().contentFlags ) ? C_DETAIL : 0;
}
static int InferredBrushDetailFlag( int brushNum ){
	return detailDecisions.empty() ? LegacyBrushDetailFlag( brushNum ) : detailDecisions[brushNum].applied ? C_DETAIL : 0;
}



/*
   ConvertBrush()
   exports a map brush
 */

struct BspTriangleRef
{
	bspSurfaceType_t surfaceType;
	int surfaceIndex;
	TriRef tri;
	MinMax minmax; // X is on c_spatial_sort_direction

	BspTriangleRef( bspSurfaceType_t surfaceType, int surfaceIndex, const bspDrawVert_t& v0, const bspDrawVert_t& v1, const bspDrawVert_t& v2 )
	:	surfaceType( surfaceType ), surfaceIndex( surfaceIndex ),
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

struct UVFaceMatches {
	struct Triangle { TriRef verts; double weight; const char* material; int surface; };
	std::vector<Triangle> triangles;
	const char* material = nullptr;
	size_t total = 0;
	bool limited = false;
	void add( const BspTriangleRef& triangle, double area, const char* shader ) {
		++total;
		if ( limited ) return;
		if ( triangles.size() == q3mapx::maxUVFitSamples / 3 ) {
			triangles.clear(); limited = true; return;
		}
		triangles.push_back( { triangle.tri, area, shader, triangle.surfaceIndex } );
	}
};

static bool UVVertexLess( const bspDrawVert_t* a, const bspDrawVert_t* b ) {
	for ( size_t axis = 0; axis < 3; ++axis ) if ( a->xyz[axis] != b->xyz[axis] ) return a->xyz[axis] < b->xyz[axis];
	for ( size_t axis = 0; axis < 2; ++axis ) if ( a->st[axis] != b->st[axis] ) return a->st[axis] < b->st[axis];
	return false;
}

static TriRef CanonicalUVTriangle( const TriRef& triangle ) {
	size_t first = 0;
	for ( size_t i = 1; i < 3; ++i ) if ( UVVertexLess( triangle[i], triangle[first] ) ) first = i;
	return { triangle[first], triangle[(first+1)%3], triangle[(first+2)%3] }; // preserve winding
}

static bool FitTextureUV( const UVFaceMatches& matches, const TriRef& anchor, const Vector3& origin,
    const DoubleVector3& texX, const DoubleVector3& texY, int brush, int plane, q3mapx::Affine2& matrix,
    q3mapx::Point2 outputUnits = { 1, 1 } ) {
	if ( decompileOptions.uvPolicy == DecompileOptions::UVPolicy::Triangle ) return false;
	std::vector<q3mapx::UVSample> samples;
	std::vector<int> surfaces;
	samples.reserve( matches.triangles.size() * 3 );
	surfaces.reserve( matches.triangles.size() );
	for ( const auto& triangle : matches.triangles ) if ( triangle.material == matches.material ) {
		for ( const auto* vert : triangle.verts ) {
			const DoubleVector3 point = DoubleVector3( vert->xyz ) + DoubleVector3( origin );
			samples.push_back( { { vector3_dot( point, texX ), vector3_dot( point, texY ) },
			    { vert->st[0], vert->st[1] }, triangle.weight } );
		}
		surfaces.push_back( triangle.surface );
	}
	if ( !matches.limited && samples.size() <= 3 ) { ++uvRecoveryCounts["insufficient_triangles"]; return false; }
	q3mapx::Affine2 fitted;
	q3mapx::UVFitResult result;
	bool anchorConsistent = false;
	if ( !matches.limited ) {
		std::array<q3mapx::Point2, 3> xy, uv;
		for ( size_t i = 0; i < 3; ++i ) {
			const DoubleVector3 point = DoubleVector3( anchor[i]->xyz ) + DoubleVector3( origin );
			xy[i] = { vector3_dot( point, texX ), vector3_dot( point, texY ) };
			uv[i] = { anchor[i]->st[0], anchor[i]->st[1] };
		}
		q3mapx::Affine2 baseline;
		if ( q3mapx::solveAffine( xy, uv, baseline ) ) {
			for ( auto& row : baseline ) for ( double& value : row ) value = float( value );
			result = q3mapx::evaluateUVFit( samples, baseline );
			anchorConsistent = result.status == q3mapx::UVFitStatus::Consistent;
		}
	}
	// Preserve a transform that already explains all evidence. Besides avoiding
	// needless source churn, this common path needs no regression or sample sort.
	if ( !anchorConsistent ) result = matches.limited ? q3mapx::UVFitResult{ q3mapx::UVFitStatus::Limit }
	    : q3mapx::fitUVConsensus( samples, fitted );
	const char* status = anchorConsistent ? "triangle_consistent" : q3mapx::uvFitStatusName( result.status );
	bool accepted = !anchorConsistent && result.status == q3mapx::UVFitStatus::Consistent;
	if ( accepted ) {
		// Brush-primitive/Valve emission stores this intermediate in binary32.
		// Validate that representation too, not only a double-precision fit.
		q3mapx::Affine2 stored = fitted;
		for ( auto& row : stored ) for ( double& value : row ) value = float( value );
		const auto storageError = q3mapx::evaluateUVFit( samples, stored );
		if ( storageError.status != q3mapx::UVFitStatus::Consistent ) {
			accepted = false; status = "storage_precision"; result = storageError;
		}
		else {
			// Classic MAP conversion works in texture pixels. Its old affine
			// solver checks this range too; fitting repeats must not bypass it.
			for ( size_t axis = 0; axis < 2; ++axis ) for ( double value : fitted[axis] ) {
				const double scaled = value * outputUnits[axis];
				if ( !std::isfinite( scaled ) || std::abs( scaled ) > std::numeric_limits<float>::max() ) {
					accepted = false; status = "output_precision";
				}
			}
			if ( accepted ) matrix = fitted;
		}
	}
	++uvRecoveryCounts[status];
	if ( decompileOptions.report || decompileOptions.automaticReport ) {
		if ( uvRecoveryRecords.size() == maxUVRecoveryRecords ) ++omittedUVRecoveryRecords;
		else {
			std::sort( surfaces.begin(), surfaces.end() );
			surfaces.erase( std::unique( surfaces.begin(), surfaces.end() ), surfaces.end() );
			const size_t omittedSurfaces = surfaces.size() > 64 ? surfaces.size() - 64 : 0;
			if ( omittedSurfaces ) surfaces.resize( 64 );
			uvRecoveryRecords.push_back( { brush, plane, matches.limited ? matches.total : samples.size()/3,
			    status, result, std::move( surfaces ), omittedSurfaces } );
		}
	}
	return accepted;
}

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
	ModelTriangles( const bspModel_t& model, bool countDegenerate = true ){
		for ( int surface = 0; surface < model.numBSPSurfaces; ++surface )
		{
			const auto& s = bspDrawSurfaces[model.firstBSPSurface + surface];
			if ( s.surfaceType == MST_PLANAR || s.surfaceType == MST_TRIANGLE_SOUP ) {
				auto& vec = m_modelTriangles[bspShaders[s.shaderNum].shader].triangles;
				for ( int t = 0; t + 3 <= s.numIndexes; t += 3 )
				{
					BspTriangleRef triangle( s.surfaceType, model.firstBSPSurface + surface,
						bspDrawVerts[s.firstVert + bspDrawIndexes[s.firstIndex + t + 0]],
						bspDrawVerts[s.firstVert + bspDrawIndexes[s.firstIndex + t + 1]],
						bspDrawVerts[s.firstVert + bspDrawIndexes[s.firstIndex + t + 2]]
					);
					Plane3f plane;
					if ( !PlaneFromPoints( plane, triangle.tri[0]->xyz, triangle.tri[1]->xyz, triangle.tri[2]->xyz ) ) {
						if ( countDegenerate ) ++recovery.degenerateTriangles;
						continue;
					}
					vec.push_back( triangle );
				}
			}
		}

		for( auto& [ k, values ] : m_modelTriangles ) values.build();
	}
	TriRef GetBestSurfaceTriangleMatchForBrushside( side_t& buildSide, std::vector<int>* groupSurfaces = nullptr,
	    uint64_t* groupWork = nullptr, bool* ambiguous = nullptr, UVFaceMatches* uvMatches = nullptr ) const {
		const float nepsilon = groupSurfaces ? 0.0001f : normalEpsilon * 100;
		const float depsilon = groupSurfaces ? 0.01f : 2;
		double coveredArea = 0;
		winding_t polygon;
		float bestarea = 0;
		float thisarea;
		const plane_t& buildPlane = mapplanes[buildSide.planenum];
		int matches = 0;

		// first, start out with NULLs
		TriRef bestVert{ nullptr };
		const char* bestMaterial=nullptr;
		const bool inferMaterial=bspEarlyVersion==43 || bspEarlyVersion==44;

		// Older BSPs do not store brush-side material names. Search each material's
		// spatial index and retain the largest positive coplanar overlap.
		const auto known=m_modelTriangles.find(buildSide.shaderInfo->shader.c_str());
		const auto begin=inferMaterial ? m_modelTriangles.begin() : known;
		const auto end=inferMaterial || known==m_modelTriangles.end() ? m_modelTriangles.end() : std::next(known);
		for(auto triangles=begin;triangles!=end;++triangles){
			MinMax minmax;
			for( const Vector3& v : buildSide.winding )
				minmax.extend( Vector3( spatial_distance( v ), v.y(), v.z() ) );
			minmax.mins -= Vector3( 32, depsilon, depsilon ); // 32 helps to spot more triangles, when brush is noticeably smaller
			minmax.maxs += Vector3( 32, depsilon, depsilon ); // e.g. produced by original model autoclip

			triangles->second.query( minmax, [&]( const BspTriangleRef& triangle ) {
				if ( groupWork ) q3mapx::spendGroupWork( *groupWork, decompileOptions.groupWorkLimit, 1 + buildSide.winding.size() );
				const auto* tri = &triangle;
				if ( !minmax.test( tri->minmax ) ) {
					return;
				}
				const TriRef vert = uvMatches ? CanonicalUVTriangle( tri->tri ) : tri->tri;
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
					ChopWindingInPlace( polygon, plane, groupSurfaces ? 0.00001f : distanceEpsilon );
					if ( polygon.empty() ) {
						goto exwinding;
					}
				}
				thisarea = WindingArea( polygon );
				if ( thisarea > 0 ) {
					++matches;
					if ( uvMatches ) {
						Plane3 plane;
						PlaneFromPoints( plane, DoubleVector3( vert[0]->xyz ), DoubleVector3( vert[1]->xyz ), DoubleVector3( vert[2]->xyz ) );
						if ( vector3_equal_epsilon( plane.normal(), DoubleVector3( buildPlane.normal() ), 0.0001 )
						  && std::fabs( plane3_distance_to_point( buildPlane.plane, vert[0]->xyz ) ) <= 0.01
						  && std::fabs( plane3_distance_to_point( buildPlane.plane, vert[1]->xyz ) ) <= 0.01
						  && std::fabs( plane3_distance_to_point( buildPlane.plane, vert[2]->xyz ) ) <= 0.01 )
							uvMatches->add( *tri, thisarea, triangles->first.c_str() );
					}
				}
				if ( groupSurfaces && thisarea > 0.001f ) {
					if ( groupSurfaces->size() == 2'000'000 ) throw std::runtime_error( "Group surface association limit exceeded" );
					groupSurfaces->push_back( tri->surfaceIndex );
					coveredArea += thisarea;
				}
				if ( thisarea > bestarea || ( uvMatches && thisarea > 0 && thisarea == bestarea
				    && bestMaterial == triangles->first.c_str()
				    && std::lexicographical_compare( vert.begin(), vert.end(), bestVert.begin(), bestVert.end(), UVVertexLess ) ) ) {
					bestarea = thisarea;
					bestVert = vert;
					bestMaterial=triangles->first.c_str();
				}
		exwinding:
				;
			} );
		}
		if ( ambiguous && coveredArea > double( WindingArea( buildSide.winding ) ) * 1.001 + 0.001 ) *ambiguous = true;
		//if( !striEqualPrefix( buildSide.shaderInfo->shader, "textures/common/" ) )
		//	fprintf( stderr, "brushside with %s: %d matches (%f area)\n", buildSide.shaderInfo->shader, matches, bestarea );
		if(inferMaterial && bestMaterial) {
			buildSide.shaderInfo=&ShaderInfoForShader(bestMaterial);
			++recovery.inferredMaterials;
		}
		if ( uvMatches ) uvMatches->material = bestMaterial;
		return bestVert;
	}
};

struct GroupRecovery {
	uint64_t work = 0;
	const char* exportBlock = nullptr;
	std::vector<q3mapx::GroupBrushEvidence> brushes;
	q3mapx::RecoveryGroupPlan plan;
	std::vector<int> assigned;
	std::unique_ptr<ModelTriangles> triangles;
};
static std::unique_ptr<GroupRecovery> groupRecovery;

static bool GroupCompileKey( const char* key ){
	for ( const char* candidate : { "_castShadows", "_cs", "_receiveShadows", "_rs", "lightmapscale", "_lightmapscale", "_ls",
	    "_celshader", "_shadeangle", "_smoothnormals", "_sn", "_sa", "_smooth", "_lightmapsamplesize", "_samplesize", "_ss",
	    "_color", "_ambient", "ambient" } )
		if ( striEqual( key, candidate ) ) return true;
	return false;
}

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
	const int contentFlag = InferredBrushDetailFlag( bspBrushNum );
	recovery.brushesWithDetailFlag += contentFlag != 0;
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
			if ( buildPlane.type >= 3 ) {
				// A float tangent basis plus three decimal places changes oblique
				// normals/distances. Solve the dominant coordinate in double precision
				// near the face instead; the two free axes keep the points well spaced.
				size_t axis = 0;
				for ( size_t a = 1; a < 3; ++a ) if ( std::abs( buildPlane.normal()[a] ) > std::abs( buildPlane.normal()[axis] ) ) axis = a;
				const size_t b = ( axis+1 )%3, c = ( axis+2 )%3;
				DoubleVector3 precise[3];
				precise[0] = DoubleVector3( buildSide.winding.front() );
				precise[0][b] = std::round( precise[0][b] ); precise[0][c] = std::round( precise[0][c] );
				precise[1] = precise[2] = precise[0];
				precise[1][buildPlane.normal()[axis] > 0 ? c : b] += 256;
				precise[2][buildPlane.normal()[axis] > 0 ? b : c] += 256;
				for ( auto& point : precise ) {
					point[axis] = ( double( buildPlane.dist() ) - double( buildPlane.normal()[b] )*point[b]
					    - double( buildPlane.normal()[c] )*point[c] ) / double( buildPlane.normal()[axis] );
					point += DoubleVector3( origin );
				}
				fprintf( f, "\t\t( %.17g %.17g %.17g ) ( %.17g %.17g %.17g ) ( %.17g %.17g %.17g ) ",
				    precise[0][0], precise[0][1], precise[0][2], precise[1][0], precise[1][1], precise[1][2], precise[2][0], precise[2][1], precise[2][2] );
			}
			else fprintf( f, "\t\t( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ( %.3f %.3f %.3f ) ",
			         pts[ 0 ][ 0 ], pts[ 0 ][ 1 ], pts[ 0 ][ 2 ],
			         pts[ 1 ][ 0 ], pts[ 1 ][ 1 ], pts[ 1 ][ 2 ],
			         pts[ 2 ][ 0 ], pts[ 2 ][ 1 ], pts[ 2 ][ 2 ]
			       );
			if ( brushType == EBrushType::Quake ) {
				fprintf( f, "%s %.8f %.8f %.8f %.8f %.8f %d 0 0\n",
				         texture,
				         0.0f, 0.0f, 0.0f, 0.5f, 0.5f, contentFlag
				       );
			}
			else if ( brushType == EBrushType::Valve220 ) {
				Vector3 texX, texY;
				ComputeAxisBase( buildPlane.normal(), texX, texY );
				fprintf( f, "%s [ %.8f %.8f %.8f %.8f ] [ %.8f %.8f %.8f %.8f ] 0 0.5 0.5 %d 0 0\n",
				         texture,
				         texX.x(), texX.y(), texX.z(), 0.f,
				         texY.x(), texY.y(), texY.z(), 0.f, contentFlag
				       );
			}
			else if ( brushType == EBrushType::Bp ) {
				fprintf( f, "( ( %.8f %.8f %.8f ) ( %.8f %.8f %.8f ) ) %s %d 0 0\n",
				         1.0f / 32.0f, 0.0f, 0.0f,
				         0.0f, 1.0f / 32.0f, 0.0f,
				         texture, contentFlag
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
	const bool preciseUV = PreciseTextureOutput();

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

	const int contentFlag = InferredBrushDetailFlag( bspBrushNum );
	recovery.brushesWithDetailFlag += contentFlag != 0;

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
		UVFaceMatches uvMatches;
		const TriRef vert = modelTriangles.GetBestSurfaceTriangleMatchForBrushside( buildSide, nullptr, nullptr, nullptr,
		    decompileOptions.uvPolicy == DecompileOptions::UVPolicy::Consensus ? &uvMatches : nullptr );

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
				const bool matchedUV = FitTextureUV( uvMatches, vert, origin, texX, texY, bspBrushNum, buildSide.planenum, matrix )
				    || q3mapx::solveAffine( xy, uv, matrix );
				if ( matchedUV ) {
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
					fprintf( f, preciseUV ? "( ( %.9g %.9g %.9g ) ( %.9g %.9g %.9g ) ) %s %d 0 0\n"
					                     : "( ( %.8f %.8f %.8f ) ( %.8f %.8f %.8f ) ) %s %d 0 0\n",
					         buildSide.texMat[0][0], buildSide.texMat[0][1], preciseUV ? buildSide.texMat[0][2] : FRAC( buildSide.texMat[0][2] ),
					         buildSide.texMat[1][0], buildSide.texMat[1][1], preciseUV ? buildSide.texMat[1][2] : FRAC( buildSide.texMat[1][2] ),
					         texture,
					         contentFlag
					       );
				}
				else if( brushType == EBrushType::Valve220 ){
					// brush_primit.cpp Valve220_from_BP()
					double scale[2], shift[2];
					DoubleVector3 basis[2];
					const auto project = [&]{
						bool representable = true;
						for ( size_t axis = 0; axis < 2; ++axis ) {
							const auto& row = buildSide.texMat[axis];
							const int size = axis == 0 ? buildSide.shaderInfo->shaderWidth : buildSide.shaderInfo->shaderHeight;
							scale[axis] = 1.0 / ( vector2_length( row.vec2() ) * size );
							shift[axis] = ( preciseUV ? double( row[2] ) : FRAC( row[2] ) ) * size;
							basis[axis] = vector3_normalised( texX * row[0] + texY * row[1] );
							representable &= FloatTextureParameter( shift[axis] ) && FloatTextureParameter( scale[axis] )
							    && float( scale[axis] ) != 0;
							for ( size_t component = 0; component < 3; ++component )
								representable &= FloatTextureParameter( basis[axis][component] );
						}
						return representable;
					};
					if ( !project() && preciseUV ) {
						// A native repeat offset can fit binary32 while its pixel
						// representation cannot. Do not publish an unreadable MAP.
						++recovery.unrepresentableUVOutputs;
						if ( matchedUV ) { --recovery.matchedFaces; ++recovery.fallbackFaces; }
						Sys_FPrintf( SYS_WRN, "Brush %d plane %d: Valve texture parameters exceed MAP storage; using fallback\n", bspBrushNum, buildSide.planenum );
						buildSide.texMat[0] = { 1 / 32.0, 0, 0 };
						buildSide.texMat[1] = { 0, 1 / 32.0, 0 };
						project();
					}

					/* ( 640 24 -224 ) ( 448 24 -224 ) ( 448 -232 -224 ) common/caulk [ 1 0 0 0 ] [ 0 -1 0 48 ] 90 0.5 0.5 0 0 0 */
					fprintf( f, preciseUV ? "%s [ %.17g %.17g %.17g %.17g ] [ %.17g %.17g %.17g %.17g ] 0 %.17g %.17g %d 0 0\n"
					                     : "%s [ %.8f %.8f %.8f %.8f ] [ %.8f %.8f %.8f %.8f ] 0 %.8f %.8f %d 0 0\n",
					         texture,
					         basis[0].x(), basis[0].y(), basis[0].z(), shift[0],
					         basis[1].x(), basis[1].y(), basis[1].z(), shift[1],
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
				DoubleVector3 fitX( 0 ), fitY( 0 ); fitX[sv] = 1; fitY[tv] = 1;
				const bool fitted = FitTextureUV( uvMatches, vert, origin, fitX, fitY, bspBrushNum, buildSide.planenum, matrix,
				    { double( buildSide.shaderInfo->shaderWidth ), double( buildSide.shaderInfo->shaderHeight ) } );
				if ( fitted ) {
					for ( double& value : matrix[0] ) value *= buildSide.shaderInfo->shaderWidth;
					for ( double& value : matrix[1] ) value *= buildSide.shaderInfo->shaderHeight;
				}
				if ( fitted || q3mapx::solveAffine( xy, uv, matrix ) ) {
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

				shift[0] = preciseUV ? texMat[0][2] : buildSide.shaderInfo->shaderWidth * FRAC( texMat[0][2] / buildSide.shaderInfo->shaderWidth );
				shift[1] = preciseUV ? texMat[1][2] : buildSide.shaderInfo->shaderHeight * FRAC( texMat[1][2] / buildSide.shaderInfo->shaderHeight );

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
				fprintf( f, preciseUV ? "%s %.9g %.9g %.9g %.9g %.9g %d 0 0\n"
				                     : "%s %.8f %.8f %.8f %.8f %.8f %d 0 0\n",
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
			fprintf( f, PreciseTextureOutput() ? " ( %.9g %.9g %.9g %.9g %.9g )" : " ( %f %f %f %f %f )",
			    xyz[ 0 ], xyz[ 1 ], xyz[ 2 ], dv.st[ 0 ], dv.st[ 1 ] );
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

static bool OpaqueBrushForRebuild( int brushNum ){
	// Use exactly the side selection used by both MAP writers, including the
	// modelclip option. Native material inference and -wtf are excluded by the CLI.
	bspBrush_to_buildBrush( bspBrushes[brushNum] );
	const auto translucent = []( const side_t& side ){
		return side.shaderInfo && ( side.shaderInfo->compileFlags & C_TRANSLUCENT );
	};
	if ( std::none_of( buildBrush.sides.begin(), buildBrush.sides.end(), translucent ) )
		return true;
	// Redundant/bevel sides with no winding are not written to the MAP. Their
	// material must not determine its opacity when that MAP is loaded again.
	if ( !CreateBrushWindings( buildBrush ) ) return true;
	return std::none_of( buildBrush.sides.begin(), buildBrush.sides.end(), [&]( const side_t& side ){
		return !side.winding.empty() && translucent( side );
	} );
}

static void InferBrushDetailFromCells(){
	detailEvidence = std::make_unique<q3mapx::BSPEvidence>( q3mapx::analyzeBSPEvidence( 0, decompileOptions.detailWorkLimit ) );
	q3mapx::analyzeBSPBrushCells( *detailEvidence, decompileOptions.detailWorkLimit );
	detailDecisions.resize( bspBrushes.size() );
	for ( size_t b = 0; b < bspBrushes.size(); ++b ) {
		auto& decision = detailDecisions[b];
		decision.baseline = decision.applied = LegacyBrushDetailFlag( int( b ) ) != 0;
		if ( detailEvidence->brushes[b].model != 0 ) { decision.reason = "non_world_geometry_preserved"; continue; }
		const auto& cells = detailEvidence->brushCells[b];
		if ( !strEqual( cells.status, "analyzed" ) ) { decision.reason = "cell_analysis_unavailable"; continue; }
		bspBrush_to_buildBrush( bspBrushes[b] );
		if ( !CreateBrushWindings( buildBrush ) ) { decision.reason = "export_geometry_unavailable"; continue; }
		decision.materialEvaluated = true;
		for ( const auto& side : buildBrush.sides )
			if ( side.shaderInfo && !side.winding.empty() ) decision.materialFlags |= side.shaderInfo->compileFlags;
		if ( ( decision.materialFlags & C_STRUCTURAL )
		    || ( bspShaders[bspBrushes[b].shaderNum].contentFlags & GetRequiredSurfaceParm<"structural">().contentFlags ) ) {
			decision.applied = false; decision.reason = "explicit_structural_material";
		}
		else if ( decision.materialFlags & ( C_HINT | C_SKIP | C_AREAPORTAL | C_ANTIPORTAL | C_ORIGIN | C_SKY | C_LIQUID | C_FOG ) )
			decision.reason = "protected_material_preserved";
		else if ( decision.materialFlags & C_DETAIL ) { decision.applied = true; decision.reason = "explicit_detail_material"; }
		else if ( !( decision.materialFlags & C_SOLID ) || ( decision.materialFlags & C_TRANSLUCENT ) )
			decision.reason = "nonopaque_material_preserved";
		else if ( detailEvidence->visibility.present && detailEvidence->visibility.missingSelfBits ) decision.reason = "inconsistent_pvs_preserved";
		else if ( cells.open.available ) { decision.applied = true; decision.reason = "open_interior_detail_candidate"; }
		else decision.reason = "opaque_or_uncertain_interior_preserved";
	}
}

static void InferBrushGroups(){
	groupRecovery = std::make_unique<GroupRecovery>();
	auto& state = *groupRecovery;
	const auto evidence = q3mapx::analyzeBSPEvidence( 0, decompileOptions.groupWorkLimit );
	state.work = evidence.workUsed;
	const auto spend = [&]( uint64_t amount ){ q3mapx::spendGroupWork( state.work, decompileOptions.groupWorkLimit, amount ); };
	const auto& world = bspModels[0];
	if ( world.numBSPBrushes > 50'000 ) throw std::runtime_error( "Group inference exceeds 50000 world brushes" );
	for ( size_t i = 1; i < bspModels.size(); ++i ) {
		const auto& other = bspModels[i];
		if ( other.numBSPSurfaces && world.numBSPSurfaces
		    && other.firstBSPSurface < world.firstBSPSurface + world.numBSPSurfaces
		    && world.firstBSPSurface < other.firstBSPSurface + other.numBSPSurfaces )
			state.exportBlock = "ambiguous_surface_model_ownership";
	}
	uint64_t triangles = 0;
	for ( int i = 0; i < world.numBSPSurfaces; ++i ) {
		const auto& surface = bspDrawSurfaces[world.firstBSPSurface+i];
		triangles += surface.numIndexes / 3;
		if ( triangles > 2'000'000 ) throw std::runtime_error( "Group inference exceeds 2000000 expanded world triangles" );
	}
	spend( triangles );
	state.triangles = std::make_unique<ModelTriangles>( world, !fast );
	state.assigned.assign( bspBrushes.size(), -1 );
	if ( !entities[0].classname_is( "worldspawn" ) || entities[0].vectorForKey( "origin" ) != g_vector3_identity )
		state.exportBlock = "nonstandard_world_entity";
	if ( const char* value; entities[0].read_keyvalue( value, "_indexmap", "alphamap" ) ) state.exportBlock = "world_index_map";
	if ( entities[0].floatForKey( "_shadeangle", "_smoothnormals", "_sn", "_sa", "_smooth" ) > 0 )
		state.exportBlock = "world_smoothing_context";
	for ( size_t i = 1; i < entities.size(); ++i )
		if ( strEqual( entities[i].valueForKey( "model" ), "*0" ) ) state.exportBlock = "shared_world_entity_model";
	MinMax worldBounds;
	uint64_t windingPoints = 0, associations = 0;
	for ( int i = 0; i < world.numBSPBrushes; ++i ) {
		q3mapx::GroupBrushEvidence brush;
		brush.index = world.firstBSPBrush+i;
		const auto& raw = bspBrushes[brush.index];
		if ( raw.numSides > 256 ) throw std::runtime_error( "Group inference exceeds 256 sides on a world brush" );
		spend( 1 + uint64_t( raw.numSides ) * raw.numSides * raw.numSides );
		bspBrush_to_buildBrush( raw );
		if ( evidence.brushes[brush.index].model != 0 ) {
			brush.exclusion = "ambiguous_model_ownership"; state.exportBlock = "ambiguous_model_ownership";
		}
		if ( !CreateBrushWindings( buildBrush ) ) {
			brush.exclusion = "unavailable_export_geometry"; state.exportBlock = "incomplete_geometry_mapping";
		}
		else {
			worldBounds.extend( buildBrush.minmax );
			for ( size_t a = 0; a < 3; ++a ) {
				brush.mins[a] = buildBrush.minmax.mins[a]; brush.maxs[a] = buildBrush.minmax.maxs[a];
				if ( !std::isfinite( brush.mins[a] ) || !std::isfinite( brush.maxs[a] )
				    || std::abs( brush.mins[a] ) > 1e7 || std::abs( brush.maxs[a] ) > 1e7 )
					throw std::runtime_error( "Group inference world-brush coordinate limit exceeded" );
			}
			bool ambiguous = false;
			for ( auto& side : buildBrush.sides ) {
				windingPoints += side.winding.size();
				if ( side.winding.size() > 2048 || windingPoints > 2'000'000 ) throw std::runtime_error( "Group inference winding-point limit exceeded" );
				if ( side.winding.empty() || !side.shaderInfo ) continue;
				const auto& material = *side.shaderInfo;
				brush.opaque &= !( material.compileFlags & C_TRANSLUCENT );
				if ( material.compileFlags & ( C_HINT | C_SKIP | C_ORIGIN | C_AREAPORTAL | C_ANTIPORTAL | C_SKY | C_LIQUID | C_FOG ) )
					brush.exclusion = "protected_material";
				if ( material.indexed || material.legacyTerrain || material.nonplanar || material.shadeAngleDegrees > 0
				    || material.autosprite || material.furNumLayers || !material.surfaceModels.empty() || !material.foliage.empty()
				    || material.backShader || material.cloneShader || material.remapShader || material.deprecateShader
				    || std::ranges::any_of( material.colorMod, []( const auto& mod ){ return mod.type == EColorMod::Volume; } ) )
					brush.exclusion = "context_dependent_material";
				state.triangles->GetBestSurfaceTriangleMatchForBrushside( side, &brush.surfaces, &state.work, &ambiguous );
			}
			if ( ambiguous ) brush.exclusion = "ambiguous_overlapping_surface_support";
			std::sort( brush.surfaces.begin(), brush.surfaces.end() );
			brush.surfaces.erase( std::unique( brush.surfaces.begin(), brush.surfaces.end() ), brush.surfaces.end() );
			if ( brush.surfaces.empty() ) brush.exclusion = "no_render_surface_support";
		}
		associations += brush.surfaces.size();
		if ( associations > 2'000'000 ) throw std::runtime_error( "Group inference surface association limit exceeded" );
		state.brushes.push_back( std::move( brush ) );
	}
	for ( auto& brush : state.brushes ) {
		spend( 1 );
		if ( brush.exclusion ) continue;
		for ( size_t a = 0; a < 3; ++a )
			if ( brush.mins[a] <= worldBounds.mins[a]+0.01 || brush.maxs[a] >= worldBounds.maxs[a]-0.01 )
				brush.exclusion = "outer_world_boundary";
	}
	state.plan = q3mapx::planRecoveryGroups( state.brushes, state.work, decompileOptions.groupWorkLimit );
	if ( state.exportBlock ) {
		for ( auto& group : state.plan.groups ) if ( group.exported ) { group.exported = false; group.status = "world_context_blocks_export"; }
		state.plan.emissionOrder.clear();
	}
	for ( size_t group : state.plan.emissionOrder )
		for ( int member : state.plan.groups[group].members ) state.assigned[member] = int( group );
}

static void ConvertModel( FILE *f, const bspModel_t& model, const Vector3& origin, EBrushType brushType ){
	if ( origin != g_vector3_identity ) {
		ConvertOriginBrush( f, -1, origin, brushType );
	}

	std::vector<int> brushes;
	if ( decompileOptions.brushOrder == DecompileOptions::BrushOrder::Rebuild ) {
		brushes.reserve( model.numBSPBrushes );
		std::vector<int> translucent;
		for ( int i = 0; i < model.numBSPBrushes; ++i ) {
			const int brush = model.firstBSPBrush + i;
			( OpaqueBrushForRebuild( brush ) ? brushes : translucent ).push_back( brush );
		}
		// LoadMapFile prepends opaque brushes and appends translucent brushes.
		// Invert that insertion to retain the compiled order where recoverable.
		std::reverse( brushes.begin(), brushes.end() );
		brushes.insert( brushes.end(), translucent.begin(), translucent.end() );
	}
	const auto brushAt = [&]( int i ){
		return brushes.empty() ? model.firstBSPBrush + i : brushes[i];
	};
	const bool groupedWorld = groupRecovery && &model == &bspModels[0];
	const auto assigned = [&]( int index ){ return groupedWorld && groupRecovery->assigned[index] >= 0; };

	/* go through each brush in the model */
	if( fast ){
		for ( int i = 0; i < model.numBSPBrushes; ++i ) if ( !assigned( brushAt( i ) ) ) ConvertBrushFast( f, brushAt( i ), origin, brushType );
	}
	else{
		const auto local = groupedWorld ? nullptr : std::make_unique<ModelTriangles>( model );
		const auto& modelTriangles = groupedWorld ? *groupRecovery->triangles : *local;
		for ( int i = 0; i < model.numBSPBrushes; ++i ) if ( !assigned( brushAt( i ) ) ) ConvertBrush( f, brushAt( i ), origin, brushType, modelTriangles );
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

static void WriteGroupInferenceReport( rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer, rapidjson::StringBuffer& buffer ){
	const auto count = [&]( const char* key, uint64_t value ){ writer.Key( key ); writer.Uint64( value ); };
	writer.Key( "group_inference" ); writer.StartObject();
	writer.Key( "policy" ); writer.String( "surfaces" );
	writer.Key( "basis" ); writer.String( "strict_coplanar_brush_face_overlap_with_shared_bsp_draw_surfaces" );
	writer.Key( "author_grouping_proven" ); writer.Bool( false );
	writer.Key( "original_group_parameters_recovered" ); writer.Bool( false );
	writer.Key( "bsp_rebuild_validated" ); writer.Bool( false );
	writer.Key( "current_shader_assets_used" ); writer.Bool( true );
	writer.Key( "original_shader_assets_verified" ); writer.Bool( false );
	writer.Key( "compile_parameter_basis" ); writer.String( "copied_from_recovered_worldspawn" );
	writer.Key( "copied_compile_keys" ); writer.StartArray();
	for ( const auto& ep : entities[0].epairs ) if ( GroupCompileKey( ep.key.c_str() ) && !ep.value.empty() ) writer.String( ep.key.c_str() );
	writer.EndArray();
	count( "source_entities", entities.size() ); count( "exported_groups", groupRecovery->plan.emissionOrder.size() );
	count( "work_limit", decompileOptions.groupWorkLimit ); count( "work_used", groupRecovery->work );
	writer.Key( "export_block" ); if ( groupRecovery->exportBlock ) writer.String( groupRecovery->exportBlock ); else writer.Null();
	writer.Key( "emission_order" ); writer.StartArray(); for ( size_t i : groupRecovery->plan.emissionOrder ) writer.Uint64( i ); writer.EndArray();
	writer.Key( "brushes" ); writer.StartArray();
	for ( const auto& brush : groupRecovery->brushes ) {
		if ( buffer.GetSize() > 64 * 1024 * 1024 ) throw std::runtime_error( "Group inference report exceeds 64 MiB" );
		writer.StartObject(); count( "brush_index", brush.index );
		writer.Key( "opaque" ); writer.Bool( brush.opaque );
		writer.Key( "exclusion" ); if ( brush.exclusion ) writer.String( brush.exclusion ); else writer.Null();
		writer.Key( "surface_indices" ); writer.StartArray(); for ( int surface : brush.surfaces ) writer.Int( surface ); writer.EndArray();
		writer.EndObject();
	}
	writer.EndArray(); writer.Key( "groups" ); writer.StartArray();
	for ( size_t i = 0; i < groupRecovery->plan.groups.size(); ++i ) {
		if ( buffer.GetSize() > 64 * 1024 * 1024 ) throw std::runtime_error( "Group inference report exceeds 64 MiB" );
		const auto& group = groupRecovery->plan.groups[i];
		writer.StartObject(); count( "group_index", i );
		const std::string groupName = "q3mapx_inferred_group_" + std::to_string( i+1 );
		writer.Key( "name" ); writer.String( groupName.c_str() );
		writer.Key( "status" ); writer.String( group.status ); writer.Key( "exported" ); writer.Bool( group.exported );
		writer.Key( "brush_indices" ); writer.StartArray(); for ( int member : group.members ) writer.Int( member ); writer.EndArray();
		writer.Key( "surface_indices" ); writer.StartArray(); for ( int surface : group.surfaces ) writer.Int( surface ); writer.EndArray();
		writer.Key( "mins" ); writer.StartArray(); for ( double value : group.mins ) writer.Double( value ); writer.EndArray();
		writer.Key( "maxs" ); writer.StartArray(); for ( double value : group.maxs ) writer.Double( value ); writer.EndArray();
		count( "members_with_detail_flag", std::count_if( group.members.begin(), group.members.end(), []( int b ){ return InferredBrushDetailFlag( b ) != 0; } ) );
		writer.EndObject();
	}
	writer.EndArray(); writer.EndObject();
}

static int ConvertBSPToMap_Ext( char *bspName, EBrushType brushType ) try {
	recovery = {};
	uvRecoveryRecords.clear(); uvRecoveryCounts.clear(); omittedUVRecoveryRecords = 0;
	detailDecisions.clear(); detailEvidence.reset();
	groupRecovery.reset();
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
	if ( decompileOptions.detailPolicy == DecompileOptions::DetailPolicy::Cells ) InferBrushDetailFromCells();
	if ( decompileOptions.groupPolicy == DecompileOptions::GroupPolicy::Surfaces ) InferBrushGroups();

	if( g_game->load == LoadRBSPFile )
		UnSetLightStyles();

	/* note it */
	Sys_Printf( "--- Convert BSP to MAP ---\n" );

	/* create map filename from the bsp name */
	const auto name = decompileOptions.output ? StringStream( decompileOptions.output )
	    : StringStream( PathExtensionless( bspName ), "_converted.map" );
	Sys_Printf( "writing %s\n", name.c_str() );

	// Complete both outputs before publishing either. Publish the report first
	// so only the smaller companion needs a rollback copy if MAP replacement fails.
	const bool wantReport = decompileOptions.report || decompileOptions.automaticReport;
	const auto report = decompileOptions.report ? StringStream( decompileOptions.report ) : StringStream( name, ".recovery.json" );
	q3mapx::OutputFiles outputs;
	FILE* reportFile = wantReport ? outputs.open( report.c_str() ) : nullptr;
	FILE* f = outputs.open( name.c_str() );

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

	if ( groupRecovery ) {
		size_t emitted = 0;
		for ( size_t groupIndex : groupRecovery->plan.emissionOrder ) {
			const auto& group = groupRecovery->plan.groups[groupIndex];
			fprintf( f, "// entity %zu\n// Inferred assembly; original author grouping and compile properties are unproven.\n{\n", entities.size()+emitted++ );
			fprintf( f, "\t\"classname\" \"func_group\"\n\t\"name\" \"q3mapx_inferred_group_%zu\"\n", groupIndex+1 );
			for ( const auto& ep : entities[0].epairs ) if ( GroupCompileKey( ep.key.c_str() ) && !ep.value.empty() )
				fprintf( f, "\t\"%s\" \"%s\"\n", ep.key.c_str(), ep.value.c_str() );
			for ( int brush : group.members ) {
				if ( fast ) ConvertBrushFast( f, brush, g_vector3_identity, brushType );
				else ConvertBrush( f, brush, g_vector3_identity, brushType, *groupRecovery->triangles );
			}
			fprintf( f, "}\n\n" );
		}
	}

	if ( wantReport ) {
		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer( buffer );
		writer.StartObject();
		writer.Key( "schema_version" ); writer.Int( 1 );
		writer.Key( "input" ); writer.String( bspName );
		writer.Key( "output" ); writer.String( name.c_str() );
		writer.Key( "format" ); writer.String( brushType == EBrushType::Valve220 ? "map_220" : brushType == EBrushType::Bp ? "map_bp" : "map" );
		writer.Key( "fast" ); writer.Bool( fast );
		writer.Key( "brush_order" ); writer.StartObject();
		const bool rebuildOrder = decompileOptions.brushOrder == DecompileOptions::BrushOrder::Rebuild;
		writer.Key( "policy" ); writer.String( rebuildOrder ? "rebuild" : "bsp" );
		writer.Key( "basis" ); writer.String( groupRecovery ? "q3mapx_map_loader_group_collapse_and_side_shader_opacity" : rebuildOrder ? "q3mapx_map_loader_side_shader_opacity" : "bsp_brush_record_order" );
		writer.Key( "author_order_proven" ); writer.Bool( false );
		writer.Key( "rebuild_equivalence_proven" ); writer.Bool( false );
		writer.EndObject();
		writer.Key( "game" ); writer.String( g_game->arg );
		writer.Key( "native_write_supported" ); writer.Bool( g_game->write != nullptr );
		writer.Key( "native_losses" ); writer.StartArray();
		for(const auto& loss:bspRecoveryLosses) {
			writer.StartObject(); writer.Key("feature"); writer.String(loss.feature);
			writer.Key("bytes"); writer.Uint64(loss.bytes);
			writer.Key("reason"); writer.String(loss.reason); writer.EndObject();
		}
		writer.EndArray();
		if(!g_game->write) {
			writer.Key("native_shaders"); writer.StartArray();
			for(size_t i=0;i<bspShaders.size();++i) {
				const auto& shader=bspShaders[i];
				writer.StartObject(); writer.Key("index"); writer.Uint64(i);
				writer.Key("name"); writer.String(shader.shader);
				writer.Key("contents"); writer.Uint(uint32_t(shader.contentFlags));
				writer.Key("surface_flags"); writer.Uint(uint32_t(shader.surfaceFlags));
				if(i<bspNativeShaderSubdivisions.size()) { writer.Key("subdivisions"); writer.Int(bspNativeShaderSubdivisions[i]); }
				if(i<bspNativeFenceMasks.size()) { writer.Key("fence_mask"); writer.String(bspNativeFenceMasks[i].c_str()); }
				writer.EndObject();
			}
			writer.EndArray();
			writer.Key("native_surface_subdivisions"); writer.StartArray();
			for(float value:bspNativeSurfaceSubdivisions) writer.Double(value);
			writer.EndArray();
			if(!bspNativeFenceMasks.empty()) {
				writer.Key("native_side_equations"); writer.StartArray();
				for(const auto& eq:bspNativeSideEquations) { writer.StartArray(); for(float v:eq) writer.Double(v); writer.EndArray(); }
				writer.EndArray();
				writer.Key("native_side_equation_indices"); writer.StartArray();
				for(int id:bspNativeSideEquationIndices) writer.Int(id);
				writer.EndArray();
				writer.Key("native_static_models"); writer.StartArray();
				for(const auto& model:bspNativeStaticModels) {
					writer.StartObject(); writer.Key("model"); writer.String(model.model.c_str());
					writer.Key("origin"); writer.StartArray(); for(float v:model.origin) writer.Double(v); writer.EndArray();
					writer.Key("angles"); writer.StartArray(); for(float v:model.angles) writer.Double(v); writer.EndArray();
					writer.Key("scale"); writer.Double(model.scale); writer.EndObject();
				}
				writer.EndArray();
				writer.Key("native_terrain_triangles"); writer.Uint64(bspNativeTerrainTriangles);
				writer.Key("native_terrain_removed_triangles"); writer.Uint64(bspNativeTerrain.size()*128-bspNativeTerrainTriangles);
				writer.Key("native_terrain"); writer.StartArray();
				for(const auto& terrain:bspNativeTerrain) {
					writer.StartObject(); writer.Key("origin"); writer.StartArray(); writer.Int(terrain.x*64); writer.Int(terrain.y*64); writer.Int(terrain.baseHeight); writer.EndArray();
					writer.Key("shader_index"); writer.Int(terrain.shader); writer.Key("flags"); writer.Int(terrain.flags);
					writer.Key("texture_corners"); writer.StartArray(); for(float v:terrain.corners) writer.Double(v); writer.EndArray();
					writer.Key("height_steps"); writer.StartArray(); for(auto v:terrain.heights) writer.Uint(v); writer.EndArray();
					writer.Key("variance_flags"); writer.StartArray(); for(auto v:terrain.variance) writer.Uint(v); writer.EndArray();
					writer.Key("lightmap"); writer.Int(terrain.lightmap); writer.Key("lightmap_scale"); writer.Int(terrain.lightmapScale);
					writer.Key("lightmap_st"); writer.StartArray(); writer.Int(terrain.lightmapS); writer.Int(terrain.lightmapT); writer.EndArray(); writer.EndObject();
				}
				writer.EndArray();
			}
		}
		const auto count = [&]( const char* key, size_t value ){ writer.Key( key ); writer.Uint64( value ); };
		count( "entities", entities.size() + ( groupRecovery ? groupRecovery->plan.emissionOrder.size() : 0 ) );
		count( "brushes", recovery.brushes );
		writer.Key( "detail_classification" ); writer.StartObject();
		writer.Key( "method" ); writer.String( detailEvidence ? "convex_interior_witnesses_with_material_protection" : "nonopaque_leaf_reference_heuristic" );
		writer.Key( "structural_override_scope" ); writer.String( detailEvidence ? "brush_shader_contents_and_current_exported_side_materials" : "brush_shader_contents" );
		writer.Key( "author_classification_proven" ); writer.Bool( false );
		count( "exported_brushes_with_detail_flag", recovery.brushesWithDetailFlag );
		count( "exported_brushes_without_detail_flag", recovery.brushes - recovery.brushesWithDetailFlag );
		writer.EndObject();
		if ( detailEvidence ) {
			writer.Key( "detail_inference" ); writer.StartObject();
			writer.Key( "policy" ); writer.String( "cells" );
			writer.Key( "author_classification_proven" ); writer.Bool( false );
			writer.Key( "current_shader_assets_used" ); writer.Bool( true );
			writer.Key( "original_shader_assets_verified" ); writer.Bool( false );
			writer.Key( "bsp_rebuild_validated" ); writer.Bool( false );
			count( "work_limit", decompileOptions.detailWorkLimit ); count( "work_used", detailEvidence->workUsed );
			writer.Key( "stored_pvs_present" ); writer.Bool( detailEvidence->visibility.present );
			count( "pvs_missing_self_bits", detailEvidence->visibility.missingSelfBits );
			writer.Key( "brushes" ); writer.StartArray();
			for ( size_t b = 0; b < detailDecisions.size(); ++b ) {
				if ( buffer.GetSize() > 64 * 1024 * 1024 ) throw std::runtime_error( "Detail inference report exceeds 64 MiB" );
				const auto& decision = detailDecisions[b]; const auto& cells = detailEvidence->brushCells[b];
				writer.StartObject(); count( "brush_index", b );
				writer.Key( "baseline_detail" ); writer.Bool( decision.baseline );
				writer.Key( "applied_detail" ); writer.Bool( decision.applied );
				writer.Key( "reason" ); writer.String( decision.reason );
				writer.Key( "current_material_compile_flags" ); if ( decision.materialEvaluated ) writer.Uint( uint32_t( decision.materialFlags ) ); else writer.Null();
				writer.Key( "cell_status" ); writer.String( cells.status );
				count( "leaf_fragments", cells.leafFragments ); count( "uncertain_fragments", cells.uncertainFragments );
				writer.Key( "interior_clusters" ); writer.StartArray(); for ( int cluster : cells.interiorClusters ) writer.Int( cluster ); writer.EndArray();
				writer.Key( "invisible_pvs_pairs" ); if ( detailEvidence->visibility.present ) writer.Uint64( cells.invisiblePVSPairs ); else writer.Null();
				writer.Key( "open_witness" );
				if ( cells.open.available ) {
					writer.StartObject(); count( "leaf", cells.open.leaf ); writer.Key( "cluster" ); writer.Int( cells.open.cluster );
					writer.Key( "point" ); writer.StartArray(); for ( double v : cells.open.point ) writer.Double( v ); writer.EndArray();
					writer.Key( "clearance" ); writer.Double( cells.open.clearance ); writer.EndObject();
				}
				else writer.Null();
				writer.EndObject();
			}
			writer.EndArray(); writer.EndObject();
		}
		if ( groupRecovery ) WriteGroupInferenceReport( writer, buffer );
		count( "skipped_brushes", recovery.skippedBrushes );
		count( "patches", recovery.patches );
		count( "faces", recovery.faces );
		count( "matched_uv_faces", recovery.matchedFaces );
		count( "fallback_uv_faces", recovery.fallbackFaces );
		count( "degenerate_uv_transforms", recovery.degenerateUVs );
		count( "degenerate_triangles", recovery.degenerateTriangles );
		count( "normalized_unused_lightmap_uv_pairs", bspNormalizedUnusedLightmapPairs );
		count( "normalized_unused_flare_fogs", bspNormalizedUnusedFlareFogs );
		count( "normalized_unused_native_equations", bspNormalizedUnusedNativeEquations );
		count( "inferred_material_faces", recovery.inferredMaterials );
		writer.Key( "uv_output" ); writer.StartObject();
		writer.Key( "policy" ); writer.String( PreciseTextureOutput() ? "preserve_offsets_and_precision" : "legacy_wrapped_decimal" );
		writer.Key( "preserves_integer_offsets" ); writer.Bool( PreciseTextureOutput() && !fast );
		count( "unrepresentable_valve_faces", recovery.unrepresentableUVOutputs );
		writer.EndObject();
		writer.Key( "uv_recovery" ); writer.StartObject();
		writer.Key( "policy" ); writer.String( decompileOptions.uvPolicy == DecompileOptions::UVPolicy::Consensus ? "consensus" : "triangle" );
		writer.Key( "enabled" ); writer.Bool( !fast && decompileOptions.uvPolicy == DecompileOptions::UVPolicy::Consensus );
		writer.Key( "author_mapping_proven" ); writer.Bool( false );
		count( "triangle_limit_per_face", q3mapx::maxUVFitSamples / 3 );
		count( "record_limit", maxUVRecoveryRecords ); count( "omitted_records", omittedUVRecoveryRecords );
		writer.Key( "counts" ); writer.StartObject();
		for ( const auto& [status, value] : uvRecoveryCounts ) count( status.c_str(), value );
		writer.EndObject(); writer.Key( "faces" ); writer.StartArray();
		for ( const auto& record : uvRecoveryRecords ) {
			writer.StartObject(); writer.Key( "brush" ); writer.Int( record.brush );
			writer.Key( "plane" ); writer.Int( record.plane ); count( "triangles", record.triangles );
			writer.Key( "status" ); writer.String( record.status );
			const bool measured = record.error.status == q3mapx::UVFitStatus::Consistent || record.error.status == q3mapx::UVFitStatus::Conflict;
			writer.Key( "rms_error" ); if ( measured ) writer.Double( record.error.rmsError ); else writer.Null();
			writer.Key( "max_error" ); if ( measured ) writer.Double( record.error.maxError ); else writer.Null();
			writer.Key( "max_tolerance_ratio" ); if ( measured ) writer.Double( record.error.maxToleranceRatio ); else writer.Null();
			writer.Key( "surfaces" ); writer.StartArray(); for ( int surface : record.surfaces ) writer.Int( surface ); writer.EndArray();
			count( "omitted_surfaces", record.omittedSurfaces );
			writer.EndObject();
			if ( buffer.GetSize() > 64 * 1024 * 1024 ) throw std::runtime_error( "UV recovery report exceeds 64 MiB" );
		}
		writer.EndArray(); writer.EndObject();
		if(bspEarlyVersion) {
			writer.Key("native_models"); writer.StartArray();
			for(const auto& model:bspEarlyModels) {
				writer.StartObject(); writer.Key("origin"); writer.StartArray(); for(float v:model.origin) writer.Double(v); writer.EndArray();
				writer.Key("head_node"); writer.Int(model.headNode);
				writer.Key("declared_first_surface"); writer.Int(model.declaredFirstSurface);
				writer.Key("declared_surface_count"); writer.Int(model.declaredSurfaceCount); writer.EndObject();
			}
			writer.EndArray();
			const auto values=[&](const char* key,const auto& data,bool flags) {
				writer.Key(key); writer.StartArray();
				for(int v:data) { if(flags) writer.Uint(uint32_t(v)); else writer.Int(v); }
				writer.EndArray();
			};
			values("native_brush_contents",bspEarlyBrushContents,true);
			values("native_side_flags",bspEarlySideFlags,true);
			values("native_surface_source_indices",bspEarlySurfaceSources,false);
			values("native_brush_source_indices",bspEarlyBrushSources,false);
		}
		count( "approximate_quake_uv_faces", recovery.approximateQuakeFaces );
		count( "triangle_soup_surfaces", std::count_if( bspDrawSurfaces.begin(), bspDrawSurfaces.end(),
		    []( const auto& surface ){ return surface.surfaceType == MST_TRIANGLE_SOUP; } ) );
		writer.Key( "limitations" );
		writer.StartArray();
		writer.String( "Original editor groups are unavailable; removed entities and some source model instances may not be stored in the BSP." );
		writer.String( "Baked lightmaps and lightgrid data are not reconstructed as source lights by MAP export." );
		if(!g_game->write) writer.String("Native shader flags and subdivisions are retained in this report; standard MAP syntax does not reproduce all native compiler semantics. Native BSP writing is unavailable.");
		if(!bspNativeFenceMasks.empty()) writer.String("Native terrain is retained in this report and OBJ/ASE export, not as MAP brushes or Bezier patches. Static-model placements are retained here; their external model meshes are not imported.");
		if(bspEarlyVersion) writer.String("Early BSP model origins/head nodes are retained here. Fog visible sides depend on native shader semantics and are not reconstructed. The native shader dialect is only partially supported.");
		if(bspEarlyVersion==43 || bspEarlyVersion==44) writer.String("This format has no brush-side material names. Visible face names and UVs are inferred from rendered triangles; unmatched faces use common/caulk and fallback UVs. Raw brush contents and side flags remain in source order in this report.");
		writer.String( "Triangle soup geometry is not exported separately; collision brushes may approximate it." );
		writer.String( "Fallback texture axes are used on faces without a usable rendered triangle, including hidden faces." );
		writer.String( "Consensus output preserves whole texture offsets and uses round-trip decimal precision for stored parameters. Native compiler texture biases and source reconstruction loss cannot be undone; rebuilding with different shader or compiler semantics can still change UVs. Triangle compatibility mode retains legacy offset wrapping and decimal rounding. Constant axes and unsupported output representations retain fallback mappings." );
		writer.String( "UV consensus uses bounded overlap-weighted samples and a capped binary32 error allowance. Conflicting, ill-conditioned or limited evidence retains the largest-triangle mapping; charts are not averaged across a detected seam. Independent integer UV biases can also cause conflicts. Report errors describe fitting, not a guarantee of exact serialized/rebuilt UVs or original author intent." );
		if ( detailEvidence ) writer.String( "Cell detail inference proposes opaque world brushes with witnessed open-cell interiors as detail under current material semantics. Protected materials and ambiguous/opaque interiors retain the baseline; original author flags and rebuilt VIS equivalence are unproven." );
		if ( groupRecovery ) writer.String( "Shared render surfaces propose authoring assemblies independently of detail flags. Generated func_groups copy recovered worldspawn compile parameters and preserve compiled opacity order; original groups/names/parameters and rebuilt rendering equivalence are unproven. Protected, ambiguous and order-incompatible proposals remain flat. Patches and brush entities are not regrouped." );
		if ( rebuildOrder ) writer.String( "Rebuild brush order depends on the current shader assets and q3mapx loader semantics. Discarded source flags, plane/side ordering and other compiler differences can still change partitions or visibility." );
		writer.EndArray();
		writer.EndObject();
		if ( ( detailEvidence || groupRecovery ) && buffer.GetSize() > 64 * 1024 * 1024 ) throw std::runtime_error( "Recovery inference report exceeds 64 MiB" );
		if ( std::fwrite( buffer.GetString(), 1, buffer.GetSize(), reportFile ) != buffer.GetSize() )
			throw std::runtime_error( "Cannot write recovery report " + std::string( report.c_str() ) );
	}
	outputs.commit();
	Sys_Printf( "Recovered %zu brushes, %zu patches; %zu/%zu faces matched texture coordinates, %zu used fallback\n",
	    recovery.brushes, recovery.patches, recovery.matchedFaces, recovery.faces, recovery.fallbackFaces );
	if ( recovery.skippedBrushes || recovery.degenerateUVs || recovery.approximateQuakeFaces ) {
		Sys_Warning( "Recovery: %zu invalid brushes skipped, %zu degenerate UV transforms, %zu approximate Quake texture transforms\n",
		    recovery.skippedBrushes, recovery.degenerateUVs, recovery.approximateQuakeFaces );
	}
	if ( wantReport ) Sys_Printf( "Recovery report: %s\n", report.c_str() );
	if ( !uvRecoveryCounts.empty() ) {
		Sys_Printf( "UV consensus: %zu fitted faces, %zu conflicting mappings, %zu candidate limits\n",
		    uvRecoveryCounts["consistent"], uvRecoveryCounts["conflicting_mappings"], uvRecoveryCounts["sample_limit"] );
	}

	/* return to sender */
	return 0;
}
catch ( const std::exception& error ) { Error( "MAP recovery: %s", error.what() ); }

int ConvertBSPToMap( char *bspName ){
	return ConvertBSPToMap_Ext( bspName, EBrushType::Quake );
}

int ConvertBSPToMap_BP( char *bspName ){
	return ConvertBSPToMap_Ext( bspName, EBrushType::Bp );
}

int ConvertBSPToMap_220( char *bspName ){
	return ConvertBSPToMap_Ext( bspName, EBrushType::Valve220 );
}
