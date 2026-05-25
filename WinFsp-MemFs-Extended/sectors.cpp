#include "globalincludes.h"
#include "sectors.h"

#include "memfs.h"

using namespace Memfs;

size_t SectorManager::AlignSize(const size_t size, const bool alignUp) {
	const size_t remainder = size % FULL_SECTOR_SIZE;
	if (remainder == 0) {
		return size;
	}

	return size + (alignUp ? FULL_SECTOR_SIZE : 0) - remainder;
}

UINT64 SectorManager::GetSectorAmount(const size_t alignedSize) {
	return alignedSize / FULL_SECTOR_SIZE;
}


// Caller takes care of locking
bool SectorManager::ReAllocate(SectorNode& node, const size_t size) {
	const SIZE_T vectorSize = node.Sectors.size();

	const SIZE_T oldSize = vectorSize * FULL_SECTOR_SIZE;
	const SIZE_T alignedSize = AlignSize(size);
	const UINT64 wantedSectorCount = GetSectorAmount(alignedSize);

	if (vectorSize < wantedSectorCount) {
		// Allocate
		const SIZE_T sectorDifference = wantedSectorCount - vectorSize;
		InterlockedExchangeAdd(&this->allocatedSectors, sectorDifference);

		try {
			node.Sectors.resize(wantedSectorCount);
		} catch (std::bad_alloc&) {
			this->ReAllocate(node, oldSize);
			return false;
		}

		for (UINT64 i = vectorSize; i < wantedSectorCount; i++) {
			// VirtualAlloc instead of HeapAlloc — VirtualFree(MEM_RELEASE) is instant and does not require paging in evicted pages, unlike HeapFree.
			Sector* allocPtr = static_cast<Sector*>(VirtualAlloc(nullptr, sizeof(Sector), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
			if (allocPtr == nullptr) {
				this->ReAllocate(node, oldSize);
				return false;
			}

			node.Sectors[i] = allocPtr;
		}
	} else if (vectorSize > wantedSectorCount) {
		// Deallocate
		for (UINT64 i = wantedSectorCount; i < vectorSize; i++) {
			VirtualFree(node.Sectors[i], 0, MEM_RELEASE);
		}

		node.Sectors.resize(wantedSectorCount);
		const UINT64 sectorDifference = vectorSize - wantedSectorCount;
		InterlockedExchangeSubtract(&this->allocatedSectors, sectorDifference);
	}

	return true;
}

bool SectorManager::Free(SectorNode& node) {
	return ReAllocate(node, 0);
}

bool SectorManager::IsFullyEmpty() {
	return this->GetAllocatedSectors() == 0;
}

UINT64 SectorManager::GetAllocatedSectors() {
	return InterlockedExchangeAdd(&this->allocatedSectors, 0ULL);
}

template <bool IsReading>
bool SectorManager::ReadWrite(SectorNode& node, void* buffer, const size_t size, const size_t offset) {
	if (size == 0) {
		return true;
	}

	const SIZE_T sectorCount = node.Sectors.size();

	const SIZE_T downAlignedOffset = AlignSize(offset, FALSE);
	const UINT64 offsetSectorBegin = GetSectorAmount(downAlignedOffset);
	const SIZE_T offsetOffset = offset - downAlignedOffset;

	UINT64 sectorEnd = offsetSectorBegin + GetSectorAmount(AlignSize(size)) - 1;
	if (offsetOffset > 0 && sectorCount > sectorEnd + 1) {
		sectorEnd++; // Needs to read one more sector to account for the bytes from the first sector to receive the full length
	}

	if (offsetSectorBegin >= sectorCount || sectorEnd >= sectorCount || offsetOffset > FULL_SECTOR_SIZE) {
		return false;
	}

	SIZE_T byteAmount = min(size, FULL_SECTOR_SIZE - offsetOffset);
	if constexpr (IsReading) {
		memcpy(buffer, node.Sectors[offsetSectorBegin]->Bytes + offsetOffset, byteAmount);
	} else {
		memcpy(node.Sectors[offsetSectorBegin]->Bytes + offsetOffset, buffer, byteAmount);
	}

	for (UINT64 i = offsetSectorBegin + 1; i <= sectorEnd; i++) {
		const SIZE_T copyNow = min(FULL_SECTOR_SIZE, size - byteAmount);

		if constexpr (IsReading) {
			memcpy((PVOID)((ULONG_PTR)buffer + byteAmount), node.Sectors[i]->Bytes, copyNow);
		} else {
			memcpy(node.Sectors[i]->Bytes, (PVOID)((ULONG_PTR)buffer + byteAmount), copyNow);
		}

		byteAmount += copyNow;
	}

	return true;
}

// Explicitly generate the template functions
template bool SectorManager::ReadWrite<true>(SectorNode& node, void* buffer, const size_t size, const size_t offset);
template bool SectorManager::ReadWrite<false>(SectorNode& node, void* buffer, const size_t size, const size_t offset);


SectorNode::~SectorNode() {
	SectorManager& sectorManager = MEMFS_SINGLETON->GetSectorManager();
	sectorManager.Free(*this);
}

SectorNode::SectorNode(SectorNode&& other) noexcept : Sectors(std::move(other.Sectors)) {}

SectorNode& SectorNode::operator=(SectorNode&& other) noexcept {
	MEMFS_SINGLETON->GetSectorManager().Free(*this);

	this->Sectors = std::move(other.Sectors);
	return *this;
}

size_t SectorNode::ApproximateSize() const {
	return this->Sectors.size() * (sizeof(Sector) + sizeof(Sector*));
}
