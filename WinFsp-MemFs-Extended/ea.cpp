#include <ranges>

#include "memfs-interface.h"

namespace Memfs::Interface {
	NTSTATUS GetEa(FSP_FILE_SYSTEM* fileSystem,
	                      PVOID fileNode0,
	                      PFILE_FULL_EA_INFORMATION ea, ULONG eaLength, PULONG pBytesTransferred) {
		FileNode* fileNode = GetFileNode(fileNode0);

		const auto eaMapOpt = fileNode->GetEaMapOpt();
		if (eaMapOpt.has_value()) {
			const auto& eaMap = eaMapOpt.value().get();

			for (const auto eaEntry : eaMap | std::views::values) {
				if (!FspFileSystemAddEa((PFILE_FULL_EA_INFORMATION)eaEntry.Struct(), ea, eaLength, pBytesTransferred)) {
					return STATUS_SUCCESS; // Without end
				}
			}
		}

		FspFileSystemAddEa(nullptr, ea, eaLength, pBytesTransferred); // List end
		return STATUS_SUCCESS;
	}

	NTSTATUS SetEa(FSP_FILE_SYSTEM* fileSystem,
	                      PVOID fileNode0,
	                      PFILE_FULL_EA_INFORMATION ea, ULONG eaLength,
	                      FSP_FSCTL_FILE_INFO* fileInfo) {
		FileNode* fileNode = GetFileNode(fileNode0);

		const NTSTATUS result = FspFileSystemEnumerateEa(fileSystem, CompatFspFileNodeSetEa, fileNode, ea, eaLength);
		if (!NT_SUCCESS(result)) {
			return result;
		}

		fileNode->CopyFileInfo(fileInfo);
		return STATUS_SUCCESS;
	}
}
