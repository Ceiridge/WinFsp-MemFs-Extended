#include <cassert>

#include "memfs-interface.h"
#include "utils.h"

namespace Memfs::Interface {
	VOID Cleanup(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, PWSTR fileName, ULONG flags) {
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
			for (const auto& namedStream : memfs->EnumerateNamedStreams(*fileNode, false)) {
				memfs->RemoveNode(*namedStream);
			}

			memfs->RemoveNode(*fileNode);
		}
	}

	NTSTATUS GetStreamInfo(FSP_FILE_SYSTEM* fileSystem,
	                              PVOID fileNode0, PVOID buffer, ULONG length, PULONG pBytesTransferred) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);
		std::shared_ptr<FileNode> mainFileNodeShared;

		if (!fileNode->IsMainNode()) {
			mainFileNodeShared = fileNode->GetMainNode().lock();
			fileNode = mainFileNodeShared.get();
		}

		if (0 == (fileNode->fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
			!CompatAddStreamInfo(fileNode, buffer, length, pBytesTransferred))
			return STATUS_SUCCESS;

		// I don't think this makes references
		for (const auto& namedStream : memfs->EnumerateNamedStreams(*fileNode, false)) {
			if (!CompatAddStreamInfo(namedStream.get(), buffer, length, pBytesTransferred)) {
				return STATUS_SUCCESS; // Without end
			}
		}

		FspFileSystemAddStreamInfo(nullptr, buffer, length, pBytesTransferred); // List end
		return STATUS_SUCCESS;
	}

	NTSTATUS Control(FSP_FILE_SYSTEM* fileSystem,
	                        PVOID fileNode, UINT32 controlCode,
	                        PVOID inputBuffer, ULONG inputBufferLength,
	                        PVOID outputBuffer, ULONG outputBufferLength, PULONG pBytesTransferred) {
		// The original author found it extremely funny to add ROT13 "encryption" as an IOCTL feature... See below:

		/* MEMFS also supports encryption! See below :) */
		if (CTL_CODE(0x8000 + 'M', 'R', METHOD_BUFFERED, FILE_ANY_ACCESS) == controlCode) {
			if (outputBufferLength != inputBufferLength)
				return STATUS_INVALID_PARAMETER;

			for (PUINT8 P = (PUINT8)inputBuffer, Q = (PUINT8)outputBuffer, EndP = P + inputBufferLength;
			     EndP > P; P++, Q++) {
				if (('A' <= *P && *P <= 'M') || ('a' <= *P && *P <= 'm'))
					*Q = *P + 13;
				else if (('N' <= *P && *P <= 'Z') || ('n' <= *P && *P <= 'z'))
					*Q = *P - 13;
				else
					*Q = *P;
			}

			*pBytesTransferred = inputBufferLength;
			return STATUS_SUCCESS;
		}

		return STATUS_INVALID_DEVICE_REQUEST;
	}
}
