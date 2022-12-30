#include "nodes.h"
#include "exceptions.h"
#include "utils.h"

using namespace Memfs;

static UINT64 IndexNumber = 1;

FileNode::FileNode(const std::wstring& fileName) : fileName(fileName) {
	this->EnsureFileNameLength();

	const uint64_t now = Utils::GetSystemTime();
	this->fileInfo.CreationTime =
		this->fileInfo.LastAccessTime =
		this->fileInfo.LastWriteTime =
		this->fileInfo.ChangeTime = now;

	this->fileInfo.IndexNumber = IndexNumber++;
}

FileNode::FileNode(FileNode&& other) noexcept : fileName(std::move(other.fileName)), fileInfo(other.fileInfo), sectors(std::move(other.sectors)), fileSecuritySize(other.fileSecuritySize), fileSecurity(std::move(other.fileSecurity)), reparseDataSize(other.reparseDataSize), reparseData(std::move(other.reparseData)), refCount(other.refCount), mainFileNode(std::move(other.mainFileNode)), eaMap(std::move(other.eaMap)) {
	this->EnsureFileNameLength();
}

void FileNode::EnsureFileNameLength() const {
	if (this->fileName.length() >= MEMFS_MAX_PATH) {
		throw FileNameTooLongException();
	}
}


void FileNode::Reference() {
	InterlockedIncrement(&this->refCount);
}

void FileNode::Dereference() {
	if (InterlockedDecrement(&this->refCount) == 0) {
		// TODO: Delete or does it happen automatically?
	}
}

void FileNode::CopyFileInfo(FSP_FSCTL_FILE_INFO* fileInfoDest) const {
	if (this->mainFileNode.expired()) {
		*fileInfoDest = this->fileInfo;
	} else {
		const auto mainFile = this->mainFileNode.lock();
		*fileInfoDest = mainFile->fileInfo;

		fileInfoDest->FileAttributes &= ~FILE_ATTRIBUTE_DIRECTORY;
		/* named streams cannot be directories */
		fileInfoDest->AllocationSize = this->fileInfo.AllocationSize;
		fileInfoDest->FileSize = this->fileInfo.FileSize;
	}
}

FileNodeEaMap& FileNode::GetEaMap() {
	if (!this->mainFileNode.expired()) {
		return this->mainFileNode.lock()->GetEaMap();
	}

	if (!this->eaMap.has_value()) {
		this->eaMap = std::make_optional(FileNodeEaMap());
	}

	return this->eaMap.value();
}

void FileNode::SetEa(PFILE_FULL_EA_INFORMATION ea) {
	FILE_FULL_EA_INFORMATION* fileNodeEa = nullptr;
	ULONG eaSizePlus = 0, eaSizeMinus = 0;

	FileNodeEaMap& eaMap = this->GetEaMap();

	if (0 != ea->EaValueLength) {
		eaSizePlus = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) +
			ea->EaNameLength + 1 + ea->EaValueLength;

		// This is horrible and better not cause memory leaks
		const size_t requiredInts = eaSizePlus / sizeof(int32_t) + 1;
		fileNodeEa = reinterpret_cast<FILE_FULL_EA_INFORMATION*>(new int32_t[requiredInts]);
		memcpy_s(fileNodeEa, requiredInts * sizeof(int32_t), ea, eaSizePlus);

		fileNodeEa->NextEntryOffset = 0;

		eaSizePlus = FspFileSystemGetEaPackedSize(ea);
	}

	const FileNodeEaMap::iterator p = eaMap.find(ea->EaName);
	if (p != eaMap.end()) {
		eaSizeMinus = FspFileSystemGetEaPackedSize(ea);
		eaMap.erase(p); // Now, here the old ea is hopefully freed
	}

	if (0 != ea->EaValueLength) {
		try {
			eaMap.insert(FileNodeEaMap::value_type(fileNodeEa->EaName, fileNodeEa));
		} catch (...) {
			delete fileNodeEa;
			throw CreateException(STATUS_INSUFFICIENT_RESOURCES);
		}
	}

	this->fileInfo.EaSize = this->fileInfo.EaSize + eaSizePlus - eaSizeMinus;
}

bool FileNode::NeedsEa() {
	if (!this->mainFileNode.expired()) {
		return this->mainFileNode.lock()->NeedsEa();
	}

	if (!this->eaMap.has_value()) {
		return false;
	}

	for (const auto& p : this->eaMap) {
		if (0 != (p.second->Flags & FILE_NEED_EA)) {
			return true;
		}
	}

	return false;
}
