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
	using FileNodeMap = std::map<std::wstring, FileNode*, Utils::FileLess>;

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

		std::shared_mutex& GetFileMapMutex();

		[[nodiscard]] bool IsCaseInsensitive() const;
		std::refoptional<FileNode> FindFile(const std::wstring_view& fileName);
		std::optional<FileNode*> FindMainFromStream(const std::wstring_view& fileName);
		std::pair<NTSTATUS, std::refoptional<FileNode>> FindParent(const std::wstring_view& fileName);
		void TouchParent(const FileNode& node);
		bool HasChild(const FileNode& node);

		std::pair<NTSTATUS, FileNode*> InsertNode(FileNode* node);
		std::pair<NTSTATUS, FileNode&> InsertNode(FileNode&& node);
		void RemoveNode(FileNode& node, const bool reportDeletedSize = true);

		std::vector<FileNode*> EnumerateNamedStreams(const FileNode& node, const bool references);
		std::vector<FileNode*> EnumerateDescendants(const FileNode& node, const bool references);
		std::vector<FileNode*> EnumerateDirChildren(const FileNode& node, const wchar_t* marker);

		// --- diagnostics (docs/MEMORY-SAFETY-AUDIT.md, step 1) -------------
		// D1: how often RemoveNode was asked to unlink a name that meanwhile belongs to a
		// different node. Always counted; a single hit proves finding A1.
		std::atomic<uint64_t> diagWrongNameRemovals{0};
		// A3: how often InsertNode(FileNode*) hit a name collision and returned a foreign node
		// under STATUS_SUCCESS.
		std::atomic<uint64_t> diagInsertCollisions{0};
		// A4: how often Cleanup(FspCleanupDelete) ran on a node that is no longer the one the
		// map holds under its name.
		std::atomic<uint64_t> diagCleanupDeleteUnlinked{0};
#if MEMFS_DIAGNOSTICS
		// D2: fileMap write epoch, and how many lookups ran across a concurrent write.
		std::atomic<uint64_t> diagMapEpoch{0};
		std::atomic<uint64_t> diagLookups{0};
		std::atomic<uint64_t> diagOverlappedLookups{0};
		// How many unlocked lookups were in flight at the same time (max observed) — decides
		// whether WinFsp's FINE guard strategy really lets namespace operations overlap.
		std::atomic<long> diagLookupsInFlight{0};
		std::atomic<long> diagMaxLookupsInFlight{0};
		// Writers currently inside the fileMapMutex critical section.
		std::atomic<long> diagWritersActive{0};
#endif
		void DiagReport();

	private:
		std::unique_ptr<FSP_FILE_SYSTEM> fileSystem;

		UINT64 maxFsSize;
		UINT64 cachedMaxFsSize{};
		UINT64 lastCacheTime{};

		std::wstring volumeLabel{L"MEMEFS"};

		SectorManager sectors;
		FileNodeMap fileMap;
		std::shared_mutex fileMapMutex;
	};

	inline MemFs* MEMFS_SINGLETON;

#if MEMFS_DIAGNOSTICS
	// Samples the fileMap write epoch around an unlocked lookup. Two relaxed loads, no lock.
	class LookupGuard {
	public:
		explicit LookupGuard(MemFs& memfs)
			: memfs(memfs), before(memfs.diagMapEpoch.load()),
			  writerSeen(memfs.diagWritersActive.load() != 0) {
			const long inFlight = this->memfs.diagLookupsInFlight.fetch_add(1) + 1;
			long observedMax = this->memfs.diagMaxLookupsInFlight.load();
			while (inFlight > observedMax &&
				!this->memfs.diagMaxLookupsInFlight.compare_exchange_weak(observedMax, inFlight)) {
			}
		}

		~LookupGuard() {
			this->memfs.diagLookupsInFlight.fetch_sub(1);
			this->memfs.diagLookups.fetch_add(1, std::memory_order_relaxed);
			if (this->memfs.diagMapEpoch.load() != this->before ||
				this->writerSeen || this->memfs.diagWritersActive.load() != 0) {
				this->memfs.diagOverlappedLookups.fetch_add(1, std::memory_order_relaxed);
			}
		}

		LookupGuard(const LookupGuard&) = delete;
		LookupGuard& operator=(const LookupGuard&) = delete;

	private:
		MemFs& memfs;
		uint64_t before;
		bool writerSeen;
	};

	// Marks the fileMapMutex critical section of InsertNode/RemoveNode.
	class WriterGuard {
	public:
		explicit WriterGuard(MemFs& memfs) : memfs(memfs) { memfs.diagWritersActive.fetch_add(1); }
		~WriterGuard() { this->memfs.diagWritersActive.fetch_sub(1); }

		WriterGuard(const WriterGuard&) = delete;
		WriterGuard& operator=(const WriterGuard&) = delete;

	private:
		MemFs& memfs;
	};

#define MEMFS_LOOKUP_GUARD() const Memfs::LookupGuard memfsLookupGuard_(*this)
#else
#define MEMFS_LOOKUP_GUARD() ((void)0)
#endif
}
