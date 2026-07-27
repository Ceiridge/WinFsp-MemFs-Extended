#pragma once

#include <Windows.h>
#include <winfsp/winfsp.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <shared_mutex>
#include <vector>
#include <type_traits>
#include <exception>
#include <cstdint>
#include <cassert>
#include <sddl.h>

// Diagnostics counters for the memory-safety audit (docs/MEMORY-SAFETY-AUDIT.md, step 1).
// Compiles away completely when 0; enable with /D MEMFS_DIAGNOSTICS=1.
#ifndef MEMFS_DIAGNOSTICS
#define MEMFS_DIAGNOSTICS 0
#endif

// Modifying std for convenience, although not recommended
namespace std {
	template <typename T>
	using refoptional = optional<reference_wrapper<T>>;
}

namespace Memfs {
	static constexpr int MEMFS_MAX_PATH = 32766;
	static constexpr UINT64 MEMFS_SECTOR_SIZE = 512;
	static constexpr UINT64 MEMFS_SECTORS_PER_ALLOCATION_UNIT = 128; // 64 KB allocation unit — matches VirtualAlloc granularity

	static constexpr size_t FULL_SECTOR_SIZE = MEMFS_SECTOR_SIZE * MEMFS_SECTORS_PER_ALLOCATION_UNIT;

	enum {
		MemfsDisk = 0x00000000,
		MemfsNet = 0x00000001,
		MemfsDeviceMask = 0x0000000f,
		MemfsCaseInsensitive = 0x80000000,
		MemfsFlushAndPurgeOnCleanup = 0x40000000,
		MemfsLegacyUnlinkRename = 0x20000000,
	};
}
