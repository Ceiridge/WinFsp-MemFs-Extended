#include "globalincludes.h"
#include "exceptions.h"
#include "utils.h"
#include "nodes.h"

#include "memfs.h"

using namespace Memfs;

static std::atomic<UINT64> IndexNumber{1};
static constexpr bool LOG_REFERENCES = false; // Debug option

FileNode::FileNode(FileNode&& other) noexcept
	: fileName(std::move(other.fileName)),
	  fileInfo(other.fileInfo),
	  fileSecurity(std::move(other.fileSecurity)),
	  reparseData(std::move(other.reparseData)),
	  // nodeMutex is intentionally NOT moved — new object gets a fresh unlocked mutex
	  sectors(std::move(other.sectors)),
	  refCount(other.refCount),
	  mainFileNode(other.mainFileNode),
	  eaMap(std::move(other.eaMap)) {
	// A2: the reference on the main node moves with the pointer — the source must not
	// release it again in its destructor.
	other.mainFileNode = nullptr;
}

FileNode::FileNode(const std::wstring& fileName) : fileName(fileName) {
	this->EnsureFileNameLength();

	const uint64_t now = Utils::GetSystemTime();
	this->fileInfo.CreationTime =
		this->fileInfo.LastAccessTime =
		this->fileInfo.LastWriteTime =
		this->fileInfo.ChangeTime = now;

	this->fileInfo.IndexNumber = IndexNumber.fetch_add(1, std::memory_order_relaxed);
}

FileNode::~FileNode() {
#if MEMFS_DIAGNOSTICS
	this->DiagCheckAlive("~FileNode");
#endif

	// A2: release the reference this stream holds on its main node.
	if (this->mainFileNode != nullptr) {
		FileNode* const mainNode = this->mainFileNode;
		this->mainFileNode = nullptr;
		mainNode->Dereference();
	}

#if MEMFS_DIAGNOSTICS
	this->diagMagic = DEAD_MAGIC;
#endif
}

#if MEMFS_DIAGNOSTICS
void FileNode::DiagCheckAlive(const char* where) const {
	if (this->diagMagic != ALIVE_MAGIC) {
		DiagNodeUseAfterFree.fetch_add(1, std::memory_order_relaxed);
		FspDebugLog("memefs diagnostics: use of a destroyed FileNode in %s\n", where);
	}
}
#endif

void FileNode::EnsureFileNameLength() const {
	if (this->fileName.length() >= MEMFS_MAX_PATH) {
		throw FileNameTooLongException();
	}
}

long FileNode::GetReferenceCount(const bool withInterlock) {
	if (withInterlock) {
		return FspInterlockedLoad32(reinterpret_cast<INT32 volatile*>(&this->refCount));
	} else {
		return this->refCount;
	}
}

void FileNode::Reference() {
#if MEMFS_DIAGNOSTICS
	this->DiagCheckAlive("Reference");
#endif
	InterlockedIncrement(&this->refCount);

	if (LOG_REFERENCES) {
		FspServiceLog(EVENTLOG_INFORMATION_TYPE, (PWSTR)L"+%d for %s", this->refCount, this->fileName.c_str());
	}
}

void FileNode::Dereference() {
#if MEMFS_DIAGNOSTICS
	this->DiagCheckAlive("Dereference");
#endif
	const long newRefCount = InterlockedDecrement(&this->refCount);

	if (LOG_REFERENCES) {
		FspServiceLog(EVENTLOG_INFORMATION_TYPE, (PWSTR)L"-%d for %s", newRefCount, this->fileName.c_str());
	}

	if (newRefCount == 0) {
		if (LOG_REFERENCES) {
			FspServiceLog(EVENTLOG_INFORMATION_TYPE, (PWSTR)L"Removing %s", this->fileName.c_str());
		}
		delete this;
	}
}

void FileNode::CopyFileInfo(FSP_FSCTL_FILE_INFO* fileInfoDest) const {
#if MEMFS_DIAGNOSTICS
	this->DiagCheckAlive("CopyFileInfo");
#endif
	if (this->IsMainNode()) {
		*fileInfoDest = this->fileInfo;
	} else {
		const auto mainFile = this->mainFileNode;
#if MEMFS_DIAGNOSTICS
		mainFile->DiagCheckAlive("CopyFileInfo(main node)");
#endif
		std::shared_lock mainLock(mainFile->nodeMutex);
		*fileInfoDest = mainFile->fileInfo;
		mainLock.unlock();

		fileInfoDest->FileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;
		/* named streams cannot be directories */
		fileInfoDest->AllocationSize = this->fileInfo.AllocationSize;
		fileInfoDest->FileSize = this->fileInfo.FileSize;
	}
}

bool FileNode::IsMainNode() const {
	return this->mainFileNode == nullptr;
}

FileNode* FileNode::GetMainNode() const {
#if MEMFS_DIAGNOSTICS
	this->DiagCheckAlive("GetMainNode");
	if (this->mainFileNode != nullptr) {
		this->mainFileNode->DiagCheckAlive("GetMainNode(main node)");
	}
#endif
	return this->mainFileNode;
}

void FileNode::SetMainNode(FileNode* mainNode) {
	if (this->mainFileNode == mainNode) {
		return;
	}

	// A2: a named stream keeps its main node alive. Without this the main node can be deleted
	// while the stream is still open (FILE_SHARE_DELETE / POSIX unlink), and every later
	// GetSecurity/CopyFileInfo on the stream reads freed memory. Plain interlocked ops, no lock.
	if (mainNode != nullptr) {
		mainNode->Reference();
	}

	FileNode* const previous = this->mainFileNode;
	this->mainFileNode = mainNode;

	if (previous != nullptr) {
		previous->Dereference();
	}
}

FileNodeEaMap& FileNode::GetEaMap() {
	if (!this->IsMainNode()) {
		return this->mainFileNode->GetEaMap();
	}

	if (!this->eaMap.has_value()) {
		this->eaMap = FileNodeEaMap();
	}

	return this->eaMap.value();
}

std::refoptional<FileNodeEaMap> FileNode::GetEaMapOpt() {
	if (!this->IsMainNode()) {
		return this->mainFileNode->GetEaMapOpt();
	}

	if (!this->eaMap.has_value()) {
		return {};
	}

	return this->eaMap.value();
}

void FileNode::SetEa(PFILE_FULL_EA_INFORMATION ea) {
	DynamicStruct<FILE_FULL_EA_INFORMATION> fileNodeEaDynamic;
	FILE_FULL_EA_INFORMATION* fileNodeEa = nullptr;
	ULONG eaSizePlus = 0, eaSizeMinus = 0;

	FileNodeEaMap& eaMap = this->GetEaMap();

	if (0 != ea->EaValueLength) {
		eaSizePlus = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) +
			ea->EaNameLength + 1 + ea->EaValueLength;

		fileNodeEaDynamic = DynamicStruct<FILE_FULL_EA_INFORMATION>(eaSizePlus);
		fileNodeEa = fileNodeEaDynamic.Struct();
		memcpy_s(fileNodeEa, fileNodeEaDynamic.ByteSize(), ea, eaSizePlus);

		fileNodeEa->NextEntryOffset = 0;

		eaSizePlus = FspFileSystemGetEaPackedSize(ea);
	}

	const FileNodeEaMap::iterator p = eaMap.find(ea->EaName);
	if (p != eaMap.end()) {
		// C4: measure the entry that is being *replaced*, not the new one. Measuring the new one
		// makes fileInfo.EaSize drift and, as a UINT32, underflow.
		if (p->second.HoldsStruct()) {
			eaSizeMinus = FspFileSystemGetEaPackedSize(p->second.Struct());
		}
		eaMap.erase(p); // Now, here the old ea is hopefully freed
	}

	if (0 != ea->EaValueLength && fileNodeEaDynamic.HoldsStruct()) {
		try {
			eaMap.insert(FileNodeEaMap::value_type(fileNodeEa->EaName, std::move(fileNodeEaDynamic)));
		} catch (...) {
			throw CreateException(STATUS_INSUFFICIENT_RESOURCES);
		}
	}

	this->fileInfo.EaSize = this->fileInfo.EaSize + eaSizePlus - eaSizeMinus;
}

bool FileNode::NeedsEa() {
	if (!this->IsMainNode()) {
		return this->mainFileNode->NeedsEa();
	}

	if (!this->eaMap.has_value()) {
		return false;
	}

	for (const auto& p : this->eaMap.value()) {
		if (p.second.HoldsStruct() && 0 != (p.second.Struct()->Flags & FILE_NEED_EA)) {
			return true;
		}
	}

	return false;
}

void FileNode::DeleteEaMap() {
	this->eaMap.reset();
}

SectorNode& FileNode::GetSectorNode() {
	return this->sectors;
}
