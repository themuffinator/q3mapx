// SPDX-License-Identifier: GPL-3.0-or-later
// q3mapx: validate untrusted BSP data before legacy compiler traversal.
#include "bspfile_abstract.h"

#include <cmath>
#include <cstring>
#include <limits>

bspHeader_t ReadBSPHeader( const MemBuffer& file, int lumpCount ){
	const size_t headerSize = 8 + size_t( lumpCount ) * sizeof( bspLump_t );
	if ( lumpCount < 1 || lumpCount > 100 || file.size() < headerSize ) {
		Error( "Invalid BSP: truncated header (need %zu bytes, have %zu)", headerSize, file.size() );
	}
	bspHeader_t header{};
	std::memcpy( &header, file.data(), headerSize );
	header.version = LittleLong( header.version );
	for ( int i = 0; i < lumpCount; ++i ) {
		auto& lump = header.lumps[i];
		lump.offset = LittleLong( lump.offset );
		lump.length = LittleLong( lump.length );
		if ( lump.offset < 0 || lump.length < 0
		  || size_t( lump.offset ) > file.size()
		  || size_t( lump.length ) > file.size() - size_t( lump.offset )
		  || ( lump.length != 0 && size_t( lump.offset ) < headerSize ) ) {
			Error( "Invalid BSP: lump %d range (%d, %d) is outside the file", i, lump.offset, lump.length );
		}
	}
	return header;
}

namespace {
void range( int first, int count, size_t size, const char* kind, size_t index ){
	if ( first < 0 || count < 0 || size_t( first ) > size || size_t( count ) > size - size_t( first ) ) {
		Error( "Invalid BSP: %s %zu has an out-of-range span (%d, %d; limit %zu)", kind, index, first, count, size );
	}
}

void index( int value, size_t size, const char* kind, size_t item ){
	if ( value < 0 || size_t( value ) >= size ) {
		Error( "Invalid BSP: %s %zu references %d (limit %zu)", kind, item, value, size );
	}
}

void string( const char* text, size_t size, const char* kind, size_t item ){
	if ( std::memchr( text, '\0', size ) == nullptr ) {
		Error( "Invalid BSP: %s %zu has an unterminated string", kind, item );
	}
}

template<typename Vector>
void finite( const Vector& value, size_t components, const char* kind, size_t item ){
	for ( size_t axis = 0; axis < components; ++axis ) {
		if ( !std::isfinite( value[axis] ) ) {
			Error( "Invalid BSP: %s %zu contains a non-finite coordinate", kind, item );
		}
	}
}

void nodeGraph(){
	// Iterative traversal also checks unreachable nodes. Never recurse into untrusted data.
	std::vector<byte> state( bspNodes.size(), 0 );
	std::vector<std::pair<size_t, unsigned>> stack;
	for ( size_t root = 0; root < bspNodes.size(); ++root ) {
		if ( state[root] == 2 ) continue;
		stack.emplace_back( root, 0 );
		state[root] = 1;
		while ( !stack.empty() ) {
			auto& [node, next] = stack.back();
			if ( next == 2 ) {
				state[node] = 2;
				stack.pop_back();
				continue;
			}
			const int child = bspNodes[node].children[next++];
			if ( child < 0 ) continue;
			if ( state[child] == 1 ) Error( "Invalid BSP: cycle in node graph at node %d", child );
			if ( state[child] == 2 ) continue;
			if ( stack.size() >= 1024 ) Error( "Invalid BSP: node depth exceeds safety limit of 1024" );
			state[child] = 1;
			stack.emplace_back( size_t( child ), 0 );
		}
	}
}
} // namespace

void ValidateBSPStrings(){
	for ( size_t i = 0; i < bspShaders.size(); ++i ) string( bspShaders[i].shader, MAX_QPATH, "shader", i );
	for ( size_t i = 0; i < bspFogs.size(); ++i ) string( bspFogs[i].shader, MAX_QPATH, "fog", i );
	for ( size_t i = 0; i < bspAds.size(); ++i ) string( bspAds[i].model, MAX_QPATH, "advertisement", i );
}

void ValidateBSPData( bool partial ){
	ValidateBSPStrings();
	for ( size_t i = 0; i < bspDrawSurfaces.size(); ++i ) {
		index( bspDrawSurfaces[i].shaderNum, bspShaders.size(), "surface shader", i );
	}
	if ( partial ) return;
	if ( bspModels.empty() ) Error( "Invalid BSP: missing world model" );
	for ( size_t i = 0; i < bspModels.size(); ++i ) {
		const auto& model = bspModels[i];
		range( model.firstBSPBrush, model.numBSPBrushes, bspBrushes.size(), "model brushes", i );
		range( model.firstBSPSurface, model.numBSPSurfaces, bspDrawSurfaces.size(), "model surfaces", i );
		finite( model.minmax.mins, 3, "model bounds", i );
		finite( model.minmax.maxs, 3, "model bounds", i );
	}
	for ( size_t i = 0; i < bspPlanes.size(); ++i ) {
		finite( bspPlanes[i].normal(), 3, "plane normal", i );
		if ( !std::isfinite( bspPlanes[i].dist() ) || vector3_length_squared( bspPlanes[i].normal() ) == 0 ) {
			Error( "Invalid BSP: plane %zu is degenerate", i );
		}
	}
	for ( size_t i = 0; i < bspBrushes.size(); ++i ) {
		const auto& brush = bspBrushes[i];
		range( brush.firstSide, brush.numSides, bspBrushSides.size(), "brush sides", i );
		index( brush.shaderNum, bspShaders.size(), "brush shader", i );
	}
	for ( size_t i = 0; i < bspBrushSides.size(); ++i ) {
		const auto& side = bspBrushSides[i];
		index( side.planeNum, bspPlanes.size(), "brush side plane", i );
		index( side.planeNum ^ 1, bspPlanes.size(), "brush side opposite plane", i );
		index( side.shaderNum, bspShaders.size(), "brush side shader", i );
		if ( side.surfaceNum != -1 ) index( side.surfaceNum, bspDrawSurfaces.size(), "brush side surface", i );
	}
	for ( size_t i = 0; i < bspDrawSurfaces.size(); ++i ) {
		const auto& surface = bspDrawSurfaces[i];
		range( surface.firstVert, surface.numVerts, bspDrawVerts.size(), "surface vertices", i );
		range( surface.firstIndex, surface.numIndexes, bspDrawIndexes.size(), "surface indices", i );
		if ( surface.fogNum != -1 ) index( surface.fogNum, bspFogs.size(), "surface fog", i );
		if ( surface.surfaceType < MST_BAD || surface.surfaceType > MST_FOLIAGE ) {
			Error( "Invalid BSP: surface %zu has unknown type %d", i, int( surface.surfaceType ) );
		}
		if ( surface.numIndexes % 3 != 0 ) Error( "Invalid BSP: surface %zu has incomplete triangles", i );
		for ( int j = 0; j < surface.numIndexes; ++j ) {
			index( bspDrawIndexes[size_t( surface.firstIndex ) + j], size_t( surface.numVerts ), "triangle vertex", i );
		}
		if ( surface.surfaceType == MST_PATCH ) {
			if ( surface.patchWidth < 3 || surface.patchHeight < 3
			  || surface.patchWidth > MAX_PATCH_SIZE || surface.patchHeight > MAX_PATCH_SIZE
			  || surface.patchWidth % 2 == 0 || surface.patchHeight % 2 == 0
			  || size_t( surface.patchWidth ) * size_t( surface.patchHeight ) != size_t( surface.numVerts ) ) {
				Error( "Invalid BSP: patch %zu has invalid control-point dimensions", i );
			}
		}
	}
	for ( size_t i = 0; i < bspDrawVerts.size(); ++i ) {
		const auto& vertex = bspDrawVerts[i];
		finite( vertex.xyz, 3, "vertex", i );
		finite( vertex.normal, 3, "vertex normal", i );
		finite( vertex.st, 2, "texture coordinate", i );
		for ( const auto& uv : vertex.lightmap ) finite( uv, 2, "lightmap coordinate", i );
	}
	for ( size_t i = 0; i < bspLeafs.size(); ++i ) {
		const auto& leaf = bspLeafs[i];
		range( leaf.firstBSPLeafBrush, leaf.numBSPLeafBrushes, bspLeafBrushes.size(), "leaf brushes", i );
		range( leaf.firstBSPLeafSurface, leaf.numBSPLeafSurfaces, bspLeafSurfaces.size(), "leaf surfaces", i );
		if ( leaf.cluster < -1 ) Error( "Invalid BSP: leaf %zu has invalid cluster", i );
	}
	for ( size_t i = 0; i < bspLeafBrushes.size(); ++i ) index( bspLeafBrushes[i], bspBrushes.size(), "leaf brush", i );
	for ( size_t i = 0; i < bspLeafSurfaces.size(); ++i ) index( bspLeafSurfaces[i], bspDrawSurfaces.size(), "leaf surface", i );
	for ( size_t i = 0; i < bspNodes.size(); ++i ) {
		index( bspNodes[i].planeNum, bspPlanes.size(), "node plane", i );
		for ( int child : bspNodes[i].children ) {
			if ( child >= 0 ) index( child, bspNodes.size(), "node child", i );
			else if ( uint64_t( -int64_t( child ) - 1 ) >= bspLeafs.size() ) {
				Error( "Invalid BSP: node %zu references invalid leaf %d", i, child );
			}
		}
	}
	nodeGraph();
	for ( size_t i = 0; i < bspFogs.size(); ++i ) {
		const auto& fog = bspFogs[i];
		if ( fog.brushNum == -1 ) continue; // global fog (Raven)
		index( fog.brushNum, bspBrushes.size(), "fog brush", i );
		if ( fog.visibleSide != -1 ) index( fog.visibleSide, bspBrushes[fog.brushNum].numSides, "fog side", i );
	}
	if ( !bspVisBytes.empty() ) {
		if ( bspVisBytes.size() < 8 ) Error( "Invalid BSP: truncated visibility header" );
		int counts[2];
		std::memcpy( counts, bspVisBytes.data(), sizeof( counts ) );
		if ( counts[0] < 0 || counts[1] < 0 || uint64_t( counts[1] ) < ( uint64_t( counts[0] ) + 7 ) / 8
		  || uint64_t( counts[0] ) * uint64_t( counts[1] ) > bspVisBytes.size() - 8 ) {
			Error( "Invalid BSP: visibility dimensions exceed lump size" );
		}
		for ( size_t i = 0; i < bspLeafs.size(); ++i ) {
			if ( bspLeafs[i].cluster >= counts[0] ) Error( "Invalid BSP: leaf %zu cluster exceeds visibility table", i );
		}
	}
	if ( bspEntData.empty() ) Error( "Invalid BSP: missing entity data" );
}
