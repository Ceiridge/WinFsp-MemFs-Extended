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

#include <map>
#include <winfsp/winfsp.h>

#include "nodes.h"
#include "sectors.h"

namespace Memfs {
	static constexpr int MEMFS_MAX_PATH = 32766;
	static constexpr UINT64 MEMFS_SECTOR_SIZE = 512;
	static constexpr UINT64 MEMFS_SECTORS_PER_ALLOCATION_UNIT = 1;

	enum {
		MemfsDisk = 0x00000000,
		MemfsNet = 0x00000001,
		MemfsDeviceMask = 0x0000000f,
		MemfsCaseInsensitive = 0x80000000,
		MemfsFlushAndPurgeOnCleanup = 0x40000000,
		MemfsLegacyUnlinkRename = 0x20000000,
	};

	using FileNodeMap = std::map<std::wstring, FileNode, Utils::FileLess>;

	class MemFs {
	public:
		MemFs(ULONG flags, UINT64 maxFsSize, const wchar_t* fileSystemName, const wchar_t* volumePrefix, const wchar_t* volumeLabel, const wchar_t* rootSddl);
		~MemFs();
		explicit MemFs(const MemFs& other) = delete;
		MemFs(MemFs&& other) noexcept = delete;

		MemFs& operator=(const MemFs& other) = delete;
		MemFs& operator=(MemFs&& other) noexcept = delete;

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
		std::optional<FileNode&> FindFile(const std::wstring_view& fileName);
		std::optional<FileNode&> FindMainFromStream(const std::wstring_view& fileName);
		std::pair<NTSTATUS, std::optional<FileNode&>> FindParent(const std::wstring_view& fileName);
		void TouchParent(const FileNode& node);
		bool HasChild(const FileNode& node);

		std::pair<NTSTATUS, FileNode&> InsertNode(FileNode&& node);
		void RemoveNode(FileNode& node, const bool reportDeletedSize = true);

		std::vector<FileNode*> EnumerateNamedStreams(const FileNode& node, const bool references);

	private:
		FSP_FILE_SYSTEM* fileSystem{};

		UINT64 maxFsSize;
		UINT64 cachedMaxFsSize;
		UINT64 lastCacheTime;

		std::wstring volumeLabel{L"MEMEFS"};

		SectorManager sectors;
		FileNodeMap fileMap;

		// std::unordered_map<MEMFS_FILE_NODE*, UINT64> toBeDeletedFileNodeSizes;
		// std::mutex toBeDeletedFileNodeMutex;
	};

	static inline MemFs* MEMFS_SINGLETON;
}
