#pragma once

#include <map>
#include <memory>
#include <optional>
#include <winfsp/winfsp.h>
#include <string>

#include "sectors.h"
#include "comparisons.h"

namespace Memfs {
	using FileNodeEaMap = std::map<std::string, std::unique_ptr<FILE_FULL_EA_INFORMATION>, Utils::EaLess>;

	class FileNode {
	public:
		std::wstring fileName; // Has to be constrained!
		FSP_FSCTL_FILE_INFO fileInfo{};

		explicit FileNode(const std::wstring& fileName);
		~FileNode() = default;
		explicit FileNode(const FileNode& other) = delete;
		FileNode& operator=(const FileNode& other) = delete;
		explicit FileNode(FileNode&& other) noexcept;
		FileNode& operator=(FileNode&& other) noexcept = delete;

		void Reference();
		void Dereference();

		void CopyFileInfo(FSP_FSCTL_FILE_INFO* fileInfoDest) const;

		FileNodeEaMap& GetEaMap();
		void SetEa(PFILE_FULL_EA_INFORMATION ea);
		bool NeedsEa();

	private:
		// Don't forget to update the move constructor if adding new variables here
		void EnsureFileNameLength() const; // Constrains filename with exceptions

		SectorNode sectors;

		size_t fileSecuritySize{0};
		std::unique_ptr<SECURITY_DESCRIPTOR> fileSecurity;

		size_t reparseDataSize{0};
		std::unique_ptr<void> reparseData;

		volatile long refCount{0};

		std::weak_ptr<FileNode> mainFileNode;
		std::optional<FileNodeEaMap> eaMap;
	};
}
