#include <cassert>

#include "memfs-interface.h"
#include "utils.h"

namespace Memfs::Interface {
	static VOID Cleanup(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, PWSTR fileName, ULONG flags) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		std::shared_ptr<FileNode> mainFileNodeShared;
		FileNode* mainFileNode;

		if (!fileNode->IsMainNode()) {
			mainFileNodeShared = fileNode->GetMainNode().lock();
			mainFileNode = mainFileNodeShared.get();
		} else {
			mainFileNode = fileNode;
		}

		assert(0 != flags); /* FSP_FSCTL_VOLUME_PARAMS::PostCleanupWhenModifiedOnly ensures this */

		if (flags & FspCleanupSetArchiveBit) {
			if (0 == (mainFileNode->fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
				mainFileNode->fileInfo.FileAttributes |= FILE_ATTRIBUTE_ARCHIVE;
			}
		}

		if (flags & (FspCleanupSetLastAccessTime | FspCleanupSetLastWriteTime | FspCleanupSetChangeTime)) {
			const UINT64 systemTime = Utils::GetSystemTime();

			if (flags & FspCleanupSetLastAccessTime) {
				mainFileNode->fileInfo.LastAccessTime = systemTime;
			}
			if (flags & FspCleanupSetLastWriteTime) {
				mainFileNode->fileInfo.LastWriteTime = systemTime;
			}
			if (flags & FspCleanupSetChangeTime) {
				mainFileNode->fileInfo.ChangeTime = systemTime;
			}
		}

		if (flags & FspCleanupSetAllocationSize) {
			const UINT64 allocationUnit = MEMFS_SECTOR_SIZE * MEMFS_SECTORS_PER_ALLOCATION_UNIT;
			const UINT64 allocationSize = (fileNode->fileInfo.FileSize + allocationUnit - 1) /
				allocationUnit * allocationUnit;

			CompatSetFileSizeInternal(fileSystem, fileNode, allocationSize, true);
		}

		if ((flags & FspCleanupDelete) && !memfs->HasChild(*fileNode)) {
			for (FileNode* namedStream : memfs->EnumerateNamedStreams(*fileNode, false)) {
				memfs->RemoveNode(*namedStream);
			}

			memfs->RemoveNode(*fileNode);
		}
	}
}
