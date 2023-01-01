#include "globalincludes.h"
#include "sectors.h"

#include "memfs.h"

using namespace Memfs;

SectorManager::SectorManager() {
	this->heap = HeapCreate(0, 0, 0);

	if (this->heap == nullptr || this->heap == INVALID_HANDLE_VALUE) {
		throw std::runtime_error("Could not create heap");
	}
}

SectorManager::~SectorManager() {
	HeapDestroy(this->heap); // Ignore errors
}

SectorManager::SectorManager(SectorManager&& other) noexcept : heap(std::move(other.heap)), allocatedSectors(other.allocatedSectors) {
	other.heap = nullptr;
}

SectorManager& SectorManager::operator=(SectorManager&& other) noexcept {
	HeapDestroy(this->heap);

	this->heap = other.heap;
	other.heap = nullptr;
	this->allocatedSectors = other.allocatedSectors;

	return *this;
}

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


bool SectorManager::ReAllocate(SectorNode& node, const size_t size) {
	std::unique_lock writeLock(node.SectorsMutex);
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
			// Deallocate again to old size after failed allocation
			if (oldSize < vectorSize) {
				writeLock.unlock();
				this->ReAllocate(node, oldSize);
			}

			return false;
		}


		for (UINT64 i = vectorSize; i < wantedSectorCount; i++) {
			try {
				Sector* allocPtr = static_cast<Sector*>(HeapAlloc(this->heap, 0, sizeof(Sector)));
				if (allocPtr == nullptr) {
					throw std::bad_alloc();
				}

				node.Sectors[i] = allocPtr;
			} catch (std::bad_alloc&) {
				// Deallocate again to old size after failed allocation
				if (oldSize < vectorSize) {
					writeLock.unlock();
					this->ReAllocate(node, oldSize);
				}

				return false;
			}
		}
	} else if (vectorSize > wantedSectorCount) {
		// Deallocate
		for (UINT64 i = wantedSectorCount; i < vectorSize; i++) {
			HeapFree(this->heap, 0, node.Sectors[i]);
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

	std::shared_lock readLock(node.SectorsMutex);
	const SIZE_T sectorCount = node.Sectors.size();

	const SIZE_T downAlignedOffset = AlignSize(offset, FALSE);
	const UINT64 offsetSectorBegin = GetSectorAmount(downAlignedOffset);
	const SIZE_T offsetOffset = offset - downAlignedOffset;

	UINT64 sectorEnd = offsetSectorBegin + GetSectorAmount(AlignSize(size)) - 1;
	if (offsetOffset > 0 && sectorCount > sectorEnd + 1) {
		sectorEnd++; // Needs to read one more sector to account for the bytes from the first sector to receive the full length
	}

	if (offsetSectorBegin >= sectorCount || sectorEnd > sectorCount || offsetOffset > FULL_SECTOR_SIZE) {
		return false;
	}

	SIZE_T byteAmount = min(size, FULL_SECTOR_SIZE - offsetOffset);
	if (IsReading) {
		memcpy(buffer, node.Sectors[offsetSectorBegin]->Bytes + offsetOffset, byteAmount);
	} else {
		memcpy(node.Sectors[offsetSectorBegin]->Bytes + offsetOffset, buffer, byteAmount);
	}

	for (UINT64 i = offsetSectorBegin + 1; i <= sectorEnd; i++) {
		const SIZE_T copyNow = min(FULL_SECTOR_SIZE, size - byteAmount);

		if (IsReading) {
			memcpy((PVOID)((ULONG_PTR)buffer + byteAmount), node.Sectors[i]->Bytes, copyNow);
		} else {
			memcpy(node.Sectors[i]->Bytes, (PVOID)((ULONG_PTR)buffer + byteAmount), copyNow);
		}

		byteAmount += copyNow;
	}

	return true;
}

// Not actually used anywhere
void ForceTemplateFunctionGenerationHelper() {
	SectorNode& nothing = *static_cast<SectorNode*>(nullptr);
	SectorManager::ReadWrite<true>(nothing, nullptr, 0, 0);
	SectorManager::ReadWrite<false>(nothing, nullptr, 0, 0);
}

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
