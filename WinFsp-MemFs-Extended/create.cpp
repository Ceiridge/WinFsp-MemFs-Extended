#include "globalincludes.h"
#include "exceptions.h"
#include "utils.h"
#include "memfs.h"
#include "memfs-interface.h"

using namespace Memfs;

MemFs::MemFs(ULONG flags, UINT64 maxFsSize, const wchar_t* fileSystemName, const wchar_t* volumePrefix, const wchar_t* volumeLabel, const wchar_t* rootSddl) : maxFsSize(maxFsSize) {
	if (MEMFS_SINGLETON != nullptr) {
		throw std::runtime_error("There can only be one memfs.");
	}

	const bool caseInsensitive = !!(flags & MemfsCaseInsensitive);
	const bool flushAndPurgeOnCleanup = !!(flags & MemfsFlushAndPurgeOnCleanup);
	const bool supportsPosixUnlinkRename = !(flags & MemfsLegacyUnlinkRename);

	PCWSTR devicePath = MemfsNet == (flags & MemfsDeviceMask) ? L"" FSP_FSCTL_NET_DEVICE_NAME : L"" FSP_FSCTL_DISK_DEVICE_NAME;

	FileNode* rootNode;
	PSECURITY_DESCRIPTOR rootSecurity;
	ULONG rootSecuritySize;

	if (rootSddl == nullptr) {
		rootSddl = L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;WD)";
	}
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(rootSddl, SDDL_REVISION_1,
	                                                          &rootSecurity, &rootSecuritySize)) {
		throw CreateException(FspNtStatusFromWin32(GetLastError()));
	}

	this->fileMap = FileNodeMap(Utils::FileLess(caseInsensitive));

	// Cannot use initializer list
	FSP_FSCTL_VOLUME_PARAMS volumeParams{};
	volumeParams.Version = sizeof(FSP_FSCTL_VOLUME_PARAMS);
	volumeParams.SectorSize = MEMFS_SECTOR_SIZE;
	volumeParams.SectorsPerAllocationUnit = MEMFS_SECTORS_PER_ALLOCATION_UNIT;
	volumeParams.VolumeCreationTime = Utils::GetSystemTime();
	volumeParams.VolumeSerialNumber = static_cast<UINT32>(Utils::GetSystemTime() / (1010000ULL * 1000ULL));
	volumeParams.FileInfoTimeout = 0;
	volumeParams.CaseSensitiveSearch = !caseInsensitive;
	volumeParams.CasePreservedNames = true;
	volumeParams.UnicodeOnDisk = true;
	volumeParams.PersistentAcls = true;
	volumeParams.ReparsePoints = true;
	volumeParams.ReparsePointsAccessCheck = false;
	volumeParams.NamedStreams = true;
	volumeParams.PostCleanupWhenModifiedOnly = true;
	volumeParams.PassQueryDirectoryFileName = true;
	volumeParams.ExtendedAttributes = true;
	volumeParams.FlushAndPurgeOnCleanup = flushAndPurgeOnCleanup;
	volumeParams.DeviceControl = 1;
	volumeParams.WslFeatures = true;
	volumeParams.AllowOpenInKernelMode = true;
	volumeParams.RejectIrpPriorToTransact0 = true;
	volumeParams.SupportsPosixUnlinkRename = supportsPosixUnlinkRename;

	if (volumePrefix != nullptr) {
		wcscpy_s(volumeParams.Prefix, sizeof volumeParams.Prefix / sizeof(WCHAR), volumePrefix);
	}

	wcscpy_s(volumeParams.FileSystemName, sizeof volumeParams.FileSystemName / sizeof(WCHAR),
	         nullptr != fileSystemName ? fileSystemName : L"-MEMEFS");

	std::wstring devicePathMut{devicePath};

	FSP_FILE_SYSTEM* fileSystemReceiver;
	NTSTATUS status = FspFileSystemCreate(devicePathMut.data(), &volumeParams, &Interface::Interface, &fileSystemReceiver);
	if (!NT_SUCCESS(status)) {
		LocalFree(rootSecurity);
		throw CreateException(status);
	}

	this->fileSystem = std::unique_ptr<FSP_FILE_SYSTEM>(fileSystemReceiver);
	this->fileSystem->UserContext = this;

	if (volumeLabel != nullptr) {
		this->volumeLabel = volumeLabel;
	}

	// Create root directory.

	FileNode rootNodeVal(L"\\");
	rootNode = &rootNodeVal;

	rootNode->fileInfo.FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
	rootNode->fileSecurity = DynamicStruct<SECURITY_DESCRIPTOR>(rootSecuritySize);

	memcpy_s(rootNode->fileSecurity.Struct(), rootNode->fileSecurity.ByteSize(), rootSecurity, rootSecuritySize);

	const auto [insertStatus, _] = this->InsertNode(std::move(rootNodeVal));
	status = insertStatus;
	if (!NT_SUCCESS(status)) {
		this->Destroy();
		LocalFree(rootSecurity);
		throw CreateException(status);
	}

	LocalFree(rootSecurity);
	MEMFS_SINGLETON = this;
}

MemFs::~MemFs() {
	this->Destroy();
}

void MemFs::Destroy() {
	if (this->fileSystem) {
		FspFileSystemDelete(this->fileSystem.get());
		this->fileSystem = nullptr;
	}
}

NTSTATUS MemFs::Start() const {
	return FspFileSystemStartDispatcher(this->fileSystem.get(), 0);
}

void MemFs::Stop() const {
	FspFileSystemStopDispatcher(this->fileSystem.get());
}
