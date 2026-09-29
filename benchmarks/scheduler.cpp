// SPDX-License-Identifier: GPL-3.0-or-later
// Microbenchmark only: reference models NRC's fresh threads + per-item mutex.
#include "q3mapx/job_pool.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>

using Clock = std::chrono::steady_clock;
static void reference( unsigned threads, size_t count, const q3mapx::JobPool::Function& function ){
    std::mutex mutex;
    size_t next = 0;
    std::vector<std::thread> workers;
    for ( unsigned t = 0; t < threads; ++t ) workers.emplace_back( [&]{
        while ( true ) {
            size_t i;
            { std::lock_guard lock( mutex ); i = next++; }
            if ( i >= count ) break;
            function( i );
        }
    } );
    for ( auto& worker : workers ) worker.join();
}

int main(){
    const unsigned threads = std::clamp( std::thread::hardware_concurrency(), 1u, 20u );
    q3mapx::JobPool pool( threads );
    constexpr size_t count = 262144;
    std::vector<uint64_t> values( count );
    const auto function = [&]( size_t i ){ values[i] = uint64_t(i) * (i + 17); };
    std::cout << "{\"kind\":\"scheduler_microbenchmark\",\"workers\":" << threads
              << ",\"items_per_pass\":" << count << ",\"passes_per_run\":8,\"records\":[";
    for ( int run = -1; run < 5; ++run ) {
        double seconds[2];
        for ( int position = 0; position < 2; ++position ) {
            const int method = (position + run + 1) % 2;
            const auto start = Clock::now();
            for ( int pass = 0; pass < 8; ++pass ) {
                if ( method ) pool.parallelFor( count, function );
                else reference( threads, count, function );
            }
            seconds[method] = std::chrono::duration<double>(Clock::now() - start).count();
            uint64_t expected = 0;
            for ( size_t i = 0; i < count; ++i ) expected += uint64_t(i) * (i + 17);
            if ( std::accumulate( values.begin(), values.end(), uint64_t(0) ) != expected ) return 1;
        }
        if ( run >= 0 ) std::cout << (run ? "," : "") << "{\"reference_seconds\":" << seconds[0]
                                 << ",\"pool_seconds\":" << seconds[1] << "}";
    }
    std::cout << "]}\n";
}
