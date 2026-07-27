#include <cassert>

#include "memfs-interface.h"
#include "utils.h"

namespace Memfs::Interface {
	VOID Cleanup(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, PWSTR fileName, ULONG flags) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		FileNode* mainFileNode;

		if (!fileNode->IsMainNode()) {
			mainFileNode = fileNode->GetMainNode();
		} else {
			mainFileNode = fileNode;
		}

		assert(0 != flags); /* FSP_FSCTL_VOLUME_PARAMS::PostCleanupWhenModifiedOnly ensures this */

		// Lock both nodes if this is a named stream (fileNode != mainFileNode) so that
		// allocation changes on the stream and metadata changes on the main file are atomic.
		// std::scoped_lock handles deadlock avoidance automatically.
		const auto doCleanup = [&]() {
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
		};

		if (fileNode != mainFileNode) {
			std::scoped_lock lock(fileNode->nodeMutex, mainFileNode->nodeMutex);
			doCleanup();
		} else {
			std::scoped_lock lock(fileNode->nodeMutex);
			doCleanup();
		}

		if (flags & FspCleanupDelete) {
			// A4: HasChild/EnumerateNamedStreams/RemoveNode all work by name. If the name
			// meanwhile belongs to a different node, they would operate on that node's children
			// and streams instead of on this one's.
			const auto currentOpt = memfs->FindFile(fileNode->fileName);
			const bool stillLinked = currentOpt.has_value() && &currentOpt.value().get() == fileNode;

			if (!stillLinked) {
				memfs->diagCleanupDeleteUnlinked.fetch_add(1, std::memory_order_relaxed);
				FspDebugLog(__FUNCTION__ ": cleanup-delete on a node that no longer owns its name\n");
			} else if (!memfs->HasChild(*fileNode)) {
				for (const auto& namedStream : memfs->EnumerateNamedStreams(*fileNode, false)) {
					memfs->RemoveNode(*namedStream);
				}

				memfs->RemoveNode(*fileNode);
			}
		}
	}

	NTSTATUS GetStreamInfo(FSP_FILE_SYSTEM* fileSystem,
	                              PVOID fileNode0, PVOID buffer, ULONG length, PULONG pBytesTransferred) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		if (!fileNode->IsMainNode()) {
			fileNode = fileNode->GetMainNode();
		}

		{
			std::shared_lock lock(fileNode->nodeMutex);
			if (0 == (fileNode->fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
				!CompatAddStreamInfo(fileNode, buffer, length, pBytesTransferred))
				return STATUS_SUCCESS;
		}

		std::vector<FileNode*> namedStreams;
		{
			std::shared_lock mapLock(memfs->GetFileMapMutex());
			namedStreams = memfs->EnumerateNamedStreams(*fileNode, false);
		}
		for (const auto& namedStream : namedStreams) {
			std::shared_lock nsLock(namedStream->nodeMutex);
			if (!CompatAddStreamInfo(namedStream, buffer, length, pBytesTransferred)) {
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
