/*
  * This file is part of WinFsp.
  *
  * You can redistribute it and/or modify it under the terms of the GNU
  * General Public License version 3 as published by the Free Software
  * Foundation.
  *
  * Licensees holding a valid commercial license may use this software
  * in accordance with the commercial license agreement provided in
  * conjunction with the software.  The terms and conditions of any such
  * commercial license agreement shall govern, supersede, and render
  * ineffective any application of the GPLv3 license to this software,
  * notwithstanding of any reference thereto in the software or
  * associated repository.
  */

#pragma once

#include "globalincludes.h"

#include "nodes.h"
#include "sectors.h"

namespace Memfs {
	using FileNodeMap = std::map<std::wstring, std::shared_ptr<FileNode>, Utils::FileLess>;
	using FileReferenceMap = std::unordered_map<UINT64, std::shared_ptr<FileNode>>;

	class MemFs {
	public:
		MemFs(ULONG flags, UINT64 maxFsSize, const wchar_t* fileSystemName, const wchar_t* volumePrefix, const wchar_t* volumeLabel, const wchar_t* rootSddl);
		~MemFs();
		explicit MemFs(const MemFs& other) = delete;
		MemFs(MemFs&& other) noexcept = default;

		MemFs& operator=(const MemFs& other) = delete;
		MemFs& operator=(MemFs&& other) noexcept = default;

		void Destroy();

		[[nodiscard]] NTSTATUS Start() const;
		void Stop() const;

		[[nodiscard]] FSP_FILE_SYSTEM* GetRawFileSystem() const;

		UINT64 GetUsedTotalSize();
		UINT64 CalculateMaxTotalSize();
		UINT64 CalculateAvailableTotalSize();

		std::wstring& GetVolumeLabel();
		void SetVolumeLabel(const std::wstring& str);

		SectorManager& GetSectorManager();
		void RecreateSectorManager();

		[[nodiscard]] bool IsCaseInsensitive() const;
		std::refoptional<FileNode> FindFile(const std::wstring_view& fileName);
		std::refoptional<FileNode> FindMainFromStream(const std::wstring_view& fileName);
		std::pair<NTSTATUS, std::refoptional<FileNode>> FindParent(const std::wstring_view& fileName);
		void TouchParent(const FileNode& node);
		bool HasChild(const FileNode& node);

		std::pair<NTSTATUS, const std::shared_ptr<FileNode>&> InsertNode(const std::shared_ptr<FileNode>& node);
		std::pair<NTSTATUS, FileNode&> InsertNode(FileNode&& node);
		void RemoveNode(FileNode& node, const bool reportDeletedSize = true);

		std::vector<std::shared_ptr<FileNode>> EnumerateNamedStreams(const FileNode& node, const bool references);
		std::vector<std::shared_ptr<FileNode>> EnumerateDescendants(const FileNode& node, const bool references);
		std::vector<std::shared_ptr<FileNode>> EnumerateDirChildren(const FileNode& node, const std::refoptional<const std::wstring_view> marker);

		FileNodeMap& GetRawFileMap();
		FileReferenceMap& GetRawRefMap();

	private:
		std::unique_ptr<FSP_FILE_SYSTEM> fileSystem;

		UINT64 maxFsSize;
		UINT64 cachedMaxFsSize{};
		UINT64 lastCacheTime{};

		std::wstring volumeLabel{L"MEMEFS"};

		SectorManager sectors;
		FileReferenceMap refMap;
		FileNodeMap fileMap;

		// std::unordered_map<MEMFS_FILE_NODE*, UINT64> toBeDeletedFileNodeSizes;
		// std::mutex toBeDeletedFileNodeMutex;
	};

	inline MemFs* MEMFS_SINGLETON;
}
