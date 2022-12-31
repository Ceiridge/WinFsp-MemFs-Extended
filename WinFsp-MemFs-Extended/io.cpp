#include "memfs-interface.h"

namespace Memfs::Interface {
	NTSTATUS Read(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, PVOID buffer, UINT64 offset, ULONG length, PULONG pBytesTransferred) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		if (offset >= fileNode->fileInfo.FileSize) {
			return STATUS_END_OF_FILE;
		}

		UINT64 endOffset = offset + length;
		if (endOffset > fileNode->fileInfo.FileSize) {
			endOffset = fileNode->fileInfo.FileSize;
		}

		// memefs: Read from sector
		if (!memfs->GetSectorManager().ReadWrite<true>(fileNode->GetSectorNode(), buffer, endOffset - offset, offset)) {
			return STATUS_UNSUCCESSFUL;
		}

		*pBytesTransferred = (ULONG)(endOffset - offset);
		return STATUS_SUCCESS;
	}

	NTSTATUS Write(FSP_FILE_SYSTEM* fileSystem,
	                      PVOID fileNode0, PVOID buffer, UINT64 offset, ULONG length,
	                      BOOLEAN writeToEndOfFile, BOOLEAN constrainedIo,
	                      PULONG pBytesTransferred, FSP_FSCTL_FILE_INFO* fileInfo) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		UINT64 endOffset;
		NTSTATUS result;

		if (constrainedIo) {
			if (offset >= fileNode->fileInfo.FileSize) {
				return STATUS_SUCCESS;
			}
			endOffset = offset + length;
			if (endOffset > fileNode->fileInfo.FileSize) {
				endOffset = fileNode->fileInfo.FileSize;
			}
		} else {
			if (writeToEndOfFile) {
				offset = fileNode->fileInfo.FileSize;
			}
			endOffset = offset + length;
			if (endOffset > fileNode->fileInfo.FileSize) {
				result = CompatSetFileSizeInternal(fileSystem, fileNode, endOffset, false);
				if (!NT_SUCCESS(result)) {
					return result;
				}
			}
		}

		// memefs: Write to sector
		if (!memfs->GetSectorManager().ReadWrite<false>(fileNode->GetSectorNode(), buffer, endOffset - offset, offset)) {
			return STATUS_UNSUCCESSFUL;
		}

		*pBytesTransferred = (ULONG)(endOffset - offset);
		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}

	NTSTATUS Flush(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, FSP_FSCTL_FILE_INFO* fileInfo) {
		const FileNode* fileNode = GetFileNode(fileNode0);

		if (fileNode != nullptr) {
			fileNode->CopyFileInfo(fileInfo);
		}

		return STATUS_SUCCESS;
	}
}
