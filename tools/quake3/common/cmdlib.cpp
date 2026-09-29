/*
   Copyright (C) 1999-2006 Id Software, Inc. and contributors.
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
 */

// cmdlib.c
// TTimo 09/30/2000
// from an initial copy of common/cmdlib.c
// stripped out the Sys_Printf Sys_Printf stuff

// SPoG 05/27/2001
// merging alpha branch into trunk
// replaced qprintf with Sys_Printf

#include "cmdlib.h"
#include "inout.h"
#include "qstringops.h"
#include "qpathops.h"
#include "stream/stringstream.h"
#include "stream/textstream.h"
#include <cerrno>
#include <filesystem>
#include <limits>
#include "q3mapx/atomic_file.h"

#ifdef WIN32
#include <direct.h>
#include <windows.h>
#endif

#if defined ( __linux__ ) || defined ( __APPLE__ )
#include <unistd.h>
#endif


void_ptr safe_malloc( size_t size ){
	void *p = malloc( size );
	if ( !p ) {
		Error( "safe_malloc failed on allocation of %zu bytes", size );
	}
	return p;
}

void_ptr safe_calloc( size_t size ){
	void *p = calloc( 1, size );
	if ( !p ) {
		Error( "safe_calloc failed on allocation of %zu bytes", size );
	}
	return p;
}



char *ExpandArg( const char *path ){
	static char full[1024];
	std::error_code error;
	const auto absolute = std::filesystem::absolute( path, error ).string();
	if ( error ) Error( "Cannot resolve path: %s", error.message().c_str() );
	if ( absolute.size() >= sizeof(full) ) Error( "Path exceeds %zu bytes", sizeof(full) - 1 );
	std::memcpy( full, absolute.c_str(), absolute.size() + 1 );
	return full;
}



void Q_getwd( char *out ){
#ifdef WIN32
	_getcwd( out, 256 );
#else
	// Gef: Changed from getwd() to getcwd() to avoid potential buffer overflow
	if ( !getcwd( out, 256 ) ) {
		strClear( out );
	}
#endif
	path_add_slash( out );
	FixDOSName( out );
}


void Q_mkdir( const char* path ){
	std::error_code err;
	std::filesystem::create_directories( path, err );
	if ( err ) {
		Error( "Q_mkdir %s: %s", path, err.message().c_str() );
	}
}



/*
   =============================================================================

                        MISC FUNCTIONS

   =============================================================================
 */


/*
   ================
   Q_filelength
   ================
 */
int Q_filelength( FILE *f ){
#ifdef WIN32
	const auto pos = _ftelli64( f );
	if ( pos < 0 || _fseeki64( f, 0, SEEK_END ) != 0 ) Error( "File seek failure" );
	const auto end = _ftelli64( f );
	if ( _fseeki64( f, pos, SEEK_SET ) != 0 ) Error( "File seek failure" );
#else
	const auto pos = ftello( f );
	if ( pos < 0 || fseeko( f, 0, SEEK_END ) != 0 ) Error( "File seek failure" );
	const auto end = ftello( f );
	if ( fseeko( f, pos, SEEK_SET ) != 0 ) Error( "File seek failure" );
#endif
	if ( end < 0 || end >= std::numeric_limits<int>::max() ) Error( "File exceeds supported 2 GiB size limit" );
	return int(end);
}


FILE *SafeOpenWrite( const char *filename, const char *mode ){
	FILE *f = fopen( filename, mode );

	if ( !f ) {
		Error( "Error opening %s: %s", filename, strerror( errno ) );
	}

	return f;
}

FILE *SafeOpenRead( const char *filename, const char *mode ){
	FILE *f = fopen( filename, mode );

	if ( !f ) {
		Error( "Error opening %s: %s", filename, strerror( errno ) );
	}

	return f;
}


void SafeRead( FILE *f, MemBuffer& buffer ){
	if ( fread( buffer.data(), 1, buffer.size(), f ) != buffer.size() ) {
		Error( "File read failure" );
	}
}


void SafeWrite( FILE *f, const void *buffer, int count ){
	if ( count < 0 || ( count && !buffer ) ) Error( "Invalid file write length/buffer" );
	if ( count && fwrite( buffer, 1, size_t(count), f ) != size_t(count) ) {
		Error( "File write failure" );
	}
}

void SafeClose( FILE *f ){
	const bool failed = ferror(f) != 0;
	if ( fclose(f) != 0 || failed ) Error( "File close/flush failure" );
}


/*
   ==============
   FileExists
   ==============
 */
bool    FileExists( const char *filename ){
	return access( filename, R_OK ) == 0;
}

/*
   ==============
   LoadFile
   ==============
 */
MemBuffer LoadFile( const char *filename ){
	FILE *f = SafeOpenRead( filename );
	MemBuffer buffer( Q_filelength( f ) );
	SafeRead( f, buffer );
	fclose( f );

	return buffer;
}


/*
   ==============
   SaveFile
   ==============
 */
void    SaveFile( const char *filename, const void *buffer, int count ){
	try {
		q3mapx::AtomicFile output(filename);
		FILE *f = SafeOpenWrite( output.temporary().string().c_str() );
		SafeWrite( f, buffer, count );
		SafeClose( f );
		output.commit();
	}
	catch ( const std::exception& error ) { Error( "%s", error.what() ); }
}


/*
   ============================================================================

                    BYTE ORDER FUNCTIONS

   ============================================================================
 */

#ifdef _SGI_SOURCE
#define __BIG_ENDIAN__
#endif

#ifdef __BIG_ENDIAN__

short   LittleShort( short l ){
	byte b1, b2;

	b1 = l & 255;
	b2 = ( l >> 8 ) & 255;

	return ( b1 << 8 ) + b2;
}

short   BigShort( short l ){
	return l;
}


int    LittleLong( int l ){
	byte b1, b2, b3, b4;

	b1 = l & 255;
	b2 = ( l >> 8 ) & 255;
	b3 = ( l >> 16 ) & 255;
	b4 = ( l >> 24 ) & 255;

	return ( (int)b1 << 24 ) + ( (int)b2 << 16 ) + ( (int)b3 << 8 ) + b4;
}

int    BigLong( int l ){
	return l;
}


float   LittleFloat( float l ){
	union {byte b[4]; float f; } in, out;

	in.f = l;
	out.b[0] = in.b[3];
	out.b[1] = in.b[2];
	out.b[2] = in.b[1];
	out.b[3] = in.b[0];

	return out.f;
}

float   BigFloat( float l ){
	return l;
}


#else


short   BigShort( short l ){
	byte b1, b2;

	b1 = l & 255;
	b2 = ( l >> 8 ) & 255;

	return ( b1 << 8 ) + b2;
}

short   LittleShort( short l ){
	return l;
}


int    BigLong( int l ){
	byte b1, b2, b3, b4;

	b1 = l & 255;
	b2 = ( l >> 8 ) & 255;
	b3 = ( l >> 16 ) & 255;
	b4 = ( l >> 24 ) & 255;

	return ( (int)b1 << 24 ) + ( (int)b2 << 16 ) + ( (int)b3 << 8 ) + b4;
}

int    LittleLong( int l ){
	return l;
}

float   BigFloat( float l ){
	union {byte b[4]; float f; } in, out;

	in.f = l;
	out.b[0] = in.b[3];
	out.b[1] = in.b[2];
	out.b[2] = in.b[1];
	out.b[3] = in.b[0];

	return out.f;
}

float   LittleFloat( float l ){
	return l;
}


#endif
