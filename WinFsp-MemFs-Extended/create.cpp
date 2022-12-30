#include <Windows.h>
#include <sddl.h>

#include "memfs.h"
#include "exceptions.h"
#include "utils.h"
#include "memfs-interface.h"

using namespace Memfs;

MemFs::MemFs(ULONG flags, UINT64 maxFsSize, const wchar_t* fileSystemName, const wchar_t* volumePrefix, const wchar_t* volumeLabel, const wchar_t* rootSddl) : maxFsSize(maxFsSize) {
	const bool caseInsensitive = !!(flags & MemfsCaseInsensitive);
	const bool flushAndPurgeOnCleanup = !!(flags & MemfsFlushAndPurgeOnCleanup);
	const bool supportsPosixUnlinkRename = !(flags & MemfsLegacyUnlinkRename);

	PCWSTR devicePath = MemfsNet == (flags & MemfsDeviceMask) ? L"" FSP_FSCTL_NET_DEVICE_NAME : L"" FSP_FSCTL_DISK_DEVICE_NAME;

	MEMFS_FILE_NODE* rootNode;
	PSECURITY_DESCRIPTOR rootSecurity;
	ULONG rootSecuritySize;
	BOOLEAN inserted;

	if (rootSddl == nullptr) {
		rootSddl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)";
	}
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(rootSddl, SDDL_REVISION_1,
	                                                          &rootSecurity, &rootSecuritySize)) {
		throw CreateException(FspNtStatusFromWin32(GetLastError()));
	}

	SectorInitialize();

	NTSTATUS status = MemfsFileNodeMapCreate(caseInsensitive, &Memfs->FileNodeMap);
	if (!NT_SUCCESS(status)) {
		LocalFree(rootSecurity);
		throw CreateException(status);
	}

	FSP_FSCTL_VOLUME_PARAMS volumeParams{
		.Version = sizeof(FSP_FSCTL_VOLUME_PARAMS),
		.SectorSize = MEMFS_SECTOR_SIZE,
		.SectorsPerAllocationUnit = MEMFS_SECTORS_PER_ALLOCATION_UNIT,
		.VolumeCreationTime = Utils::GetSystemTime(),
		.VolumeSerialNumber = static_cast<UINT32>(Utils::GetSystemTime() / (1010000ULL * 1000ULL)),
		.FileInfoTimeout = 0,
		.CaseSensitiveSearch = !caseInsensitive,
		.CasePreservedNames = true,
		.UnicodeOnDisk = true,
		.PersistentAcls = true,
		.ReparsePoints = true,
		.ReparsePointsAccessCheck = false,
		.NamedStreams = true,
		.PostCleanupWhenModifiedOnly = true,
		.PassQueryDirectoryFileName = true,
		.FlushAndPurgeOnCleanup = flushAndPurgeOnCleanup,
		.DeviceControl = 1,
		.ExtendedAttributes = true,
		.WslFeatures = true,
		.AllowOpenInKernelMode = true,
		.RejectIrpPriorToTransact0 = true,
		.SupportsPosixUnlinkRename = supportsPosixUnlinkRename
	};

	if (volumePrefix != nullptr) {
		wcscpy_s(volumeParams.Prefix, sizeof volumeParams.Prefix / sizeof(WCHAR), volumePrefix);
	}

	wcscpy_s(volumeParams.FileSystemName, sizeof volumeParams.FileSystemName / sizeof(WCHAR),
	         nullptr != fileSystemName ? fileSystemName : L"-MEMEFS");

	std::wstring devicePathMut{devicePath};
	status = FspFileSystemCreate(devicePathMut.data(), &volumeParams, &Interface::Interface, &this->fileSystem);
	if (!NT_SUCCESS(status)) {
		MemfsFileNodeMapDelete(Memfs->FileNodeMap);
		LocalFree(rootSecurity);
		throw CreateException(status);
	}

	this->fileSystem->UserContext = this;

	if (volumeLabel != nullptr) {
		this->volumeLabel = volumeLabel;
	}

	// Create root directory.

	status = MemfsFileNodeCreate(L"\\", &rootNode);
	if (!NT_SUCCESS(status)) {
		this->Destroy();
		LocalFree(rootSecurity);
		throw CreateException(status);
	}

	rootNode->FileInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;

	rootNode->FileSecurity = malloc(rootSecuritySize);
	if (rootNode->FileSecurity == nullptr) {
		MemfsFileNodeDelete(rootNode);
		this->Destroy();
		LocalFree(rootSecurity);
		throw CreateException(STATUS_INSUFFICIENT_RESOURCES);
	}
	rootNode->FileSecuritySize = rootSecuritySize;
	memcpy(rootNode->FileSecurity, rootSecurity, rootSecuritySize);

	status = MemfsFileNodeMapInsert(Memfs->FileNodeMap, rootNode, &inserted);
	if (!NT_SUCCESS(status)) {
		MemfsFileNodeDelete(rootNode);
		this->Destroy();
		LocalFree(rootSecurity);
		throw CreateException(status);
	}

	LocalFree(rootSecurity);
}

MemFs::~MemFs() {
	this->Destroy();
	// TODO: This might have a memory leak, because member variables are not deleted
}

void MemFs::Destroy() {
	if (this->fileSystem) {
		FspFileSystemDelete(this->fileSystem);
		this->fileSystem = nullptr;
	}
}

NTSTATUS MemFs::Start() const {
	return FspFileSystemStartDispatcher(this->fileSystem, 0);
}

void MemFs::Stop() const {
	FspFileSystemStopDispatcher(this->fileSystem);
}
