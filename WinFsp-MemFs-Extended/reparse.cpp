#include "memfs-interface.h"

namespace Memfs::Interface {
	static NTSTATUS ResolveReparsePoints(FSP_FILE_SYSTEM* fileSystem,
	                                     PWSTR fileName, UINT32 reparsePointIndex, BOOLEAN resolveLastPathComponent,
	                                     PIO_STATUS_BLOCK pIoStatus, PVOID buffer, PSIZE_T pSize) {
		return FspFileSystemResolveReparsePoints(fileSystem, CompatGetReparsePointByName, nullptr,
		                                         fileName, reparsePointIndex, resolveLastPathComponent,
		                                         pIoStatus, buffer, pSize);
	}

	static NTSTATUS GetReparsePoint(FSP_FILE_SYSTEM* fileSystem,
	                                PVOID fileNode0,
	                                PWSTR fileName, PVOID buffer, PSIZE_T pSize) {
		FileNode* fileNode = GetFileNode(fileNode0);
		std::shared_ptr<FileNode> mainFileNodeShared;

		if (!fileNode->IsMainNode()) {
			mainFileNodeShared = fileNode->GetMainNode().lock();
			fileNode = mainFileNodeShared.get();
		}

		if (0 == (fileNode->fileInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
			return STATUS_NOT_A_REPARSE_POINT;
		}

		if (fileNode->reparseData.WantedByteSize() > *pSize) {
			return STATUS_BUFFER_TOO_SMALL;
		}

		*pSize = fileNode->reparseData.WantedByteSize();
		memcpy_s(buffer, *pSize, fileNode->reparseData.Struct(), fileNode->reparseData.WantedByteSize());

		return STATUS_SUCCESS;
	}

	static NTSTATUS SetReparsePoint(FSP_FILE_SYSTEM* fileSystem,
	                                PVOID fileNode0,
	                                PWSTR fileName, PVOID buffer, SIZE_T size) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);
		std::shared_ptr<FileNode> mainFileNodeShared;

		if (!fileNode->IsMainNode()) {
			mainFileNodeShared = fileNode->GetMainNode().lock();
			fileNode = mainFileNodeShared.get();
		}

		if (memfs->HasChild(*fileNode)) {
			return STATUS_DIRECTORY_NOT_EMPTY;
		}

		if (fileNode->reparseData.HoldsStruct()) {
			const NTSTATUS result = FspFileSystemCanReplaceReparsePoint(
				fileNode->reparseData.Struct(), fileNode->reparseData.WantedByteSize(),
				buffer, size);

			if (!NT_SUCCESS(result)) {
				return result;
			}
		}

		try {
			fileNode->reparseData = DynamicStruct<byte>(size);
		} catch (...) {
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		fileNode->fileInfo.FileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
		fileNode->fileInfo.ReparseTag = *(PULONG)buffer;
		/* the first field in a reparse buffer is the reparse tag */

		memcpy_s(fileNode->reparseData.Struct(), fileNode->reparseData.ByteSize(), buffer, size);

		return STATUS_SUCCESS;
	}

	static NTSTATUS DeleteReparsePoint(FSP_FILE_SYSTEM* fileSystem,
	                                   PVOID fileNode0,
	                                   PWSTR fileName, PVOID buffer, SIZE_T size) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);
		std::shared_ptr<FileNode> mainFileNodeShared;

		if (!fileNode->IsMainNode()) {
			mainFileNodeShared = fileNode->GetMainNode().lock();
			fileNode = mainFileNodeShared.get();
		}

		if (fileNode->reparseData.HoldsStruct()) {
			const NTSTATUS result = FspFileSystemCanReplaceReparsePoint(
				fileNode->reparseData.Struct(), fileNode->reparseData.WantedByteSize(),
				buffer, size);

			if (!NT_SUCCESS(result)) {
				return result;
			}
		} else {
			return STATUS_NOT_A_REPARSE_POINT;
		}

		fileNode->reparseData = DynamicStruct<byte>(); // Throw away old dynamic struct

		fileNode->fileInfo.FileAttributes &= ~FILE_ATTRIBUTE_REPARSE_POINT;
		fileNode->fileInfo.ReparseTag = 0;
		return STATUS_SUCCESS;
	}
}
