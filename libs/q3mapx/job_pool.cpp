// SPDX-License-Identifier: GPL-3.0-or-later
#include "job_pool.h"
#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

namespace q3mapx {
thread_local JobPool* JobPool::activePool_ = nullptr;
using Clock = std::chrono::steady_clock;

JobPool::JobPool( unsigned concurrency ){
	if ( concurrency < 1 || concurrency > 1024 ) throw std::invalid_argument( "Worker count must be in 1..1024" );
	try {
		workers_.reserve( concurrency - 1 );
		for ( unsigned i = 1; i < concurrency; ++i ) workers_.emplace_back( [this, i]{ worker( i ); } );
	}
	catch ( ... ) {
		{ std::lock_guard lock( state_ ); stopping_ = true; }
		ready_.notify_all();
		for ( auto& thread : workers_ ) thread.join();
		throw;
	}
}

JobPool::~JobPool(){
	// The owner must outlive all submissions (ordinary C++ object lifetime rules).
	{ std::lock_guard lock( state_ ); stopping_ = true; }
	ready_.notify_all();
	for ( auto& thread : workers_ ) thread.join();
}

void JobPool::worker( unsigned index ){
	size_t seen = 0;
	std::unique_lock lock( state_ );
	while ( true ) {
		ready_.wait( lock, [&]{ return stopping_ || generation_ != seen; } );
		if ( stopping_ ) return;
		seen = generation_;
		if ( index >= job_.participants ) continue;
		lock.unlock();
		execute();
		lock.lock();
		if ( --pending_ == 0 ) finished_.notify_one();
	}
}

void JobPool::reportProgress( size_t completed ){
	if ( !job_.progress ) return;
	const unsigned percent = unsigned( uint64_t( completed ) * 100 / job_.count );
	if ( percent <= reported_.load( std::memory_order_relaxed ) ) return;
	std::lock_guard lock( progressMutex_ );
	if ( percent <= reported_.load( std::memory_order_relaxed ) ) return;
	reported_.store( percent, std::memory_order_relaxed );
	job_.progress( completed, job_.count );
}

void JobPool::execute(){
	JobPool* previous = activePool_;
	activePool_ = this;
	try {
		while ( !cancelled_.load( std::memory_order_relaxed ) ) {
			const size_t first = next_.fetch_add( job_.grain, std::memory_order_relaxed );
			if ( first >= job_.count ) break;
			const size_t end = std::min( first + job_.grain, job_.count );
			for ( size_t i = first; i < end; ++i ) job_.function( i );
			const size_t done = completed_.fetch_add( end - first, std::memory_order_relaxed ) + end - first;
			reportProgress( done );
		}
	}
	catch ( ... ) {
		std::lock_guard lock( state_ );
		if ( !error_ ) error_ = std::current_exception();
		cancelled_.store( true, std::memory_order_relaxed );
	}
	activePool_ = previous;
}

JobRun JobPool::parallelFor( size_t count, const Function& function, size_t grain, const Progress& progress ){
	if ( count > size_t( std::numeric_limits<int>::max() ) ) throw std::length_error( "Job range exceeds INT_MAX" );
	if ( !function ) throw std::invalid_argument( "Missing job function" );
	const auto start = Clock::now();
	const auto elapsed = [&]{ return std::chrono::duration<double>( Clock::now() - start ).count(); };
	if ( activePool_ == this ) {
		for ( size_t i = 0; i < count; ++i ) function( i );
		if ( progress && count ) progress( count, count );
		return { count, 1, 1, elapsed() };
	}
	std::lock_guard submission( submission_ );
	if ( count == 0 ) return { 0, 0, 1, elapsed() };
	const auto participants = unsigned( std::min( count, size_t( concurrency() ) ) );
	if ( participants == 1 ) {
		JobPool* previous = activePool_;
		activePool_ = this;
		try {
			for ( size_t i = 0; i < count; ++i ) function( i );
			if ( progress ) progress( count, count );
		}
		catch ( ... ) { activePool_ = previous; throw; }
		activePool_ = previous;
		return { count, 1, 1, elapsed() };
	}
	if ( grain == 0 ) grain = std::clamp( count / ( participants * 16 ), size_t(1), size_t(64) );
	grain = std::clamp( grain, size_t(1), count );
	{
		std::lock_guard lock( state_ );
		job_ = { count, grain, participants, function, progress };
		next_.store( 0, std::memory_order_relaxed );
		completed_.store( 0, std::memory_order_relaxed );
		cancelled_.store( false, std::memory_order_relaxed );
		reported_.store( 0, std::memory_order_relaxed );
		error_ = nullptr;
		pending_ = participants - 1;
		++generation_;
	}
	ready_.notify_all();
	execute();
	std::unique_lock lock( state_ );
	finished_.wait( lock, [&]{ return pending_ == 0; } );
	// Release user captures before returning, after every worker has stopped using them.
	job_.function = {};
	job_.progress = {};
	if ( error_ ) std::rethrow_exception( error_ );
	return { count, participants, grain, elapsed() };
}
} // namespace q3mapx
