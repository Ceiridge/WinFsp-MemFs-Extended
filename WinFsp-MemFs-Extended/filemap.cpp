#include "globalincludes.h"
#include "utils.h"
#include "memfs.h"

using namespace Memfs;

bool MemFs::IsCaseInsensitive() const {
	return this->fileMap.key_comp().CaseInsensitive;
}

std::refoptional<FileNode> MemFs::FindFile(const std::wstring_view& fileName) {
	const auto iter = this->fileMap.find(fileName);
	if (iter == this->fileMap.end()) {
		return {};
	}

	return *iter->second;
}

std::optional<FileNode*> MemFs::FindMainFromStream(const std::wstring_view& fileName) {
	const auto colonPos = std::ranges::find(fileName, L':');
	std::wstring mainName;

	if (colonPos != fileName.end()) {
		mainName = std::wstring(fileName.data(), colonPos - fileName.begin());
	} else {
		mainName = fileName;
	}

	const auto iter = this->fileMap.find(mainName);
	if (iter == this->fileMap.end()) {
		return {};
	}

	return iter->second;
}

std::pair<NTSTATUS, std::refoptional<FileNode>> MemFs::FindParent(const std::wstring_view& fileName) {
	const auto parentPath = Utils::PathSuffix(fileName).RemainPrefix;

	const auto iter = this->fileMap.find(parentPath);
	if (iter == this->fileMap.end()) {
		return {STATUS_OBJECT_PATH_NOT_FOUND, {}};
	}

	if (0 == (iter->second->fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		return {STATUS_NOT_A_DIRECTORY, {}};
	}

	return {STATUS_SUCCESS, *iter->second};
}

void MemFs::TouchParent(const FileNode& node) {
	if (node.fileName.empty() || node.fileName == L"\\") {
		return;
	}

	const auto [fst, snd] = this->FindParent(node.fileName);
	if (snd.has_value()) {
		FileNode& parent = snd.value();

		parent.fileInfo.LastAccessTime = parent.fileInfo.LastWriteTime = parent.fileInfo.ChangeTime = Utils::GetSystemTime();
	}
}

bool MemFs::HasChild(const FileNode& node) {
	bool result = false;

	for (auto iter = this->fileMap.upper_bound(node.fileName); this->fileMap.end() != iter; ++iter) {
		if (iter->second->fileName.find(L':') != std::wstring::npos) {
			continue;
		}

		const auto [remainPrefix, suffix] = Utils::PathSuffix(iter->second->fileName);
		result = 0 == Utils::FileNameCompare(remainPrefix.data(), remainPrefix.length(), node.fileName.c_str(), node.fileName.length(), this->IsCaseInsensitive());
		break;
	}

	return result;
}

std::pair<NTSTATUS, FileNode*> MemFs::InsertNode(FileNode* node) {
	try {
		const auto [iter, success] = this->fileMap.emplace(node->fileName, node);

		if (success) {
			iter->second->Reference();
			this->TouchParent(*iter->second);
		}

		return {STATUS_SUCCESS, iter->second};
	} catch (...) {
		return {STATUS_INSUFFICIENT_RESOURCES, node};
	}
}

std::pair<NTSTATUS, FileNode&> MemFs::InsertNode(FileNode&& node) {
	FileNode* allocatedNode{new FileNode(std::move(node))};
	const auto [status, ptr] = this->InsertNode(allocatedNode);
	return {status, *ptr};
}

void MemFs::RemoveNode(FileNode& node, const bool reportDeletedSize) {
	if (!this->fileMap.erase(node.fileName)) {
		return;
	}

	// memefs: Quickly report counter about deleted sectors
	if (reportDeletedSize) {
		// TODO: toBeDeleted stuff
	}

	this->TouchParent(node);
	node.Dereference(true);
}

std::vector<FileNode*> MemFs::EnumerateNamedStreams(const FileNode& node, const bool references) {
	std::vector<FileNode*> namedStreams;

	for (auto iter = this->fileMap.upper_bound(node.fileName); this->fileMap.end() != iter; ++iter) {
		if (!Utils::FileNameHasPrefix(iter->second->fileName.c_str(), node.fileName.c_str(), this->IsCaseInsensitive()))
			break;
		if (L':' != iter->second->fileName[node.fileName.length()])
			break;

		if (references) {
			iter->second->Reference();
		}
		namedStreams.push_back(iter->second);
	}

	return namedStreams;
}

std::vector<FileNode*> MemFs::EnumerateDescendants(const FileNode& node, const bool references) {
	std::vector<FileNode*> descendants;

	for (auto iter = this->fileMap.lower_bound(node.fileName); this->fileMap.end() != iter; ++iter) {
		if (!Utils::FileNameHasPrefix(iter->second->fileName.c_str(), node.fileName.c_str(), this->IsCaseInsensitive()))
			break;

		if (references) {
			iter->second->Reference();
		}
		descendants.push_back(iter->second);
	}

	return descendants;
}

std::vector<FileNode*> MemFs::EnumerateDirChildren(const FileNode& node, const wchar_t* marker) {
	std::vector<FileNode*> children;
	FileNodeMap::iterator iter;

	if (marker) {
		const bool needsSlash = node.fileName.length() != 1 || node.fileName[0] != L'\\';
		iter = this->fileMap.upper_bound(node.fileName + (needsSlash ? L"\\" : L"") + marker);
	} else
		iter = this->fileMap.upper_bound(node.fileName);

	for (; this->fileMap.end() != iter; ++iter) {
		if (!Utils::FileNameHasPrefix(iter->second->fileName.c_str(), node.fileName.c_str(), this->IsCaseInsensitive()))
			break;

		const Utils::SuffixView suffixView = Utils::PathSuffix(iter->second->fileName);

		bool isDirectoryChild = 0 == Utils::FileNameCompare(suffixView.RemainPrefix.data(), suffixView.RemainPrefix.length(), node.fileName.c_str(), node.fileName.length(), this->IsCaseInsensitive());
		isDirectoryChild = isDirectoryChild && suffixView.Suffix.find(L':') == std::string::npos;

		if (isDirectoryChild) {
			children.push_back(iter->second);
		}
	}

	return children;
}
