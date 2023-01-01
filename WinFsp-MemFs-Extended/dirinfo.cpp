#include "memfs-interface.h"
#include "utils.h"

namespace Memfs::Interface {
	NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* fileSystem,
		PVOID fileNode0, PWSTR pattern, PWSTR marker,
		PVOID buffer, ULONG length, PULONG pBytesTransferred) {
		assert(nullptr == pattern);

		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* fileNode = GetFileNode(fileNode0);

		if (L'\0' != fileNode->fileName[1]) {
			/* if this is not the root directory, add the dot entries */

			auto const [parentResult, parent] = memfs->FindParent(fileNode->fileName);
			if (!parent.has_value()) {
				return parentResult;
			}
			FileNode& parentNode = parent.value();

			if (marker == nullptr) {
				if (!CompatAddDirInfo(fileNode, L".", buffer, length, pBytesTransferred)) {
					return STATUS_SUCCESS;
				}
			}

			if (marker == nullptr || (L'.' == marker[0] && L'\0' == marker[1])) {
				if (!CompatAddDirInfo(&parentNode, L"..", buffer, length, pBytesTransferred)) {
					return STATUS_SUCCESS;
				}
				marker = nullptr;
			}
		}

		const std::wstring_view markerView = marker ? marker : L"";
		const std::refoptional<const std::wstring_view> markerOpt = (marker == nullptr) ? std::nullopt : std::make_optional(markerView);

		for (const auto& child : memfs->EnumerateDirChildren(*fileNode, markerOpt)) {
			if (!CompatAddDirInfo(child, nullptr, buffer, length, pBytesTransferred)) {
				return STATUS_SUCCESS; // Without end
			}
		}

		FspFileSystemAddDirInfo(nullptr, buffer, length, pBytesTransferred); // List end
		return STATUS_SUCCESS;
	}

	NTSTATUS GetDirInfoByName(FSP_FILE_SYSTEM* fileSystem,
		PVOID parentNode0, PWSTR fileName, FSP_FSCTL_DIR_INFO* dirInfo) {
		MemFs* memfs = GetMemFs(fileSystem);
		FileNode* parentNode = GetFileNode(parentNode0);

		const size_t fileNameLength = wcslen(fileName);
		const size_t parentLength = parentNode->fileName.length();
		if (MEMFS_MAX_PATH <= parentLength + fileNameLength + 1) {
			return STATUS_OBJECT_NAME_NOT_FOUND; // STATUS_OBJECT_NAME_INVALID?
		}

		const bool needsSlash = 1 < parentLength;
		std::wstring fileNameStr = parentNode->fileName + (needsSlash ? L"\\" : L"") + fileName;

		const auto fileNodeOpt = memfs->FindFile(fileNameStr);
		if (!fileNodeOpt.has_value()) {
			return STATUS_OBJECT_NAME_NOT_FOUND;
		}
		const FileNode& fileNode = fileNodeOpt.value();

		const Utils::SuffixView suffixView = Utils::PathSuffix(fileNode.fileName);
		fileNameStr = suffixView.Suffix;

		dirInfo->Size = (UINT16)(sizeof(FSP_FSCTL_DIR_INFO) + fileNameStr.length() * sizeof(WCHAR));
		dirInfo->FileInfo = fileNode.fileInfo;
		memcpy(dirInfo->FileNameBuf, fileNameStr.c_str(), dirInfo->Size - sizeof(FSP_FSCTL_DIR_INFO));

		return STATUS_SUCCESS;
	}
}
