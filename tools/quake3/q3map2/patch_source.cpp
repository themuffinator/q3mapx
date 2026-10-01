// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3map2.h"
#include "patch_source.h"
#include "bspfile_ibsp.h"
#include "bspfile_rbsp.h"
#include "authoring/patch_paint.h"
#include "authoring/surface.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <glib.h>

namespace q3mapx {
namespace {
constexpr char magic[16] = "Q3MAPX_PATCH_V1";
constexpr size_t footerSize = 4 + 64 + sizeof( magic );
constexpr size_t byteLimit = 32 * 1024 * 1024, controlLimit = 1'000'000, linkLimit = 2'000'000;
std::vector<PatchSource> sources;
std::string storedBinding;
size_t capturedControls = 0, capturedLinks = 0;

std::string checksum( const byte* data, size_t size ) {
	gchar* hash = g_compute_checksum_for_data( G_CHECKSUM_SHA256, data, size );
	if ( !hash ) throw std::runtime_error( "Cannot allocate patch source checksum" );
	std::string result( hash ); g_free( hash ); return result;
}
struct Bytes {
	std::vector<byte> data;
	void word( uint32_t n ) {
		if ( data.size() > byteLimit - 4 ) throw std::runtime_error( "Patch source archive exceeds 32 MiB; use -no-patch-source to omit authoring metadata" );
		for ( int i = 0; i < 4; ++i ) data.push_back( byte( n >> ( i * 8 ) ) );
	}
	void string( std::string_view text ) {
		word( text.size() );
		if ( text.size() > byteLimit - data.size() ) throw std::runtime_error( "Patch source archive exceeds 32 MiB" );
		for ( unsigned char c : text ) data.push_back( c );
	}
};
struct Reader {
	const std::vector<byte>& data;
	size_t pos = 0;
	uint32_t word() {
		if ( data.size() - pos < 4 ) throw std::runtime_error( "Truncated patch source archive" );
		uint32_t n = 0; for ( int i = 0; i < 4; ++i ) n |= uint32_t( data[pos++] ) << ( i * 8 ); return n;
	}
	std::string string( size_t limit ) {
		const size_t n = word();
		if ( n > limit || n > data.size() - pos ) throw std::runtime_error( "Invalid patch source string length" );
		std::string text( data.begin() + pos, data.begin() + pos + n ); pos += n;
		if ( text.find_first_of( "\"{}()" ) != std::string::npos
		  || std::any_of( text.begin(), text.end(), []( unsigned char c ) { return c <= 32 || c == 127; } )
		  || text.find( "//" ) != std::string::npos || text.find( "/*" ) != std::string::npos || text.find( "*/" ) != std::string::npos )
			throw std::runtime_error( "Invalid patch source string" );
		return text;
	}
};

std::string geometryBinding() {
	// Streaming, canonical words: LIGHT can change shader names, lighting slots,
	// entity text and RGB in lighting mode. It does not change these fields.
	std::vector<int> modes( bspDrawSurfaces.size(), 0 );
	for ( const auto& source : sources ) {
		if ( source.model < 0 || size_t( source.model ) >= bspModels.size() ) throw std::runtime_error( "Patch source model is outside BSP" );
		const auto& model = bspModels[source.model];
		for ( int surface : source.surfaces ) {
			if ( surface < model.firstBSPSurface || surface >= int64_t( model.firstBSPSurface ) + model.numBSPSurfaces )
				throw std::runtime_error( "Patch source surface belongs to another model" );
			if ( modes[surface] && modes[surface] != source.mode ) throw std::runtime_error( "Patch source surface has conflicting modes" );
			modes[surface] = source.mode;
		}
	}
	std::unique_ptr<GChecksum, decltype( &g_checksum_free )> hash( g_checksum_new( G_CHECKSUM_SHA256 ), g_checksum_free );
	if ( !hash ) throw std::runtime_error( "Cannot allocate patch source binding" );
	std::array<uint32_t, 1024> buffer;
	size_t used = 0;
	const auto flush = [&] { g_checksum_update( hash.get(), reinterpret_cast<const byte*>( buffer.data() ), used * 4 ); used = 0; };
	const auto word = [&]( uint32_t n ) { buffer[used++] = GUINT32_TO_LE( n ); if ( used == buffer.size() ) flush(); };
	word( 1 ); word( bspModels.size() );
	for ( const auto& model : bspModels ) { word( model.firstBSPSurface ); word( model.numBSPSurfaces ); word( model.firstBSPBrush ); word( model.numBSPBrushes ); }
	word( bspDrawSurfaces.size() );
	const int styles = g_game->load == LoadRBSPFile ? MAX_LIGHTMAPS : 1;
	word( styles );
	for ( size_t i = 0; i < bspDrawSurfaces.size(); ++i ) {
		const auto& ds = bspDrawSurfaces[i];
		word( modes[i] ); word( ds.surfaceType ); word( ds.firstVert ); word( ds.numVerts );
		word( ds.firstIndex ); word( ds.numIndexes ); word( ds.patchWidth ); word( ds.patchHeight );
		for ( int v = 0; v < ds.numVerts; ++v ) {
			const auto& vert = bspDrawVerts[ds.firstVert + v];
			for ( int a = 0; a < 3; ++a ) word( std::bit_cast<uint32_t>( vert.xyz[a] ) );
			for ( int a = 0; a < 2; ++a ) word( std::bit_cast<uint32_t>( vert.st[a] ) );
			if ( modes[i] ) for ( int s = 0; s < styles; ++s ) {
				word( vert.color[s][3] );
				if ( modes[i] == authoring::materialPaint ) for ( int c = 0; c < 3; ++c ) word( vert.color[s][c] );
			}
		}
	}
	word( bspDrawIndexes.size() ); for ( int index : bspDrawIndexes ) word( index );
	if ( used ) flush();
	return g_checksum_get_string( hash.get() );
}
}

void ResetPatchSources() { sources.clear(); storedBinding.clear(); capturedControls = capturedLinks = 0; }
const std::vector<PatchSource>& PatchSources() { return sources; }

int CapturePatchSource( const parseMesh_t& patch ) {
	if ( !retainPatchSources ) return -1;
	if ( sources.size() >= 10'000 || size_t( patch.mesh.numVerts() ) > controlLimit - capturedControls )
		Error( "Patch source archive limit exceeded; use -no-patch-source to omit authoring metadata" );
	capturedControls += patch.mesh.numVerts();
	const int id = sources.size();
	auto& s = sources.emplace_back();
	s.model = int( bspModels.size() ) - 1; s.entity = patch.entityNum; s.primitive = patch.brushNum;
	s.width = patch.mesh.width; s.height = patch.mesh.height; s.mode = patch.paintMode;
	s.subdivisions = patch.paintSubdivisions; s.sampleSize = patch.lightmapSampleSizeOverride;
	s.shader = patch.paintSourceShader;
	s.controls.assign( patch.mesh.begin(), patch.mesh.end() );
	return id;
}

void LinkPatchSourceSurface( const mapDrawSurface_t& ds ) {
	for ( int id : ds.patchSources ) {
		if ( id < 0 ) continue;
		if ( size_t( id ) >= sources.size() || ds.outputNum < 0 ) Error( "Invalid internal patch source association" );
		if ( ++capturedLinks > linkLimit ) Error( "Patch source association limit exceeded; use -no-patch-source" );
		sources[id].surfaces.push_back( ds.outputNum );
	}
}

void ValidatePatchSources() {
	if ( sources.empty() ) throw std::runtime_error( "No retained patch source archive in this BSP" );
	if ( storedBinding.empty() || storedBinding != geometryBinding() ) throw std::runtime_error( "Patch source archive does not match compiled geometry" );
}

std::vector<byte> PatchSourceTrailer() {
	if ( sources.empty() ) return {};
	const std::string binding = geometryBinding();
	if ( !storedBinding.empty() && storedBinding != binding ) {
		Sys_Warning( "Geometry changed: dropping stale patch source archive; rebuild from the original MAP for exact source recovery\n" );
		ResetPatchSources(); return {};
	}
	Bytes bytes;
	bytes.word( 1 ); bytes.string( g_game->arg ); bytes.string( binding ); bytes.word( sources.size() );
	for ( auto& s : sources ) {
		std::sort( s.surfaces.begin(), s.surfaces.end() );
		s.surfaces.erase( std::unique( s.surfaces.begin(), s.surfaces.end() ), s.surfaces.end() );
		for ( int n : { s.model, s.entity, s.primitive, s.width, s.height, s.mode, s.subdivisions, s.sampleSize } ) bytes.word( n );
		bytes.string( s.shader );
		for ( const auto& v : s.controls ) {
			for ( int a = 0; a < 3; ++a ) bytes.word( std::bit_cast<uint32_t>( v.xyz[a] ) );
			for ( int a = 0; a < 2; ++a ) bytes.word( std::bit_cast<uint32_t>( v.st[a] ) );
			uint32_t color = 0; for ( int c = 0; c < 4; ++c ) color |= uint32_t( v.color[0][c] ) << ( c * 8 ); bytes.word( color );
		}
		bytes.word( s.surfaces.size() ); for ( int surface : s.surfaces ) bytes.word( surface );
	}
	const auto hash = checksum( bytes.data.data(), bytes.data.size() );
	bytes.word( bytes.data.size() );
	bytes.data.insert( bytes.data.end(), hash.begin(), hash.end() );
	bytes.data.insert( bytes.data.end(), std::begin( magic ), std::end( magic ) );
	return std::move( bytes.data );
}

void ReadPatchSourceTrailer( const char* filename ) try {
	ResetPatchSources();
	std::ifstream file( std::filesystem::u8path( filename ), std::ios::binary | std::ios::ate );
	if ( !file ) throw std::runtime_error( "Cannot read BSP patch source trailer" );
	const auto fileSize = file.tellg();
	if ( fileSize < std::streamoff( footerSize ) ) return;
	file.seekg( fileSize - std::streamoff( sizeof( magic ) ) );
	char tail[sizeof( magic )];
	if ( !file.read( tail, sizeof( tail ) ) ) throw std::runtime_error( "Cannot read BSP trailer" );
	if ( std::memcmp( tail, magic, sizeof( magic ) ) ) return;
	if ( !g_game->write ) throw std::runtime_error( "Patch source archive requires a BSP-writing profile" );
	file.seekg( fileSize - std::streamoff( footerSize ) );
	std::vector<byte> footer( footerSize );
	if ( !file.read( reinterpret_cast<char*>( footer.data() ), footer.size() ) ) throw std::runtime_error( "Truncated patch source footer" );
	Reader footerReader{ footer };
	const size_t size = footerReader.word();
	if ( size > byteLimit || std::streamoff( size + footerSize + 152 ) > fileSize ) throw std::runtime_error( "Invalid patch source payload length" );
	const auto start = fileSize - std::streamoff( size + footerSize );
	// Every native lump must precede the archive; no field may masquerade as a
	// source record by aliasing engine-visible geometry or entity bytes.
	file.seekg( 8 );
	const size_t lumps = g_game->load == LoadIBSPFile && !strEqual( g_game->arg, "quakelive" ) ? 17 : 18;
	std::vector<byte> directory( lumps * 8 );
	if ( !file.read( reinterpret_cast<char*>( directory.data() ), directory.size() ) ) throw std::runtime_error( "Truncated BSP directory" );
	Reader native{ directory };
	for ( size_t i = 0; i < lumps; ++i ) {
		const uint64_t offset = native.word(), length = native.word();
		if ( offset + length > uint64_t( std::streamoff( start ) ) ) throw std::runtime_error( "Patch source archive overlaps a native BSP lump" );
	}
	file.seekg( start );
	std::vector<byte> payload( size );
	if ( !file.read( reinterpret_cast<char*>( payload.data() ), payload.size() ) ) throw std::runtime_error( "Truncated patch source payload" );
	if ( checksum( payload.data(), payload.size() ) != std::string( footer.begin() + 4, footer.begin() + 68 ) )
		throw std::runtime_error( "Patch source archive checksum mismatch" );
	Reader reader{ payload };
	if ( reader.word() != 1 ) throw std::runtime_error( "Unsupported patch source archive version" );
	if ( reader.string( 64 ) != g_game->arg ) throw std::runtime_error( "Patch source archive game profile mismatch" );
	storedBinding = reader.string( 64 );
	if ( storedBinding.size() != 64 || storedBinding.find_first_not_of( "0123456789abcdef" ) != std::string::npos )
		throw std::runtime_error( "Invalid patch source geometry digest" );
	const size_t count = reader.word();
	if ( count == 0 || count > 10'000 ) throw std::runtime_error( "Invalid patch source count" );
	for ( size_t i = 0; i < count; ++i ) {
		auto& s = sources.emplace_back();
		s.model = int( reader.word() ); s.entity = int( reader.word() ); s.primitive = int( reader.word() );
		s.width = int( reader.word() ); s.height = int( reader.word() ); s.mode = int( reader.word() );
		s.subdivisions = int( reader.word() ); s.sampleSize = int( reader.word() ); s.shader = reader.string( 63 );
		if ( s.model < 0 || size_t( s.model ) >= bspModels.size() || s.entity < 0 || s.primitive < 0
		  || !authoring::paintMeshFits( s.width, s.height, s.subdivisions ) || ( s.subdivisions & ( s.subdivisions - 1 ) )
		  || ( s.mode != authoring::alphaPaint && s.mode != authoring::materialPaint )
		  || s.sampleSize < 0 || s.sampleSize > authoring::maxSampleSize || s.shader.size() <= 9 || !striEqualPrefix( s.shader.c_str(), "textures/" ) )
			throw std::runtime_error( "Invalid patch source record" );
		if ( size_t( s.width * s.height ) > controlLimit - capturedControls ) throw std::runtime_error( "Patch source control limit exceeded" );
		capturedControls += s.width * s.height;
		for ( int v = 0; v < s.width * s.height; ++v ) {
			auto& vert = s.controls.emplace_back( c_bspDrawVert_t0 );
			for ( int a = 0; a < 3; ++a ) {
				vert.xyz[a] = std::bit_cast<float>( reader.word() );
				if ( !std::isfinite( vert.xyz[a] ) || std::abs( vert.xyz[a] ) > 1e7 ) throw std::runtime_error( "Invalid patch source position" );
			}
			for ( int a = 0; a < 2; ++a ) { vert.st[a] = std::bit_cast<float>( reader.word() ); if ( !std::isfinite( vert.st[a] ) ) throw std::runtime_error( "Invalid patch source UV" ); }
			const uint32_t color = reader.word(); Color4b rgba;
			for ( int c = 0; c < 4; ++c ) rgba[c] = byte( color >> ( c * 8 ) );
			if ( s.mode == authoring::alphaPaint && ( rgba[0] != 255 || rgba[1] != 255 || rgba[2] != 255 ) ) throw std::runtime_error( "Invalid patch source lighting RGB" );
			vert.color.fill( rgba );
		}
		const size_t links = reader.word();
		if ( links > linkLimit - capturedLinks ) throw std::runtime_error( "Patch source association limit exceeded" );
		capturedLinks += links;
		for ( size_t j = 0; j < links; ++j ) {
			const int surface = int( reader.word() );
			if ( surface < 0 || size_t( surface ) >= bspDrawSurfaces.size() || ( !s.surfaces.empty() && s.surfaces.back() >= surface ) ) throw std::runtime_error( "Invalid patch source surface list" );
			s.surfaces.push_back( surface );
		}
	}
	if ( reader.pos != payload.size() ) throw std::runtime_error( "Trailing patch source payload data" );
}
catch ( const std::exception& error ) { Error( "BSP patch source: %s", error.what() ); }
}
