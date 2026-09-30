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

// scriplib.c

#include "cmdlib.h"
#include "inout.h"
#include "qstringops.h"
#include "qpathops.h"
#include "scriplib.h"
#include "stream/stringstream.h"
#include "stream/textstream.h"
#include "vfs.h"
#include <list>

/*
   =============================================================================

                        PARSING STUFF

   =============================================================================
 */

struct script_t
{
	const CopiedString filename;
	const CopiedString location;
	const MemBuffer buffer;
	const char *it, *end;
	int line;
	script_t( const char *filename, MemBuffer&& buffer_ ) :
		filename( filename ),
		location( g_loadedScriptLocation.c_str() ),
		buffer( std::move( buffer_ ) ),
		it( buffer.data() ),
		end( it + buffer.size() ),
		line( 1 )
	{}
	script_t( script_t&& ) noexcept = delete;
};

std::list<script_t> scriptstack;

int scriptline;
char token[MAXTOKEN];
bool tokenready;                     // only true if UnGetToken was just called
static bool tokenQuoted;
static bool scriptIncludesAllowed;
static size_t scriptFilesLoaded, scriptBytesLoaded;
// Per top-level document, including the root. Repeated shallow includes consume
// the file/byte budgets too; a depth limit alone cannot bound their expansion.
static constexpr size_t MAX_SCRIPT_DEPTH = 64;
static constexpr size_t MAX_SCRIPT_FILES = 1024;
static constexpr size_t MAX_SCRIPT_BYTES = 256 * 1024 * 1024;

bool TokenIs( const char *match ){
	return !tokenQuoted && strEqual( token, match );
}

[[noreturn]] static void IncompleteScriptLine(){
	Error( "Line %i is incomplete\nFile location be: %s\n", scriptline, g_loadedScriptLocation.c_str() );
}

/*
   ==============
   AddScriptToStack
   ==============
 */
static bool AddScriptToStack( const char *filename, int index, bool verbose ){
	if ( scriptstack.size() >= MAX_SCRIPT_DEPTH || scriptFilesLoaded >= MAX_SCRIPT_FILES ) {
		Error( "Script include limit exceeded at line %d in %s (maximum %zu active files, %zu total files)",
		       scriptline, g_loadedScriptLocation.c_str(), MAX_SCRIPT_DEPTH, MAX_SCRIPT_FILES );
	}
	const size_t remaining = MAX_SCRIPT_BYTES - scriptBytesLoaded;
	if ( MemBuffer buffer = vfsLoadFile( filename, index, true, remaining ) ) {
		if( verbose ){
			if ( index > 0 )
				Sys_Printf( "entering %s (%d)\n", filename, index + 1 );
			else
				Sys_Printf( "entering %s\n", filename );
		}

		scriptBytesLoaded += buffer.size();
		++scriptFilesLoaded;
		scriptstack.emplace_back( filename, std::move( buffer ) );
		scriptline = 1;
		return true;
	}
	else
	{
		Sys_FPrintf( SYS_WRN, "Script file %s could not be loaded (missing, unreadable or exceeds the %zu-byte remaining script budget)\n",
		             filename, remaining );

		return false;
	}
}


/*
   ==============
   LoadScriptFile
   ==============
 */
bool LoadScriptFile( const char *filename, int index /* = 0 */, bool verbose /* = true */ ){
	scriptstack.clear();
	tokenready = false;
	tokenQuoted = false;
	scriptIncludesAllowed = true;
	scriptFilesLoaded = scriptBytesLoaded = 0;
	return AddScriptToStack( filename, index, verbose );
}

/*
   ==============
   ParseFromMemory
   ==============
 */
void ParseFromMemory( const char *buffer, size_t size ){
	scriptstack.clear();
	tokenready = false;
	tokenQuoted = false;
	// BSP entity text is data, never a request to load files from the game VFS.
	scriptIncludesAllowed = false;
	g_loadedScriptLocation( "memory buffer" );
	MemBuffer bu( size );
	if ( size != 0 ) memcpy( bu.data(), buffer, size );
	scriptstack.emplace_back( "memory buffer", std::move( bu ) );
	scriptline = 1;
}


/*
   ==============
   UnGetToken

   Signals that the current token was not used, and should be reported
   for the next GetToken.  Note that

   GetToken( true );
   UnGetToken();
   GetToken( false );

   could cross a line boundary.
   ==============
 */
void UnGetToken(){
	ENSURE( !tokenready && "Can't UnGetToken() twice in a row!" );
	tokenready = true;
}


/*
   ==============
   GetToken
   ==============
 */
// Read only the current buffer. Include expansion and returning to parents are
// handled iteratively by GetToken, including chains of empty include files.
static bool ReadScriptToken( script_t& script, bool crossline ){
//
// skip space
//
skipspace:
	while ( script.it < script.end && *script.it <= 32 )
	{
		if ( *script.it++ == '\n' ) {
			if ( !crossline ) {
				IncompleteScriptLine();
			}
			script.line++;
			scriptline = script.line;
		}
	}

	if ( script.it >= script.end ) {
		return false;
	}

	// ; # // comments
	if ( *script.it == ';' || *script.it == '#'
	     || ( script.it[0] == '/' && script.it[1] == '/' ) ) {
		if ( !crossline ) {
			IncompleteScriptLine();
		}
		while ( *script.it++ != '\n' )
			if ( script.it >= script.end ) {
				return false;
			}
		script.line++;
		scriptline = script.line;
		goto skipspace;
	}

	// /* */ comments
	if ( script.it[0] == '/' && script.it[1] == '*' ) {
		script.it += 2;
		for ( ;; )
		{
			if ( script.end - script.it < 2 ) {
				Error( "Unterminated comment on line %i in file %s", scriptline, script.filename.c_str() );
			}
			if ( script.it[0] == '*' && script.it[1] == '/' ) break;
			if ( *script.it == '\n' ) {
				if ( !crossline ) {
					IncompleteScriptLine();
				}
				script.line++;
				scriptline = script.line;
			}
			script.it++;
		}
		script.it += 2;
		goto skipspace;
	}

//
// copy token
//
	char *token_p = token;
	tokenQuoted = *script.it == '"';

	if ( tokenQuoted ) {
		// quoted token
		script.it++;
		while ( script.it < script.end && *script.it != '"' )
		{
			if ( *script.it == '\0' ) {
				Error( "NUL in quoted token on line %i in file %s", scriptline, script.filename.c_str() );
			}
			// Reserve the terminator before every write, including at file EOF.
			if ( token_p == token + MAXTOKEN - 1 ) {
				Error( "Token too large on line %i\nFile location be: %s\n", scriptline, g_loadedScriptLocation.c_str() );
			}
			*token_p++ = *script.it++;
		}
		if ( script.it == script.end ) {
			Error( "Unterminated quote on line %i in file %s", scriptline, script.filename.c_str() );
		}
		script.it++;
	}
	else{   // regular token
		while ( script.it < script.end && *script.it > 32 && *script.it != ';' )
		{
			if ( token_p == token + MAXTOKEN - 1 ) {
				Error( "Token too large on line %i\nFile location be: %s\n", scriptline, g_loadedScriptLocation.c_str() );
			}
			*token_p++ = *script.it++;
		}
	}

	*token_p = 0;

	return true;
}

bool GetToken( bool crossline ){
	if ( tokenready ) {
		tokenready = false;
		return true;
	}
	for ( ;; ) {
		if ( scriptstack.empty() ) {
			if ( !crossline ) IncompleteScriptLine();
			token[0] = '\0';
			tokenQuoted = false;
			return false;
		}
		script_t& script = scriptstack.back();
		if ( !ReadScriptToken( script, crossline ) ) {
			if ( !crossline ) IncompleteScriptLine();
			scriptstack.pop_back();
			if ( !scriptstack.empty() ) {
				scriptline = scriptstack.back().line;
				g_loadedScriptLocation( scriptstack.back().location.c_str() );
				Sys_Printf( "returning to %s\n", scriptstack.back().filename.c_str() );
			}
			continue;
		}
		if ( !scriptIncludesAllowed || !TokenIs( "$include" ) ) return true;
		// A filename is a single token on this line, not another directive.
		if ( !ReadScriptToken( script, false ) ) IncompleteScriptLine();
		if ( token[0] == '\0' || !AddScriptToStack( token, 0, true ) ) {
			Error( "Cannot include '%s' at line %d in %s", token, script.line, script.location.c_str() );
		}
	}
}


/*
   ==============
   TokenAvailable

   Returns true if there is another token on the line
   ==============
 */
bool TokenAvailable() {
	/* save */
	const int oldLine = scriptline;

	/* test */
	if ( !GetToken( true ) ) {
		return false;
	}
	UnGetToken();
	if ( oldLine == scriptline ) {
		return true;
	}

	/* restore */
	//%	scriptline = oldLine;
	//%	script->line = oldScriptLine;

	return false;
}


//=====================================================================


void MatchToken( const char *match ) {
	if ( !GetToken( true ) || !TokenIs( match ) ) {
		Error( "MatchToken( \"%s\" ) failed at line %i in file %s", match, scriptline, g_loadedScriptLocation.c_str() );
	}
}


template<typename T>
void Parse1DMatrix( int x, T *m ) {
	MatchToken( "(" );

	for ( int i = 0; i < x; ++i ) {
		GetToken( false );
		m[i] = atof( token );
	}

	MatchToken( ")" );
}
template void Parse1DMatrix<float>( int x, float *m );
template void Parse1DMatrix<double>( int x, double *m );

void Parse2DMatrix( int y, int x, float *m ) {
	MatchToken( "(" );

	for ( int i = 0; i < y; ++i ) {
		Parse1DMatrix( x, m + i * x );
	}

	MatchToken( ")" );
}

void Parse3DMatrix( int z, int y, int x, float *m ) {
	MatchToken( "(" );

	for ( int i = 0; i < z; ++i ) {
		Parse2DMatrix( y, x, m + i * x * y );
	}

	MatchToken( ")" );
}


void Write1DMatrix( FILE *f, int x, float *m ) {
	fprintf( f, "( " );
	for ( int i = 0; i < x; ++i ) {
		if ( m[i] == (int)m[i] ) {
			fprintf( f, "%i ", (int)m[i] );
		}
		else {
			fprintf( f, "%f ", m[i] );
		}
	}
	fprintf( f, ")" );
}

void Write2DMatrix( FILE *f, int y, int x, float *m ) {
	fprintf( f, "( " );
	for ( int i = 0; i < y; ++i ) {
		Write1DMatrix( f, x, m + i * x );
		fprintf( f, " " );
	}
	fprintf( f, ")\n" );
}


void Write3DMatrix( FILE *f, int z, int y, int x, float *m ) {
	fprintf( f, "(\n" );
	for ( int i = 0; i < z; ++i ) {
		Write2DMatrix( f, y, x, m + i * ( x * y ) );
	}
	fprintf( f, ")\n" );
}
