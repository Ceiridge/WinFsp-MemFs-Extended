#include "memfs-interface.h"

namespace Memfs::Interface {
	NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* fileSystem, FSP_FSCTL_VOLUME_INFO* volumeInfo) {
		MemFs* memfs = GetMemFs(fileSystem);

		// TODO: This is only called by the Explorer when a file is changed (maybe directory change notifier), causing this to report wrong sizes
		const UINT64 maxSize = memfs->CalculateMaxTotalSize();
		const UINT64 availableSize = memfs->CalculateAvailableTotalSize();

		volumeInfo->TotalSize = maxSize;
		volumeInfo->FreeSize = availableSize;

		const std::wstring& volumeLabel = memfs->GetVolumeLabel();
		volumeInfo->VolumeLabelLength = volumeLabel.size() * sizeof(std::wstring::value_type);
		memcpy_s(volumeInfo->VolumeLabel, MAX_VOLUME_LABEL_STR_LENGTH * sizeof(WCHAR), volumeLabel.data(), volumeInfo->VolumeLabelLength);

		return STATUS_SUCCESS;
	}

	NTSTATUS SetVolumeLabel(FSP_FILE_SYSTEM* fileSystem, PWSTR volumeLabel, FSP_FSCTL_VOLUME_INFO* volumeInfo) {
		MemFs* memfs = GetMemFs(fileSystem);
		memfs->SetVolumeLabel(volumeLabel);

		// memefs: Call GetVolumeInfo to avoid code duplication
		return GetVolumeInfo(fileSystem, volumeInfo);
	}
}
