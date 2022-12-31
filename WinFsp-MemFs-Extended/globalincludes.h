#pragma once

#include <Windows.h>
#include <winfsp/winfsp.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <mutex>
#include <vector>
#include <type_traits>
#include <exception>
#include <cstdint>
#include <cassert>
#include <sddl.h>

namespace std {
	template <typename T>
	using refoptional = optional<reference_wrapper<T>>;
}

namespace Memfs {
	static constexpr int MEMFS_MAX_PATH = 32766;
	static constexpr UINT64 MEMFS_SECTOR_SIZE = 512;
	static constexpr UINT64 MEMFS_SECTORS_PER_ALLOCATION_UNIT = 1;

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
