#pragma once

#include <string>

// Allocation counting for DucklingDev's realtime checks (owner: processor agent; implementation in
// AllocationTracking.cpp, which also replaces the global operator new / delete of the whole DucklingDev binary).
//
// Counting is per thread: only allocations and frees made on a thread while its tracking flag is on are counted, so
// the replacement is harmless for `snapshot` and `compare`. Counted: operator new / delete (all forms, including the
// aligned ones) and, after installHeapHooks(), the CRT heap functions malloc / calloc / realloc / free that JUCE's
// HeapBlock (AudioBuffer, Array, MemoryBlock ...) calls directly.
namespace dkdev
{
struct AllocationCounts
{
    long long allocations = 0;
    long long frees = 0;
};

void setAllocationTracking (bool enabled) noexcept;   // for the calling thread
AllocationCounts getAllocationCounts() noexcept;      // all threads, since start (or the last setAllocationCounts)
void setAllocationCounts (AllocationCounts) noexcept;

// Windows (dynamic CRT): points this executable's imports of malloc, calloc, realloc and free at counting wrappers.
// Idempotent. Returns the hooked names separated by spaces, e.g. "malloc calloc realloc free"; empty when nothing
// could be hooked (other platforms, static CRT), which the self-test reports as a failure.
std::string installHeapHooks();
} // namespace dkdev
