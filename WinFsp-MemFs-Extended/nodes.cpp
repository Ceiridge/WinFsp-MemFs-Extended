#include "globalincludes.h"
#include "exceptions.h"
#include "utils.h"
#include "nodes.h"

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

void FileNode::EnsureFileNameLength() const {
	if (this->fileName.length() >= MEMFS_MAX_PATH) {
		throw FileNameTooLongException();
	}
}

long FileNode::GetReferenceCount(const bool withInterlock) {
	if (withInterlock) {
		return InterlockedExchangeAdd(&this->refCount, 0L);
	} else {
		return this->refCount;
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
	if (this->IsMainNode()) {
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

bool FileNode::IsMainNode() const {
	return this->mainFileNode.expired();
}

std::weak_ptr<FileNode>& FileNode::GetMainNode() {
	return this->mainFileNode;
}

void FileNode::SetMainNode(const std::weak_ptr<FileNode> mainNode) {
	this->mainFileNode = mainNode;
}

FileNodeEaMap& FileNode::GetEaMap() {
	if (!this->IsMainNode()) {
		return this->mainFileNode.lock()->GetEaMap();
	}

	if (!this->eaMap.has_value()) {
		this->eaMap = std::make_optional(FileNodeEaMap());
	}

	return this->eaMap.value();
}

std::refoptional<FileNodeEaMap> FileNode::GetEaMapOpt() {
	if (!this->IsMainNode()) {
		return this->mainFileNode.lock()->GetEaMapOpt();
	}

	if (!this->eaMap.has_value()) {
		return {};
	}

	return this->eaMap.value();
}

void FileNode::SetEa(PFILE_FULL_EA_INFORMATION ea) {
	auto& fileNodeEaDynamic = *static_cast<DynamicStruct<FILE_FULL_EA_INFORMATION>*>(nullptr);
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
		eaSizeMinus = FspFileSystemGetEaPackedSize(ea);
		eaMap.erase(p); // Now, here the old ea is hopefully freed
	}

	if (0 != ea->EaValueLength && &fileNodeEaDynamic != nullptr) {
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
		return this->mainFileNode.lock()->NeedsEa();
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
