#include <cassert>

#include "exceptions.h"
#include "memfs-interface.h"
#include "utils.h"

namespace Memfs::Interface {
	NTSTATUS Create(FSP_FILE_SYSTEM* fileSystem,
	                       PWSTR fileName0, UINT32 createOptions, UINT32 grantedAccess,
	                       UINT32 fileAttributes, PSECURITY_DESCRIPTOR securityDescriptor, UINT64 allocationSize,
	                       PVOID extraBuffer, ULONG extraLength, BOOLEAN extraBufferIsReparsePoint,
	                       PVOID* pFileNode, FSP_FSCTL_FILE_INFO* fileInfo) {
		MemFs* memfs = GetMemFs(fileSystem);

		if (MEMFS_MAX_PATH <= wcslen(fileName0)) {
			return STATUS_OBJECT_NAME_INVALID;
		}

		if (createOptions & FILE_DIRECTORY_FILE) {
			allocationSize = 0;
		}

		if (memfs->FindFile(fileName0).has_value()) {
			return STATUS_OBJECT_NAME_COLLISION;
		}

		const auto [parentResult, parentNodeOpt] = memfs->FindParent(fileName0);
		NTSTATUS result = parentResult;
		if (!parentNodeOpt.has_value()) {
			return result;
		}
		const FileNode& parentNode = parentNodeOpt.value();

		// memefs: No more file count limit
		// if (MemfsFileNodeMapCount(Memfs->FileNodeMap) >= Memfs->MaxFileNodes)
		//    return STATUS_CANNOT_MAKE;

		if (allocationSize > memfs->CalculateAvailableTotalSize()) {
			return STATUS_DISK_FULL;
		}

		std::wstring fileName;
		if (memfs->IsCaseInsensitive()) {
			const Utils::SuffixView pathView = Utils::PathSuffix(fileName0);
			assert(0 == Utils::FileNameCompare(pathView.RemainPrefix.data(), pathView.RemainPrefix.length(), parentNode.fileName.c_str(), parentNode.fileName.length(), true));

			const size_t remainLength = parentNode.fileName.length();
			const size_t bSlashLength = 1 < remainLength;
			const size_t suffixLength = pathView.Suffix.length();
			if (MEMFS_MAX_PATH <= remainLength + bSlashLength + suffixLength) {
				return STATUS_OBJECT_NAME_INVALID;
			}

			fileName = parentNode.fileName + (bSlashLength ? L"\\" : L"") + std::wstring(pathView.Suffix);
		} else {
			fileName = fileName0;
		}


		try {
			FileNode fileNode(fileName);

			const auto mainNode = memfs->FindMainFromStream(fileName);
			if (mainNode.has_value()) {
				// TODO: This might not work and can cause dangling pointers
				fileNode.SetMainNode(std::shared_ptr<FileNode>(&mainNode.value().get()));
			}

			fileNode.fileInfo.FileAttributes = (fileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? fileAttributes : fileAttributes | FILE_ATTRIBUTE_ARCHIVE;

			if (securityDescriptor != nullptr) {
				try {
					const size_t securityDescriptorLength = GetSecurityDescriptorLength(securityDescriptor);
					fileNode.fileSecurity = DynamicStruct<SECURITY_DESCRIPTOR>(securityDescriptorLength);

					memcpy_s(fileNode.fileSecurity.Struct(), fileNode.fileSecurity.ByteSize(), securityDescriptor, securityDescriptorLength);
				} catch (...) {
					memfs->RemoveNode(fileNode);
					return STATUS_INSUFFICIENT_RESOURCES;
				}
			}

			if (nullptr != extraBuffer) {
				if (!extraBufferIsReparsePoint) {
					result = FspFileSystemEnumerateEa(fileSystem, CompatFspFileNodeSetEa, &fileNode,
					                                  (PFILE_FULL_EA_INFORMATION)extraBuffer, extraLength);

					if (!NT_SUCCESS(result)) {
						memfs->RemoveNode(fileNode);
						return result;
					}
				}

				if (extraBufferIsReparsePoint) {
					try {
						fileNode.reparseData = DynamicStruct<byte>(extraLength);

						fileNode.fileInfo.FileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
						fileNode.fileInfo.ReparseTag = *(PULONG)extraBuffer;

						memcpy_s(fileNode.reparseData.Struct(), fileNode.reparseData.ByteSize(), extraBuffer, extraLength);
					} catch (...) {
						memfs->RemoveNode(fileNode);
						return STATUS_INSUFFICIENT_RESOURCES;
					}
				}
			}

			fileNode.fileInfo.AllocationSize = allocationSize;
			if (0 != fileNode.fileInfo.AllocationSize) {
				if (!memfs->GetSectorManager().ReAllocate(fileNode.GetSectorNode(), fileNode.fileInfo.AllocationSize)) {
					memfs->RemoveNode(fileNode);
					return STATUS_INSUFFICIENT_RESOURCES;
				}
			}

			auto [insertResult, newFileNode] = memfs->InsertNode(std::move(fileNode));
			result = insertResult;

			if (!NT_SUCCESS(result)) {
				memfs->RemoveNode(fileNode);
				if (NT_SUCCESS(result)) {
					result = STATUS_OBJECT_NAME_COLLISION; /* should not happen! */
				}
				return result;
			}

			newFileNode.Reference();
			*pFileNode = &newFileNode;
			newFileNode.CopyFileInfo(fileInfo);

			if (memfs->IsCaseInsensitive()) {
				FSP_FSCTL_OPEN_FILE_INFO* openFileInfo = FspFileSystemGetOpenFileInfo(fileInfo);

				wcscpy_s(openFileInfo->NormalizedName, openFileInfo->NormalizedNameSize / sizeof(WCHAR),
				         newFileNode.fileName.c_str());
				openFileInfo->NormalizedNameSize = (UINT16)(newFileNode.fileName.length() * sizeof(WCHAR));
			}

			return STATUS_SUCCESS;
		} catch (FileNameTooLongException& ex) {
			return STATUS_OBJECT_NAME_INVALID;
		}
	}

	NTSTATUS Open(FSP_FILE_SYSTEM* fileSystem,
	                     PWSTR fileName, UINT32 createOptions, UINT32 grantedAccess,
	                     PVOID* pFileNode, FSP_FSCTL_FILE_INFO* fileInfo) {
		MemFs* memfs = GetMemFs(fileSystem);
		NTSTATUS result;

		if (MEMFS_MAX_PATH <= wcslen(fileName)) {
			return STATUS_OBJECT_NAME_INVALID;
		}

		const auto fileNodeOpt = memfs->FindFile(fileName);
		if (!fileNodeOpt.has_value()) {
			result = STATUS_OBJECT_NAME_NOT_FOUND;
			const auto [parentStatus, _] = memfs->FindParent(fileName);
			return parentStatus ? parentStatus : result;
		}
		FileNode& fileNode = fileNodeOpt.value();

		/* if the OP specified no EA's check the need EA count, but only if accessing main stream */
		if (0 != (createOptions & FILE_NO_EA_KNOWLEDGE) && (fileNode.IsMainNode())) {
			if (fileNode.NeedsEa()) {
				result = STATUS_ACCESS_DENIED;
				return result;
			}
		}

		fileNode.Reference();
		*pFileNode = &fileNode;
		fileNode.CopyFileInfo(fileInfo);

		if (memfs->IsCaseInsensitive()) {
			FSP_FSCTL_OPEN_FILE_INFO* openFileInfo = FspFileSystemGetOpenFileInfo(fileInfo);

			wcscpy_s(openFileInfo->NormalizedName, openFileInfo->NormalizedNameSize / sizeof(WCHAR),
			         fileNode.fileName.c_str());
			openFileInfo->NormalizedNameSize = (UINT16)(fileNode.fileName.length() * sizeof(WCHAR));
		}

		return STATUS_SUCCESS;
	}

	NTSTATUS Overwrite(FSP_FILE_SYSTEM* fileSystem,
	                          PVOID fileNode0, UINT32 fileAttributes, BOOLEAN replaceFileAttributes, UINT64 allocationSize,
	                          PFILE_FULL_EA_INFORMATION ea, ULONG eaLength, FSP_FSCTL_FILE_INFO* fileInfo) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		NTSTATUS result;

		for (const auto& namedStream : memfs->EnumerateNamedStreams(*fileNode, true)) {
			const volatile long refCount = namedStream->GetReferenceCount(true);
			MemoryBarrier(); // Remove this barrier in the future
			if (2 >= refCount) {
				memfs->RemoveNode(*namedStream);
			}
		}

		fileNode->DeleteEaMap();
		if (ea != nullptr) {
			result = FspFileSystemEnumerateEa(fileSystem, CompatFspFileNodeSetEa, fileNode, ea, eaLength);
			if (!NT_SUCCESS(result)) {
				return result;
			}
		}

		result = CompatSetFileSizeInternal(fileSystem, fileNode, allocationSize, true);
		if (!NT_SUCCESS(result)) {
			return result;
		}

		if (replaceFileAttributes) {
			fileNode->fileInfo.FileAttributes = fileAttributes | FILE_ATTRIBUTE_ARCHIVE;
		} else {
			fileNode->fileInfo.FileAttributes |= fileAttributes | FILE_ATTRIBUTE_ARCHIVE;
		}

		fileNode->fileInfo.FileSize = 0;
		fileNode->fileInfo.LastAccessTime =
			fileNode->fileInfo.LastWriteTime =
			fileNode->fileInfo.ChangeTime = Utils::GetSystemTime();

		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}

	VOID Close(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0) {
		FileNode* fileNode = GetFileNode(fileNode0);
		fileNode->Dereference();
	}
}
