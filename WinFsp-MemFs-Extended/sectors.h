#pragma once

#include <vector>

#include "memfs.h"

namespace Memfs {
	static constexpr size_t FULL_SECTOR_SIZE = MEMFS_SECTOR_SIZE * MEMFS_SECTORS_PER_ALLOCATION_UNIT;

	// Make sure that this struct is never padded, no matter what sector size is used
#pragma pack(push, memefsNoPadding, 1)
	struct Sector {
		byte Bytes[FULL_SECTOR_SIZE];
	};
#pragma pack(pop, memefsNoPadding)

	using SectorVector = std::vector<Sector*>;

	struct SectorNode {
		SectorVector Sectors;
		std::mutex SectorsMutex;
	};

	class SectorManager {
	public:
		SectorManager();
		~SectorManager();

		SectorManager(const SectorManager& other) = delete;
		SectorManager(SectorManager&& other) noexcept;
		SectorManager& operator=(const SectorManager& other) = delete;
		SectorManager& operator=(SectorManager&& other) noexcept;

		static size_t AlignSize(const size_t size, const bool alignUp = true);
		static UINT64 GetSectorAmount(const size_t alignedSize);

		template <bool IsReading>
		static bool ReadWrite(SectorNode& node, void* buffer, const size_t size, const size_t offset);

		bool ReAllocate(SectorNode& node, const size_t size);
		bool Free(SectorNode& node);
	private:
		HANDLE heap;
	};
}
