// Windows replacement for port/src/platform/memory.cpp (generated tree only).
#include "memory.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {

std::size_t RoundUp(std::size_t value, std::size_t to) { return (value + to - 1) / to * to; }

} // namespace

std::size_t ArenaMemoryPageSize() {
    static const std::size_t size = [] {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return static_cast<std::size_t>(info.dwPageSize);
    }();
    return size;
}

ArenaMemory ArenaMemoryMap(std::size_t bytes, std::size_t lead) {
    std::size_t page = ArenaMemoryPageSize();
    std::size_t capacity = RoundUp(bytes == 0 ? 64 : bytes, 64);
    std::size_t map_size = RoundUp(lead + capacity, page) + page;
    void       *map = VirtualAlloc(nullptr, map_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (map == nullptr) {
        std::fprintf(stderr, "arena: cannot map %zu bytes\n", map_size);
        std::abort();
    }
    auto *bytes_map = static_cast<unsigned char *>(map);
    DWORD old;
    VirtualProtect(bytes_map + map_size - page, page, PAGE_NOACCESS, &old);
    return {bytes_map, map_size, bytes_map + map_size - page - capacity, capacity, lead};
}

// Decommitting and recommitting gives zeroed pages and returns the memory to the system.
void ArenaMemoryZeroByRemap(const ArenaMemory &memory) {
    std::size_t usable = memory.map_size - ArenaMemoryPageSize();
    if (!VirtualFree(memory.map, usable, MEM_DECOMMIT) ||
        VirtualAlloc(memory.map, usable, MEM_COMMIT, PAGE_READWRITE) != memory.map) {
        std::fprintf(stderr, "arena: cannot re-map %zu bytes at %p to zero them\n", usable,
                     static_cast<void *>(memory.map));
        std::abort();
    }
}

void ArenaMemoryZero(const ArenaMemory &memory) { ArenaMemoryZeroByRemap(memory); }

const unsigned char *ArenaMemoryGuard(const ArenaMemory &memory) { return memory.base + memory.capacity; }

void ArenaMemoryUnmap(ArenaMemory &memory) {
    if (memory.map != nullptr) {
        VirtualFree(memory.map, 0, MEM_RELEASE);
    }
    memory = {};
}
