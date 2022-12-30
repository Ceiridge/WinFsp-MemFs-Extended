#include "memfs-interface.h"

namespace Memfs::Interface {
	static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* fileSystem, FSP_FSCTL_VOLUME_INFO* volumeInfo) {
		MemFs* memfs = GetMemFs(fileSystem);

		// TODO: Fix toBeDeleted stuff
		const UINT64 toBeDeletedSize = 0ULL; //InterlockedExchangeAdd(&Memfs->AllocatedSizesToBeDeleted, 0ULL);
		const UINT64 maxSize = memfs->CalculateMaxTotalSize();
		const UINT64 availableSize = memfs->CalculateAvailableTotalSize() + toBeDeletedSize;

		volumeInfo->TotalSize = maxSize;
		volumeInfo->FreeSize = availableSize;

		const std::wstring& volumeLabel = memfs->GetVolumeLabel();
		volumeInfo->VolumeLabelLength = volumeLabel.capacity(); // TODO: This might cause issues
		memcpy_s(volumeInfo->VolumeLabel, MAX_VOLUME_LABEL_STR_LENGTH * sizeof(WCHAR), volumeLabel.data(), volumeLabel.capacity());

		return STATUS_SUCCESS;
	}

	static NTSTATUS SetVolumeLabel(FSP_FILE_SYSTEM* fileSystem, PWSTR volumeLabel, FSP_FSCTL_VOLUME_INFO* volumeInfo) {
		MemFs* memfs = GetMemFs(fileSystem);
		memfs->SetVolumeLabel(volumeLabel);

		// memefs: Call GetVolumeInfo to avoid code duplication
		return GetVolumeInfo(fileSystem, volumeInfo);
	}
}
