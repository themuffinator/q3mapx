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

// q3mapx: persistent jobs replace the original per-pass thread arrays and global dispatch lock.
#include "cmdlib.h"
#include "inout.h"
#include "qthreads.h"
#include "timer.h"
#include "q3mapx/job_pool.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/stringbuffer.h"
#include <algorithm>
#include <mutex>
#include <string>

int numthreads = -1;
namespace {
// Explicit normal shutdown: Error() may call exit() from a worker. Registering
// a static destructor would try to join that same worker during fatal exit.
q3mapx::JobPool* pool = nullptr;
std::mutex compilerMutex;
thread_local bool holdsCompilerMutex = false;
struct Pass { std::string name; q3mapx::JobRun run; double totalSeconds; };
std::vector<Pass> passes;
}

void ThreadSetDefault(){
    if ( numthreads == -1 ) numthreads = int( std::clamp( std::thread::hardware_concurrency(), 1u, 1024u ) );
    if ( numthreads < 1 || numthreads > 1024 ) Error( "Thread count must be in 1..1024" );
    Sys_Printf( "%i threads (persistent job pool)\n", numthreads );
}

void ThreadLock(){
    if ( holdsCompilerMutex ) Error( "Recursive ThreadLock" );
    compilerMutex.lock();
    holdsCompilerMutex = true;
}

void ThreadUnlock(){
    if ( !holdsCompilerMutex ) Error( "ThreadUnlock without lock" );
    holdsCompilerMutex = false;
    compilerMutex.unlock();
}

void RunThreadsOnIndividual( int workcnt, bool showpacifier, void ( *func )( int ), const char* name, size_t grain ){
    if ( workcnt < 0 || !func ) Error( "Invalid parallel work range" );
    if ( numthreads == -1 ) ThreadSetDefault();
    Timer timer;
    int previousBucket = -1;
    try {
        if ( !pool || pool->concurrency() != unsigned( numthreads ) ) {
            delete pool;
            pool = nullptr;
            pool = new q3mapx::JobPool( unsigned( numthreads ) );
        }
        q3mapx::JobPool::Progress progress;
        if ( showpacifier ) {
            progress = [&]( size_t done, size_t total ){
                const int bucket = int( uint64_t( done ) * 40 / total );
                while ( previousBucket < bucket ) {
                    ++previousBucket;
                    if ( previousBucket % 4 == 0 ) Sys_Printf( "%i", previousBucket / 4 );
                    else Sys_Printf( "." );
                }
                fflush( stdout );
            };
        }
        const auto result = pool->parallelFor( size_t( workcnt ), [func]( size_t i ){ func( int( i ) ); }, grain, progress );
        passes.push_back( { name, result, timer.elapsed_sec() } );
    }
    catch ( const std::exception& error ) { Error( "Job system: %s", error.what() ); }
    catch ( ... ) { Error( "Job system: unknown worker exception" ); }
    if ( showpacifier ) Sys_Printf( " (%.3f s)\n", timer.elapsed_sec() );
}

void ThreadShutdown(){
    delete pool;
    pool = nullptr;
}

void ThreadWriteProfile( const char* filename, double totalSeconds, int exitCode ){
    if ( !filename ) return;
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer( buffer );
    writer.StartObject();
    writer.Key( "schema_version" ); writer.Int( 1 );
    writer.Key( "total_seconds" ); writer.Double( totalSeconds );
    writer.Key( "exit_code" ); writer.Int( exitCode );
    writer.Key( "requested_workers" ); writer.Int( numthreads );
    writer.Key( "passes" ); writer.StartArray();
    for ( const auto& pass : passes ) {
        writer.StartObject();
        writer.Key( "name" ); writer.String( pass.name.c_str() );
        writer.Key( "items" ); writer.Uint64( pass.run.items );
        writer.Key( "workers" ); writer.Uint( pass.run.workers );
        writer.Key( "grain" ); writer.Uint64( pass.run.grain );
        writer.Key( "seconds" ); writer.Double( pass.run.seconds );
        writer.Key( "seconds_with_setup" ); writer.Double( pass.totalSeconds );
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();
    SaveFile( filename, buffer.GetString(), int( buffer.GetSize() ) );
    Sys_Printf( "CPU profile: %s\n", filename );
}
