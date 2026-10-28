#include "AllocationTracking.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

#ifdef _WIN32
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <malloc.h>
#endif

// Allocation counting for DucklingDev (see AllocationTracking.h). This file replaces the global operator new /
// delete of the whole binary; they only count while the calling thread's flag is set.
//
// operator new alone is not enough: JUCE's own containers and buffers (HeapBlock: AudioBuffer::setSize, Array,
// MemoryBlock ...) call std::malloc / calloc / realloc / free directly. With the dynamic CRT those calls go through
// this executable's import address table, so installHeapHooks() points those table entries at counting wrappers that
// forward to the original functions. The replaced operator new / delete call the originals directly, so one
// allocation is counted once.

namespace
{
thread_local bool tlTrack = false;
std::atomic<long long> gAllocations { 0 }, gFrees { 0 };

using MallocFn = void* (*) (std::size_t);
using CallocFn = void* (*) (std::size_t, std::size_t);
using ReallocFn = void* (*) (void*, std::size_t);
using FreeFn = void (*) (void*);

// The CRT's functions as imported before hooking (null until installHeapHooks() found them).
std::atomic<MallocFn> realMalloc { nullptr };
std::atomic<CallocFn> realCalloc { nullptr };
std::atomic<ReallocFn> realRealloc { nullptr };
std::atomic<FreeFn> realFree { nullptr };

inline void countAllocation() noexcept
{
    if (tlTrack)
        gAllocations.fetch_add (1, std::memory_order_relaxed);
}

inline void countFree (const void* p) noexcept
{
    if (p != nullptr && tlTrack)
        gFrees.fetch_add (1, std::memory_order_relaxed);
}

void* rawMalloc (std::size_t size) noexcept
{
    if (const auto f = realMalloc.load (std::memory_order_relaxed))
        return f (size);

    return std::malloc (size);
}

void rawFree (void* p) noexcept
{
    if (const auto f = realFree.load (std::memory_order_relaxed))
        f (p);
    else
        std::free (p);
}

#ifdef _WIN32
void* hookMalloc (std::size_t size)
{
    countAllocation();
    return realMalloc.load (std::memory_order_relaxed) (size);
}

void* hookCalloc (std::size_t count, std::size_t size)
{
    countAllocation();
    return realCalloc.load (std::memory_order_relaxed) (count, size);
}

void* hookRealloc (void* p, std::size_t size)
{
    if (size == 0 && p != nullptr)
        countFree (p);   // the CRT frees the block
    else
        countAllocation();

    return realRealloc.load (std::memory_order_relaxed) (p, size);
}

void hookFree (void* p)
{
    countFree (p);
    realFree.load (std::memory_order_relaxed) (p);
}

bool isCrtHeapDll (const char* name) noexcept
{
    return _strnicmp (name, "api-ms-win-crt-heap-", 20) == 0 || _stricmp (name, "ucrtbase.dll") == 0;
}

// Writes `hook` into one import address table entry (made writable for the moment).
bool patchEntry (IMAGE_THUNK_DATA* entry, ULONG_PTR hook) noexcept
{
    DWORD oldProtection = 0;

    if (! VirtualProtect (&entry->u1.Function, sizeof (entry->u1.Function), PAGE_READWRITE, &oldProtection))
        return false;

    entry->u1.Function = hook;
    DWORD unused = 0;
    VirtualProtect (&entry->u1.Function, sizeof (entry->u1.Function), oldProtection, &unused);
    return true;
}

std::string hookImports()
{
    auto* base = reinterpret_cast<unsigned char*> (GetModuleHandleW (nullptr));
    if (base == nullptr)
        return {};

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*> (base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return {};

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*> (base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return {};

    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0)
        return {};

    std::string hooked;

    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*> (base + directory.VirtualAddress); d->Name != 0; ++d)
    {
        if (! isCrtHeapDll (reinterpret_cast<const char*> (base + d->Name)) || d->OriginalFirstThunk == 0)
            continue;

        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*> (base + d->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*> (base + d->FirstThunk);

        for (; names->u1.AddressOfData != 0; ++names, ++addresses)
        {
            if (IMAGE_SNAP_BY_ORDINAL (names->u1.Ordinal))
                continue;

            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*> (base + names->u1.AddressOfData);
            const char* name = reinterpret_cast<const char*> (byName->Name);
            const auto original = static_cast<ULONG_PTR> (addresses->u1.Function);
            ULONG_PTR hook = 0;

            // Store the original first: the hook may run (on any thread) as soon as the entry is written.
            if (std::strcmp (name, "malloc") == 0 && realMalloc.load() == nullptr)
            {
                realMalloc.store (reinterpret_cast<MallocFn> (original));
                hook = reinterpret_cast<ULONG_PTR> (&hookMalloc);
            }
            else if (std::strcmp (name, "calloc") == 0 && realCalloc.load() == nullptr)
            {
                realCalloc.store (reinterpret_cast<CallocFn> (original));
                hook = reinterpret_cast<ULONG_PTR> (&hookCalloc);
            }
            else if (std::strcmp (name, "realloc") == 0 && realRealloc.load() == nullptr)
            {
                realRealloc.store (reinterpret_cast<ReallocFn> (original));
                hook = reinterpret_cast<ULONG_PTR> (&hookRealloc);
            }
            else if (std::strcmp (name, "free") == 0 && realFree.load() == nullptr)
            {
                realFree.store (reinterpret_cast<FreeFn> (original));
                hook = reinterpret_cast<ULONG_PTR> (&hookFree);
            }

            if (hook != 0 && patchEntry (addresses, hook))
                hooked += (hooked.empty() ? "" : " ") + std::string (name);
        }
    }

    return hooked;
}
#endif
} // namespace

//==============================================================================
namespace dkdev
{
void setAllocationTracking (bool enabled) noexcept { tlTrack = enabled; }

AllocationCounts getAllocationCounts() noexcept
{
    return { gAllocations.load(), gFrees.load() };
}

void setAllocationCounts (AllocationCounts c) noexcept
{
    gAllocations.store (c.allocations);
    gFrees.store (c.frees);
}

std::string installHeapHooks()
{
   #ifdef _WIN32
    static const std::string hooked = hookImports();   // once (thread-safe static initialisation)
    return hooked;
   #else
    return {};
   #endif
}
} // namespace dkdev

//==============================================================================
// Global operator new / delete (whole DucklingDev binary).
void* operator new (std::size_t size)
{
    countAllocation();

    if (void* p = rawMalloc (size > 0 ? size : 1))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept
{
    countFree (p);
    rawFree (p);
}

void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete[] (void* p) noexcept { operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { operator delete (p); }

// Over-aligned types (alignas > 16) use these.
void* operator new (std::size_t size, std::align_val_t alignment)
{
    countAllocation();
    const auto align = static_cast<std::size_t> (alignment);

   #ifdef _WIN32
    void* p = _aligned_malloc (size > 0 ? size : 1, align);
   #else
    void* p = std::aligned_alloc (align, ((size > 0 ? size : 1) + align - 1) / align * align);
   #endif

    if (p != nullptr)
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p, std::align_val_t) noexcept
{
    countFree (p);

   #ifdef _WIN32
    _aligned_free (p);
   #else
    std::free (p);
   #endif
}

void* operator new[] (std::size_t size, std::align_val_t alignment) { return operator new (size, alignment); }
void operator delete[] (void* p, std::align_val_t alignment) noexcept { operator delete (p, alignment); }
void operator delete (void* p, std::size_t, std::align_val_t alignment) noexcept { operator delete (p, alignment); }
void operator delete[] (void* p, std::size_t, std::align_val_t alignment) noexcept { operator delete (p, alignment); }
