#pragma once

#include <map>
#include <memory>
#include <optional>
#include <winfsp/winfsp.h>
#include <string>

#include "sectors.h"
#include "comparisons.h"
#include "dynamicstruct.h"

namespace Memfs {
	using FileNodeEaMap = std::map<std::string, DynamicStruct<FILE_FULL_EA_INFORMATION>, Utils::EaLess>;

	class FileNode {
	public:
		std::wstring fileName; // Has to be constrained!
		FSP_FSCTL_FILE_INFO fileInfo{};

		DynamicStruct<SECURITY_DESCRIPTOR> fileSecurity;
		DynamicStruct<byte> reparseData;

		explicit FileNode(const std::wstring& fileName);
		~FileNode() = default;
		explicit FileNode(const FileNode& other) = delete;
		FileNode& operator=(const FileNode& other) = delete;
		explicit FileNode(FileNode&& other) noexcept = default;
		FileNode& operator=(FileNode&& other) noexcept = default;

		long GetReferenceCount(const bool withInterlock = true);
		void Reference();
		void Dereference();

		void CopyFileInfo(FSP_FSCTL_FILE_INFO* fileInfoDest) const;

		[[nodiscard]] bool IsMainNode() const;
		std::weak_ptr<FileNode>& GetMainNode();
		void SetMainNode(std::weak_ptr<FileNode> mainNode);

		FileNodeEaMap& GetEaMap();
		void SetEa(PFILE_FULL_EA_INFORMATION ea);
		bool NeedsEa();
		void DeleteEaMap();

		SectorNode& GetSectorNode();

	private:
		// Don't forget to update the move constructor if adding new variables here
		void EnsureFileNameLength() const; // Constrains filename with exceptions

		SectorNode sectors;
		volatile long refCount{0};

		std::weak_ptr<FileNode> mainFileNode;
		std::optional<FileNodeEaMap> eaMap;
	};

	static NTSTATUS CompatFspFileNodeSetEa(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode, PFILE_FULL_EA_INFORMATION ea);
	static NTSTATUS CompatSetFileSizeInternal(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, UINT64 newSize, BOOLEAN setAllocationSize);
	static NTSTATUS CompatGetReparsePointByName(FSP_FILE_SYSTEM* fileSystem, PVOID context, PWSTR fileName, BOOLEAN isDirectory, PVOID buffer, PSIZE_T pSize);
	static BOOLEAN CompatAddDirInfo(FileNode* fileNode, PCWSTR fileName, PVOID buffer, ULONG length, PULONG pBytesTransferred);
}
