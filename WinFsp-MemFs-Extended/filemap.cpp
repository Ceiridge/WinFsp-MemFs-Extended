#include <filesystem>

#include "memfs.h"
#include "utils.h"

using namespace Memfs;

bool MemFs::IsCaseInsensitive() const {
	return this->fileMap.key_comp().CaseInsensitive;
}

std::optional<FileNode&> MemFs::FindMainFromStream(const std::wstring_view fileName) {
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

	return {iter->second};
}

std::pair<NTSTATUS, std::optional<FileNode&>> MemFs::FindParent(const std::wstring_view fileName) {
	const std::filesystem::path filePath = fileName;
	const auto parentPath = filePath.parent_path().make_preferred();

	const auto iter = this->fileMap.find(parentPath);
	if (iter == this->fileMap.end()) {
		return {STATUS_OBJECT_PATH_NOT_FOUND, {}};
	}

	if (0 == (iter->second.fileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		return {STATUS_NOT_A_DIRECTORY, {}};
	}

	return {STATUS_SUCCESS, {iter->second}};
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

NTSTATUS MemFs::InsertNode(FileNode& node) {
	try {
		const auto [iter, success] = this->fileMap.insert(FileNodeMap::value_type(node.fileName, std::move(node)));

		if (success) {
			iter->second.Reference();
			this->TouchParent(iter->second);
		}

		return STATUS_SUCCESS;
	} catch (...) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
}

void MemFs::RemoveNode(FileNode& node, const bool reportDeletedSize) {
	if (this->fileMap.erase(node.fileName)) {
		// memefs: Quickly report counter about deleted sectors
		if (reportDeletedSize) {
			// TODO: toBeDeleted stuff
		}

		this->TouchParent(node); // TODO: Might be out of scope here?
		node.Dereference();
	}
}
