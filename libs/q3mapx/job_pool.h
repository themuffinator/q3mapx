// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace q3mapx {
struct JobRun {
	size_t items;
	unsigned workers;
	size_t grain;
	double seconds;
};

// One synchronous parallel range at a time. Workers persist across calls; the
// submitting thread participates. Nested calls on this pool execute inline.
class JobPool {
public:
	using Function = std::function<void( size_t )>;
	using Progress = std::function<void( size_t, size_t )>;
	explicit JobPool( unsigned concurrency );
	~JobPool();
	JobPool( const JobPool& ) = delete;
	JobPool& operator=( const JobPool& ) = delete;
	unsigned concurrency() const { return unsigned( workers_.size() ) + 1; }
	// grain=0 uses bounded adaptive batching. Use grain=1 for uneven costly jobs.
	JobRun parallelFor( size_t count, const Function& function, size_t grain = 0, const Progress& progress = {} );

private:
	struct Job {
		size_t count = 0, grain = 1;
		unsigned participants = 1;
		Function function;
		Progress progress;
	} job_;
	std::vector<std::thread> workers_;
	std::mutex submission_, state_, progressMutex_;
	std::condition_variable ready_, finished_;
	bool stopping_ = false;
	size_t generation_ = 0;
	unsigned pending_ = 0;
	std::exception_ptr error_;
	alignas(64) std::atomic<size_t> next_{0};
	alignas(64) std::atomic<size_t> completed_{0};
	std::atomic<bool> cancelled_{false};
	std::atomic<unsigned> reported_{0};
	static thread_local JobPool* activePool_;
	void worker( unsigned index );
	void execute();
	void reportProgress( size_t completed );
};
} // namespace q3mapx
