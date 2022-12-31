#include <cassert>

#include "exceptions.h"
#include "memfs-interface.h"
#include "nodes.h"
#include "utils.h"

namespace Memfs {
	static NTSTATUS CompatFspFileNodeSetEa(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode, PFILE_FULL_EA_INFORMATION ea) {
		FileNode* node = static_cast<FileNode*>(fileNode);

		try {
			node->SetEa(ea);
		} catch (CreateException& ex) {
			return ex.Which();
		}

		return STATUS_SUCCESS;
	}

	static NTSTATUS CompatSetFileSizeInternal(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, UINT64 newSize, BOOLEAN setAllocationSize) {
		MemFs* memfs = Interface::GetMemFs(fileSystem);
		FileNode* fileNode = Interface::GetFileNode(fileNode0);

		if (setAllocationSize) {
			if (fileNode->fileInfo.AllocationSize != newSize) {
				// memefs: Sector Reallocate
				const SIZE_T oldSize = fileNode->GetSectorNode().ApproximateSize();
				if (newSize - oldSize + memfs->GetUsedTotalSize() > memfs->CalculateMaxTotalSize()) {
					return STATUS_DISK_FULL;
				}


				if (memfs->GetSectorManager().ReAllocate(fileNode->GetSectorNode(), newSize)) {
					return STATUS_INSUFFICIENT_RESOURCES;
				}

				fileNode->fileInfo.AllocationSize = newSize;
				if (fileNode->fileInfo.FileSize > newSize) {
					fileNode->fileInfo.FileSize = newSize;
				}
			}
		} else {
			if (fileNode->fileInfo.FileSize != newSize) {
				if (fileNode->fileInfo.AllocationSize < newSize) {
					const UINT64 allocationUnit = MEMFS_SECTOR_SIZE * MEMFS_SECTORS_PER_ALLOCATION_UNIT;
					const UINT64 allocationSize = (newSize + allocationUnit - 1) / allocationUnit * allocationUnit;

					const NTSTATUS result = CompatSetFileSizeInternal(fileSystem, fileNode, allocationSize, true);
					if (!NT_SUCCESS(result)) {
						return result;
					}
				}

				// memefs: No null-initialization?
				// if (fileNode->fileInfo.FileSize < NewSize)
				//    memset((PUINT8)FileNode->FileData + fileNode->fileInfo.FileSize, 0,
				//        (size_t)(NewSize - fileNode->fileInfo.FileSize));
				fileNode->fileInfo.FileSize = newSize;
			}
		}

		return STATUS_SUCCESS;
	}

	static NTSTATUS CompatGetReparsePointByName(FSP_FILE_SYSTEM* fileSystem, PVOID context, PWSTR fileName, BOOLEAN isDirectory, PVOID buffer, PSIZE_T pSize) {
		MemFs* memfs = Interface::GetMemFs(fileSystem);

		/* GetReparsePointByName will never receive a named stream */
		assert(nullptr == wcschr(fileName, L':'));

		const auto fileNodeOpt = memfs->FindFile(fileName);
		if (!fileNodeOpt.has_value())
			return STATUS_OBJECT_NAME_NOT_FOUND;
		FileNode& fileNode = fileNodeOpt.value();

		if (0 == (fileNode.fileInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
			return STATUS_NOT_A_REPARSE_POINT;

		if (buffer != nullptr) {
			if (fileNode.reparseData.WantedByteSize() > *pSize)
				return STATUS_BUFFER_TOO_SMALL;

			*pSize = fileNode.reparseData.WantedByteSize();
			memcpy_s(buffer, *pSize, fileNode.reparseData.Struct(), fileNode.reparseData.WantedByteSize());
		}

		return STATUS_SUCCESS;
	}

	static BOOLEAN CompatAddDirInfo(FileNode* fileNode, PCWSTR fileName, PVOID buffer, ULONG length, PULONG pBytesTransferred) {
		DynamicStruct<FSP_FSCTL_DIR_INFO> dirInfoBuf(sizeof(FSP_FSCTL_DIR_INFO) + fileNode->fileName.size() + 1);
		FSP_FSCTL_DIR_INFO* dirInfo = dirInfoBuf.Struct();

		std::wstring fileNameStr;
		if (fileName == nullptr) {
			const Utils::SuffixView suffixView = Utils::PathSuffix(fileNode->fileName);
			fileNameStr = suffixView.Suffix;
		} else {
			fileNameStr = fileName;
		}

		memset(dirInfo->Padding, 0, sizeof dirInfo->Padding);
		dirInfo->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + fileNameStr.length() * sizeof(WCHAR));
		dirInfo->FileInfo = fileNode->fileInfo;
		memcpy(dirInfo->FileNameBuf, fileNameStr.c_str(), dirInfo->Size - sizeof(FSP_FSCTL_DIR_INFO));

		return FspFileSystemAddDirInfo(dirInfo, buffer, length, pBytesTransferred);
	}
}
