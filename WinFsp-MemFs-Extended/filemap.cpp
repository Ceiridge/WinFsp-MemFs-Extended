#include "globalincludes.h"
#include "utils.h"
#include "memfs.h"

using namespace Memfs;

bool MemFs::IsCaseInsensitive() const {
	return this->fileMap.key_comp().CaseInsensitive;
}

std::refoptional<FileNode> MemFs::FindFile(const std::wstring_view& fileName) {
	MEMFS_LOOKUP_GUARD();

	const auto iter = this->fileMap.find(fileName);
	if (iter == this->fileMap.end()) {
		return {};
	}

	return *iter->second;
}

std::optional<FileNode*> MemFs::FindMainFromStream(const std::wstring_view& fileName) {
	MEMFS_LOOKUP_GUARD();

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
	MEMFS_LOOKUP_GUARD();

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

		std::unique_lock lock(parent.nodeMutex);
		parent.fileInfo.LastAccessTime = parent.fileInfo.LastWriteTime = parent.fileInfo.ChangeTime = Utils::GetSystemTime();
	}
}

bool MemFs::HasChild(const FileNode& node) {
	MEMFS_LOOKUP_GUARD();

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
		FileNode* resultNode;
		bool didInsert;
		{
			std::unique_lock mapLock(this->fileMapMutex);
#if MEMFS_DIAGNOSTICS
			const WriterGuard writerGuard(*this);
#endif
			const auto [iter, success] = this->fileMap.emplace(node->fileName, node);
			resultNode = iter->second;
			didInsert = success;
			if (!success) {
				this->diagInsertCollisions.fetch_add(1, std::memory_order_relaxed);
				FspDebugLog(__FUNCTION__ ": name collision - the name already belongs to another node\n");
			} else {
				iter->second->Reference();
#if MEMFS_DIAGNOSTICS
				this->diagMapEpoch.fetch_add(1);
#endif
			}
		}
		if (!didInsert) {
			// A3: the name is taken by another node. Returning it under STATUS_SUCCESS would
			// hand the caller a handle to the wrong file and leak the node it wanted to insert.
			return {STATUS_OBJECT_NAME_COLLISION, resultNode};
		}

		this->TouchParent(*resultNode);
		return {STATUS_SUCCESS, resultNode};
	} catch (...) {
		return {STATUS_INSUFFICIENT_RESOURCES, node};
	}
}

std::pair<NTSTATUS, FileNode&> MemFs::InsertNode(FileNode&& node) {
	FileNode* allocatedNode{new FileNode(std::move(node))};
	const auto [status, ptr] = this->InsertNode(allocatedNode);

	if (!NT_SUCCESS(status)) {
		// The node never entered the map, so nothing else can reach it.
		delete allocatedNode;

		if (STATUS_OBJECT_NAME_COLLISION != status) {
			// emplace threw std::bad_alloc — unrecoverable for an in-memory filesystem
			FspDebugLog(__FUNCTION__ ": cannot insert into FileNodeMap; aborting\n");
			abort();
		}
		// On a collision ptr is the *existing* node, which is still alive.
	}

	return {status, *ptr};
}

void MemFs::RemoveNode(FileNode& node, const bool reportDeletedSize) {
	{
		std::unique_lock mapLock(this->fileMapMutex);
#if MEMFS_DIAGNOSTICS
		const WriterGuard writerGuard(*this);
#endif

		const auto iter = this->fileMap.find(node.fileName);
		if (iter == this->fileMap.end()) {
			return;
		}

		// A1: the name may meanwhile belong to a *different* node — rename with
		// replaceIfExists, POSIX unlink with handles still open. Erasing it would unlink the
		// wrong entry and drop a reference this node never owned.
		if (iter->second != &node) {
			this->diagWrongNameRemovals.fetch_add(1, std::memory_order_relaxed);
			FspDebugLog(__FUNCTION__ ": name belongs to a different node - not unlinking\n");
			return;
		}

		this->fileMap.erase(iter);
#if MEMFS_DIAGNOSTICS
		this->diagMapEpoch.fetch_add(1, std::memory_order_relaxed);
#endif
	}
	this->TouchParent(node);
	node.Dereference();
}

void MemFs::DiagReport() {
	// wvsprintf-based loggers: keep the format specifiers to what they understand.
	const ULONG wrongName = (ULONG)this->diagWrongNameRemovals.load(std::memory_order_relaxed);
	const ULONG collisions = (ULONG)this->diagInsertCollisions.load(std::memory_order_relaxed);
	const ULONG unlinkedCleanups = (ULONG)this->diagCleanupDeleteUnlinked.load(std::memory_order_relaxed);
	FspDebugLog("memefs diagnostics: A1 wrong-name removals = %lu, A3 insert collisions = %lu, "
	            "A4 cleanup-delete on unlinked node = %lu\n", wrongName, collisions, unlinkedCleanups);
	FspServiceLog((wrongName | collisions | unlinkedCleanups) != 0 ? EVENTLOG_ERROR_TYPE : EVENTLOG_INFORMATION_TYPE,
	              (PWSTR)L"memefs diagnostics: A1 wrong-name removals = %lu, A3 insert collisions = %lu, "
	              L"A4 cleanup-delete on unlinked node = %lu", wrongName, collisions, unlinkedCleanups);

#if MEMFS_DIAGNOSTICS
	const ULONG lookups = (ULONG)this->diagLookups.load(std::memory_order_relaxed);
	const ULONG overlapped = (ULONG)this->diagOverlappedLookups.load(std::memory_order_relaxed);
	const ULONG epoch = (ULONG)this->diagMapEpoch.load(std::memory_order_relaxed);
	const ULONG useAfterFree = (ULONG)DiagNodeUseAfterFree.load(std::memory_order_relaxed);
	FspDebugLog("memefs diagnostics: D0 uses of a destroyed FileNode = %lu\n", useAfterFree);
	FspServiceLog(useAfterFree != 0 ? EVENTLOG_ERROR_TYPE : EVENTLOG_INFORMATION_TYPE,
	              (PWSTR)L"memefs diagnostics: D0 uses of a destroyed FileNode = %lu", useAfterFree);

	const ULONG maxInFlight = (ULONG)this->diagMaxLookupsInFlight.load();
	FspDebugLog("memefs diagnostics: D2 map writes = %lu, lookups = %lu, overlapped with a map write = %lu, "
	            "max concurrent lookups = %lu\n", epoch, lookups, overlapped, maxInFlight);
	FspServiceLog(overlapped != 0 ? EVENTLOG_WARNING_TYPE : EVENTLOG_INFORMATION_TYPE,
	              (PWSTR)L"memefs diagnostics: D2 map writes = %lu, lookups = %lu, overlapped = %lu",
	              epoch, lookups, overlapped);
#endif
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
	MEMFS_LOOKUP_GUARD();

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
