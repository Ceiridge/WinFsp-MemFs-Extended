#pragma once

#include "globalincludes.h"

namespace Memfs {
	// Make sure that this struct is never padded, no matter what sector size is used
#pragma pack(push, memefsNoPadding, 1)
	struct Sector {
		byte Bytes[FULL_SECTOR_SIZE];
	};
#pragma pack(pop, memefsNoPadding)

	using SectorVector = std::vector<Sector*>;

	struct SectorNode {
		SectorVector Sectors;

		SectorNode() = default;
		// This must free all sectors on destruction!
		~SectorNode();

		SectorNode(const SectorNode& other) = delete;
		SectorNode(SectorNode&& other) noexcept;
		SectorNode& operator=(const SectorNode& other) = delete;
		SectorNode& operator=(SectorNode&& other) noexcept;

		[[nodiscard]] size_t ApproximateSize() const;
	};

	class SectorManager {
	public:
		SectorManager() = default;
		~SectorManager() = default;

		SectorManager(const SectorManager& other) = delete;
		SectorManager(SectorManager&& other) noexcept = default;
		SectorManager& operator=(const SectorManager& other) = delete;
		SectorManager& operator=(SectorManager&& other) noexcept = default;

		static size_t AlignSize(const size_t size, const bool alignUp = true);
		static UINT64 GetSectorAmount(const size_t alignedSize);

		template <bool IsReading>
		static bool ReadWrite(SectorNode& node, void* buffer, const size_t size, const size_t offset);

		bool ReAllocate(SectorNode& node, const size_t size);
		bool Free(SectorNode& node);

		bool IsFullyEmpty();
		UINT64 GetAllocatedSectors();
	private:
		volatile UINT64 allocatedSectors{0};
	};
}
