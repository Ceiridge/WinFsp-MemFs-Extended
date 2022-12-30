#include <Windows.h>

#include "memfs.h"

using namespace Memfs;

// memefs: Get the all file sizes and the node map size
UINT64 MemFs::GetUsedTotalSize() {
	// TODO: Update this to the new code
	const ULONG nodeMapSize = (ULONG)this->fileNodeMap->size() * (sizeof(PWSTR) * MEMFS_MAX_PATH + sizeof(MEMFS_FILE_NODE) + sizeof(std::mutex) + sizeof(std::mutex*));
	// EA node map is ignored, because it is insignificant

	// TODO: Update
	const SIZE_T sectorSizes = this->allocatedSectors * (sizeof(MEMEFS_SECTOR) + sizeof(MEMEFS_SECTOR*));
	return nodeMapSize + sectorSizes;
}


// memefs: This is required to update the maximum total size according to the available RAM that is left
UINT64 MemFs::CalculateMaxTotalSize() {
	if (this->maxFsSize != 0) {
		return this->maxFsSize;
	}

	const UINT64 usedSize = this->GetUsedTotalSize();
	const UINT64 currentTicks = GetTickCount64();

	// Limit calls to GlobalMemoryStatusEx with a 100ms cooldown to improve performance
	if (currentTicks - this->lastCacheTime < 100) {
		return this->cachedMaxFsSize + usedSize;
	}

	MEMORYSTATUSEX memoryStatus{};
	memoryStatus.dwLength = sizeof(MEMORYSTATUSEX);
	if (!GlobalMemoryStatusEx(&memoryStatus)) {
		return 0;
	}

	// TODO: Check whether it should be limited by physical or virtual memory
	const UINT64 availMemorySize = min(memoryStatus.ullAvailPhys, memoryStatus.ullAvailVirtual);

	this->cachedMaxFsSize = availMemorySize;
	this->lastCacheTime = currentTicks;

	return availMemorySize + usedSize;
}

UINT64 MemFs::CalculateAvailableTotalSize() {
	const UINT64 totalSize = this->CalculateMaxTotalSize();
	const UINT64 usedSize = this->GetUsedTotalSize();

	if (usedSize >= totalSize) {
		return 0;
	}

	return totalSize - usedSize;
}
