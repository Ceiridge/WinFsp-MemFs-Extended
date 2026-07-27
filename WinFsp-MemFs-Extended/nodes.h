#pragma once

#include "globalincludes.h"

#include "sectors.h"
#include "comparisons.h"
#include "dynamicstruct.h"

namespace Memfs {
	using FileNodeEaMap = std::map<std::string, DynamicStruct<FILE_FULL_EA_INFORMATION>, Utils::EaLess>;

#if MEMFS_DIAGNOSTICS
	// D0 substitute (docs/MEMORY-SAFETY-AUDIT.md): gflags/PageHeap needs administrator rights,
	// so instead every FileNode carries a liveness magic that its destructor clears. Touching a
	// destroyed node through a stale pointer (findings A1, A2, B2, B4) is then counted instead
	// of silently corrupting whatever the allocator handed the memory to.
	inline std::atomic<uint64_t> DiagNodeUseAfterFree{0};
#endif

	class FileNode {
	public:
		std::wstring fileName; // Has to be constrained!
		FSP_FSCTL_FILE_INFO fileInfo{};

		DynamicStruct<SECURITY_DESCRIPTOR> fileSecurity;
		DynamicStruct<byte> reparseData;

		explicit FileNode(const std::wstring& fileName);
		~FileNode();
		explicit FileNode(const FileNode& other) = delete;
		FileNode& operator=(const FileNode& other) = delete;
		// std::shared_mutex is not movable; move constructor defined in nodes.cpp
		explicit FileNode(FileNode&& other) noexcept;
		FileNode& operator=(FileNode&& other) noexcept = delete;

		long GetReferenceCount(const bool withInterlock = true);
		void Reference();
		void Dereference();

		void CopyFileInfo(FSP_FSCTL_FILE_INFO* fileInfoDest) const;

		[[nodiscard]] bool IsMainNode() const;
		FileNode* GetMainNode() const;
		void SetMainNode(FileNode* mainNode);

		FileNodeEaMap& GetEaMap();
		std::refoptional<FileNodeEaMap> GetEaMapOpt();
		void SetEa(PFILE_FULL_EA_INFORMATION ea);
		bool NeedsEa();
		void DeleteEaMap();

		SectorNode& GetSectorNode();

		mutable std::shared_mutex nodeMutex;

	private:
		// Keep the manual move constructor in nodes.cpp in sync when adding members here
		void EnsureFileNameLength() const; // Constrains filename with exceptions

#if MEMFS_DIAGNOSTICS
		static constexpr uint32_t ALIVE_MAGIC = 0x4D454D46; // 'MEMF'
		static constexpr uint32_t DEAD_MAGIC = 0xDEADF11E;
		void DiagCheckAlive(const char* where) const;
		uint32_t diagMagic{ALIVE_MAGIC};
#endif

		SectorNode sectors;
		volatile long refCount{0};

		FileNode* mainFileNode{};
		std::optional<FileNodeEaMap> eaMap;
	};

	NTSTATUS CompatFspFileNodeSetEa(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode, PFILE_FULL_EA_INFORMATION ea);
	NTSTATUS CompatSetFileSizeInternal(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0, UINT64 newSize, BOOLEAN setAllocationSize);
	NTSTATUS CompatGetReparsePointByName(FSP_FILE_SYSTEM* fileSystem, PVOID context, PWSTR fileName, BOOLEAN isDirectory, PVOID buffer, PSIZE_T pSize);
	BOOLEAN CompatAddDirInfo(FileNode* fileNode, PCWSTR fileName, PVOID buffer, ULONG length, PULONG pBytesTransferred);
	BOOLEAN CompatAddStreamInfo(FileNode* fileNode, PVOID buffer, ULONG length, PULONG pBytesTransferred);
}
