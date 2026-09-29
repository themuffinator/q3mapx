// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <cstdint>

// Each lighting job owns its random stream. Worker count and other jobs' early
// exits cannot change its sample positions. Keep entity-light startup RNG separate.
inline thread_local std::uint32_t lightingRandomState;
inline void SeedLightingRandom( std::uint32_t domain, std::uint32_t item ){
	lightingRandomState = item ^ ( domain * 0x9e3779b9u );
}
inline float LightingRandom(){
	lightingRandomState += 0x9e3779b9u;
	std::uint32_t value = lightingRandomState;
	value = ( value ^ ( value >> 16 ) ) * 0x85ebca6bu;
	value = ( value ^ ( value >> 13 ) ) * 0xc2b2ae35u;
	value ^= value >> 16;
	return float( value >> 8 ) * ( 1.0f / 16777216.0f );
}

// Ordinary reads/resets occur only outside joined worker passes. Atomic references
// keep diagnostics race-free without changing the existing reporting interfaces.
inline void AddLightStatistic( std::uint64_t& counter, std::uint64_t amount = 1 ){
	static_assert( std::atomic_ref<std::uint64_t>::required_alignment <= alignof( std::uint64_t ) );
	std::atomic_ref<std::uint64_t>( counter ).fetch_add( amount, std::memory_order_relaxed );
}
