// SPDX-License-Identifier: GPL-3.0-or-later
// q3mapx: validate untrusted BSP data before legacy compiler traversal.
#include "bspfile_abstract.h"
#include "q3mapx/index_validation.h"
#include "q3mapx/bsp_tree.h"

#include <cmath>
#include <cstring>
#include <limits>

size_t bspNormalizedUnusedLightmapPairs = 0;
size_t bspNormalizedUnusedFlareFogs = 0;

bspHeader_t ReadBSPHeader( const MemBuffer& file, int lumpCount, size_t directoryOffset ){
	if ( lumpCount < 1 || lumpCount > 100 || (directoryOffset != 8 && directoryOffset != 12) )
		Error( "Invalid BSP: unsupported directory layout" );
	const size_t headerSize = directoryOffset + size_t( lumpCount ) * sizeof( bspLump_t );
	if ( file.size() < headerSize ) {
		Error( "Invalid BSP: truncated header (need %zu bytes, have %zu)", headerSize, file.size() );
	}
	bspHeader_t header{};
	std::memcpy( &header, file.data(), 8 );
	std::memcpy( header.lumps, static_cast<const byte*>(file.data()) + directoryOffset,
	    size_t(lumpCount) * sizeof(bspLump_t) );
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

} // namespace

void ValidateBSPNodeGraph() try { q3mapx::bspNodeGraphDepth(bspNodes); }
catch ( const std::exception& error ) { Error("Invalid BSP: %s",error.what()); }

void ValidateBSPStrings(){
	for ( size_t i = 0; i < bspShaders.size(); ++i ) string( bspShaders[i].shader, MAX_QPATH, "shader", i );
	for ( size_t i = 0; i < bspFogs.size(); ++i ) string( bspFogs[i].shader, MAX_QPATH, "fog", i );
	for ( size_t i = 0; i < bspAds.size(); ++i ) string( bspAds[i].model, MAX_QPATH, "advertisement", i );
}

void ValidateBSPData( bool partial ){
	bspNormalizedUnusedLightmapPairs = 0;
	bspNormalizedUnusedFlareFogs = 0;
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
	q3mapx::IndexRangeValidator triangleIndices( bspDrawIndexes );
	for ( size_t i = 0; i < bspDrawSurfaces.size(); ++i ) {
		auto& surface = bspDrawSurfaces[i];
		range( surface.firstVert, surface.numVerts, bspDrawVerts.size(), "surface vertices", i );
		range( surface.firstIndex, surface.numIndexes, bspDrawIndexes.size(), "surface indices", i );
		// Raven's retail yavin_temple has a zero-geometry flare with fog 0
		// despite an empty fog lump. No fog exists to preserve for that flare.
		if ( surface.surfaceType == MST_FLARE && surface.numVerts == 0 && surface.numIndexes == 0
		  && surface.fogNum == 0 && bspFogs.empty() ) {
			surface.fogNum = -1;
			++bspNormalizedUnusedFlareFogs;
		}
		if ( surface.fogNum != -1 ) index( surface.fogNum, bspFogs.size(), "surface fog", i );
		if ( surface.surfaceType < MST_BAD || surface.surfaceType > MST_FOLIAGE ) {
			Error( "Invalid BSP: surface %zu has unknown type %d", i, int( surface.surfaceType ) );
		}
		if ( surface.numIndexes % 3 != 0 ) Error( "Invalid BSP: surface %zu has incomplete triangles", i );
		if ( const auto bad = triangleIndices.firstInvalid( size_t(surface.firstIndex), size_t(surface.numIndexes), surface.numVerts ) )
			index( bspDrawIndexes[*bad], size_t( surface.numVerts ), "triangle vertex", i );
		if ( surface.surfaceType == MST_PATCH ) {
			if ( surface.patchWidth < 3 || surface.patchHeight < 3
			  || surface.patchWidth > MAX_PATCH_SIZE || surface.patchHeight > MAX_PATCH_SIZE
			  || surface.patchWidth % 2 == 0 || surface.patchHeight % 2 == 0
			  || size_t( surface.patchWidth ) * size_t( surface.patchHeight ) != size_t( surface.numVerts ) ) {
				Error( "Invalid BSP: patch %zu has invalid control-point dimensions", i );
			}
		}
	}
	std::array<std::vector<size_t>, MAX_LIGHTMAPS> unusedCandidates;
	for ( size_t i = 0; i < bspDrawVerts.size(); ++i ) {
		const auto& vertex = bspDrawVerts[i];
		finite( vertex.xyz, 3, "vertex", i );
		finite( vertex.normal, 3, "vertex normal", i );
		finite( vertex.st, 2, "texture coordinate", i );
		for ( size_t slot = 0; slot < MAX_LIGHTMAPS; ++slot ) {
			const auto& uv = vertex.lightmap[slot];
			if ( !std::isfinite( uv[0] ) || !std::isfinite( uv[1] ) ) unusedCandidates[slot].push_back( i );
		}
	}
	// Retail Raven patches can contain NaNs in unused lighting slots, including
	// slot zero when the surface uses vertex lighting (lightmap number -3).
	// Check every referencing surface before normalizing them. A vertex shared
	// with an active slot must still fail, including under -force. Sorted sparse
	// candidates avoid walking each surface's entire (possibly shared) span.
	for ( size_t slot = 0; slot < MAX_LIGHTMAPS; ++slot ) {
		const auto& candidates = unusedCandidates[slot];
		if ( candidates.empty() ) continue;
		for ( const auto& surface : bspDrawSurfaces ) {
			if ( surface.lightmapStyles[slot] >= LS_UNUSED || surface.lightmapNum[slot] < 0 ) continue;
			const auto found = std::lower_bound( candidates.begin(), candidates.end(), size_t( surface.firstVert ) );
			if ( found != candidates.end() && *found < size_t( surface.firstVert ) + size_t( surface.numVerts ) )
				Error( "Invalid BSP: active lightmap coordinate %zu in slot %zu is non-finite", *found, slot );
		}
		for ( const size_t vertex : candidates ) bspDrawVerts[vertex].lightmap[slot] = Vector2( 0 );
		bspNormalizedUnusedLightmapPairs += candidates.size();
	}
	if ( bspNormalizedUnusedLightmapPairs )
		Sys_Warning( "Normalized %zu non-finite UV pairs in unused lightmap slots\n", bspNormalizedUnusedLightmapPairs );
	if ( bspNormalizedUnusedFlareFogs )
		Sys_Warning( "Normalized %zu zero-geometry flare fog references with no fog lump\n", bspNormalizedUnusedFlareFogs );
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
	ValidateBSPNodeGraph();
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
