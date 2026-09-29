// SPDX-License-Identifier: GPL-3.0-or-later
#include "q3mapx/job_pool.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

static void require( bool condition, const char* message ){
	if ( !condition ) { std::cerr << message << '\n'; std::exit( 1 ); }
}

int main(){
	for ( unsigned threads : { 1u, 2u, 4u, 16u, 70u } ) {
		q3mapx::JobPool pool( threads );
		for ( size_t count : { 0u, 1u, 3u, 1000u, 100003u } ) {
			for ( size_t grain : { 0u, 1u, 17u, 4096u } ) {
				std::vector<std::atomic<unsigned>> seen( count );
				size_t progress = 0;
				pool.parallelFor( count, [&]( size_t i ){ seen[i].fetch_add( 1, std::memory_order_relaxed ); }, grain,
				    [&]( size_t done, size_t total ){
				        require( done > progress && done <= total && total == count, "Progress not monotonic" ); progress = done;
				    } );
				for ( const auto& value : seen ) require( value.load() == 1, "Item omitted or executed twice" );
				require( progress == count, "Missing completed progress" );
			}
		}
		std::atomic<unsigned> nested{0};
		pool.parallelFor( 37, [&]( size_t ){
			pool.parallelFor( 19, [&]( size_t ){ ++nested; } );
		} );
		require( nested == 37 * 19, "Nested work lost" );
		bool caught = false;
		try { pool.parallelFor( 100, []( size_t i ){ if ( i == 7 ) throw std::runtime_error( "test failure" ); }, 1 ); }
		catch ( const std::runtime_error& ) { caught = true; }
		require( caught, "Worker exception not propagated" );
		std::atomic<unsigned> afterError{0};
		pool.parallelFor( 100, [&]( size_t ){ ++afterError; } );
		require( afterError == 100, "Pool unusable after exception" );
	}
	q3mapx::JobPool shared( 4 );
	std::atomic<unsigned> total{0};
	std::vector<std::thread> submitters;
	for ( int i = 0; i < 4; ++i ) submitters.emplace_back( [&]{
		for ( int pass = 0; pass < 50; ++pass ) shared.parallelFor( 101, [&]( size_t ){ ++total; } );
	} );
	for ( auto& thread : submitters ) thread.join();
	require( total == 4 * 50 * 101, "Concurrent submission lost jobs" );
	bool rejected = false;
	try { q3mapx::JobPool invalid( 0 ); } catch ( const std::invalid_argument& ) { rejected = true; }
	require( rejected, "Zero workers accepted" );
	std::cout << "Exactly-once jobs, batching, 70 workers, nested work, exceptions and concurrent submissions passed\n";
}
