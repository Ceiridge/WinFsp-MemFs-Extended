#include "memfs-interface.h"

namespace Memfs::Interface {
	NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, FSP_FSCTL_FILE_INFO* fileInfo) {
		const FileNode* fileNode = GetFileNode(fileNode0);
		std::shared_lock lock(fileNode->nodeMutex);
		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}

	NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM* fileSystem,
	                      PVOID fileNode0, UINT32 fileAttributes,
	                      UINT64 creationTime, UINT64 lastAccessTime, UINT64 lastWriteTime, UINT64 changeTime, FSP_FSCTL_FILE_INFO* fileInfo) {
		FileNode* fileNode = GetFileNode(fileNode0);

		if (!fileNode->IsMainNode()) {
			fileNode = fileNode->GetMainNode();
		}

		std::unique_lock lock(fileNode->nodeMutex);

		if (INVALID_FILE_ATTRIBUTES != fileAttributes) {
			fileNode->fileInfo.FileAttributes = fileAttributes;
		}
		if (0 != creationTime) {
			fileNode->fileInfo.CreationTime = creationTime;
		}
		if (0 != lastAccessTime) {
			fileNode->fileInfo.LastAccessTime = lastAccessTime;
		}
		if (0 != lastWriteTime) {
			fileNode->fileInfo.LastWriteTime = lastWriteTime;
		}
		if (0 != changeTime) {
			fileNode->fileInfo.ChangeTime = changeTime;
		}

		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}

	NTSTATUS SetFileSize(FSP_FILE_SYSTEM* fileSystem,
	                     PVOID fileNode0, UINT64 newSize, BOOLEAN setAllocationSize,
	                     FSP_FSCTL_FILE_INFO* fileInfo) {
		FileNode* fileNode = GetFileNode(fileNode0);

		std::unique_lock lock(fileNode->nodeMutex);
		const NTSTATUS result = CompatSetFileSizeInternal(fileSystem, fileNode, newSize, setAllocationSize);
		if (!NT_SUCCESS(result)) {
			return result;
		}

		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}

	NTSTATUS CanDelete(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, PWSTR fileName) {
		MemFs* memfs = GetMemFs(fileSystem);
		const FileNode* fileNode = GetFileNode(fileNode0);

		if (memfs->HasChild(*fileNode)) {
			return STATUS_DIRECTORY_NOT_EMPTY;
		}

		return STATUS_SUCCESS;
	}

	NTSTATUS Rename(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0,
	                PWSTR fileName, PWSTR newFileName, BOOLEAN replaceIfExists) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		const auto newFileNodeOpt = memfs->FindFile(newFileName);
		if (newFileNodeOpt.has_value() && fileNode != &newFileNodeOpt.value().get()) {
			const FileNode& newFileNode = newFileNodeOpt.value();
			if (!replaceIfExists) {
				return STATUS_OBJECT_NAME_COLLISION;
			}

			if (newFileNode.fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				return STATUS_ACCESS_DENIED;
			}
		}

		// Old length
		const ULONG fileNameLen = (ULONG)fileNode->fileName.length();
		const ULONG newFileNameLen = (ULONG)wcslen(newFileName);

		// Check for max path
		const auto descendants = memfs->EnumerateDescendants(*fileNode, true);
		for (const auto& descendant : descendants) {
			if (MEMFS_MAX_PATH <= descendant->fileName.length() - fileNameLen + newFileNameLen) {
				// C1: EnumerateDescendants took a reference on every node — release them all
				// before bailing out, otherwise they can never be freed again.
				for (const auto& toRelease : descendants) {
					toRelease->Dereference();
				}
				return STATUS_OBJECT_NAME_INVALID;
			}
		}

		if (newFileNodeOpt.has_value()) {
			FileNode& newFileNode = newFileNodeOpt.value();

			newFileNode.Reference();
			memfs->RemoveNode(newFileNode);
			newFileNode.Dereference();
		}

		// Rename descendants
		for (const auto& descendant : descendants) {
			memfs->RemoveNode(*descendant, false);

			{
				std::unique_lock descLock(descendant->nodeMutex);
				std::wstring oldFileNameDesc = descendant->fileName;
				descendant->fileName = newFileName + oldFileNameDesc.substr(fileNameLen);
			}

			const auto [result,_] = memfs->InsertNode(descendant);
			if (STATUS_INSUFFICIENT_RESOURCES == result) {
				FspDebugLog(__FUNCTION__ ": cannot insert into FileNodeMap; aborting\n");
				abort();
			}
			if (!NT_SUCCESS(result)) {
				// A3: the new name is taken by a node that is not ours. Leaving this descendant
				// unlinked is the same outcome as before, but no longer silent.
				FspDebugLog(__FUNCTION__ ": name collision while renaming a descendant\n");
			}

			descendant->Dereference(); // Remove the reference from the descendant enumeration above
		}

		return STATUS_SUCCESS;
	}
}
