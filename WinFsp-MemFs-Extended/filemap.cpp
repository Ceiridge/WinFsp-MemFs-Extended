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
	const std::wstring_view mainNameView = (colonPos != fileName.end())
		? std::wstring_view(fileName.data(), colonPos - fileName.begin())
		: fileName;

	const auto iter = this->fileMap.find(mainNameView);
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
	const size_t nodeLen = node.fileName.length();
	const bool isRoot = (nodeLen == 1 && node.fileName[0] == L'\\');

	for (auto iter = this->fileMap.upper_bound(node.fileName); this->fileMap.end() != iter; ++iter) {
		const std::wstring& childName = iter->second->fileName;
		const size_t childLen = childName.length();

		// Skip named streams
		if (childName.find(L':') != std::wstring::npos) {
			continue;
		}

		// Check it is a direct child: prefix matches and no further separator after it
		bool isChild;
		if (isRoot) {
			isChild = childLen > 1 &&
			          childName.find(L'\\', 1) == std::wstring::npos;
		} else {
			isChild = childLen > nodeLen &&
			          childName[nodeLen] == L'\\' &&
			          childName.find(L'\\', nodeLen + 1) == std::wstring::npos;
		}

		if (isChild) {
			return true;
		}
		break; // map is sorted; first entry after upper_bound is the closest match
	}

	return false;
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

	this->TouchParent(node);
	node.Dereference();
}

std::vector<FileNode*> MemFs::EnumerateNamedStreams(const FileNode& node, const bool references) {
	std::vector<FileNode*> namedStreams;
	const int nodeLen = (int)node.fileName.length();

	for (auto iter = this->fileMap.upper_bound(node.fileName); this->fileMap.end() != iter; ++iter) {
		if (!Utils::FileNameHasPrefix(iter->second->fileName.c_str(), (int)iter->second->fileName.length(), node.fileName.c_str(), nodeLen, this->IsCaseInsensitive()))
			break;
		if (L':' != iter->second->fileName[nodeLen])
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
	const int nodeLen = (int)node.fileName.length();

	for (auto iter = this->fileMap.lower_bound(node.fileName); this->fileMap.end() != iter; ++iter) {
		if (!Utils::FileNameHasPrefix(iter->second->fileName.c_str(), (int)iter->second->fileName.length(), node.fileName.c_str(), nodeLen, this->IsCaseInsensitive()))
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
	const size_t nodeLen = node.fileName.length();
	const int nodeLenI = (int)nodeLen;
	const bool isRoot = (nodeLen == 1 && node.fileName[0] == L'\\');

	if (marker) {
		const bool needsSlash = !isRoot;
		std::wstring markerKey;
		markerKey.reserve(nodeLen + 1 + wcslen(marker));
		markerKey = node.fileName;
		if (needsSlash) markerKey += L'\\';
		markerKey += marker;
		iter = this->fileMap.upper_bound(markerKey);
	} else {
		iter = this->fileMap.upper_bound(node.fileName);
	}

	for (; this->fileMap.end() != iter; ++iter) {
		const std::wstring& childName = iter->second->fileName;
		const size_t childLen = childName.length();

		if (!Utils::FileNameHasPrefix(childName.c_str(), (int)childLen, node.fileName.c_str(), nodeLenI, this->IsCaseInsensitive()))
			break;

		// Direct child: next char after prefix is '\', no further '\' or ':' after it
		bool isDirectoryChild;
		if (isRoot) {
			isDirectoryChild = childLen > 1 &&
			                   childName.find(L'\\', 1) == std::wstring::npos &&
			                   childName.find(L':', 1) == std::wstring::npos;
		} else {
			isDirectoryChild = childLen > nodeLen &&
			                   childName[nodeLen] == L'\\' &&
			                   childName.find(L'\\', nodeLen + 1) == std::wstring::npos &&
			                   childName.find(L':', nodeLen + 1) == std::wstring::npos;
		}

		if (isDirectoryChild) {
			children.push_back(iter->second);
		}
	}

	return children;
}
