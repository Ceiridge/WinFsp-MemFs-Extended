#include "memfs-interface.h"

namespace Memfs::Interface {
	NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* fileSystem, PWSTR fileName, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR securityDescriptor, SIZE_T* pSecurityDescriptorSize) {
		MemFs* memfs = GetMemFs(fileSystem);
		NTSTATUS result;

		const auto fileNodeOpt = memfs->FindFile(fileName);
		if (!fileNodeOpt.has_value()) {
			result = STATUS_OBJECT_NAME_NOT_FOUND;

			if (FspFileSystemFindReparsePoint(fileSystem, CompatGetReparsePointByName, nullptr,
			                                  fileName, pFileAttributes)) {
				result = STATUS_REPARSE;
			} else {
				const auto [parentResult, _] = memfs->FindParent(fileName);
				if (parentResult != STATUS_SUCCESS) {
					result = parentResult;
				}
			}

			return result;
		}
		FileNode* fileNode = &fileNodeOpt.value().get();
		std::shared_ptr<FileNode> mainFileNodeShared;

		UINT32 fileAttributesMask = ~(UINT32)0;
		if (!fileNode->IsMainNode()) {
			fileAttributesMask = ~(UINT32)FILE_ATTRIBUTE_DIRECTORY;

			mainFileNodeShared = fileNode->GetMainNode().lock();
			fileNode = mainFileNodeShared.get();
		}

		if (pFileAttributes != nullptr) {
			*pFileAttributes = fileNode->fileInfo.FileAttributes & fileAttributesMask;
		}

		if (pSecurityDescriptorSize != nullptr) {
			if (fileNode->fileSecurity.WantedByteSize() > *pSecurityDescriptorSize) {
				*pSecurityDescriptorSize = fileNode->fileSecurity.WantedByteSize();
				return STATUS_BUFFER_OVERFLOW;
			}

			*pSecurityDescriptorSize = fileNode->fileSecurity.WantedByteSize();
			if (securityDescriptor != nullptr) {
				memcpy_s(securityDescriptor, *pSecurityDescriptorSize, fileNode->fileSecurity.Struct(), fileNode->fileSecurity.WantedByteSize());
			}
		}

		return STATUS_SUCCESS;
	}

	// This code is slightly duplicated :)
	NTSTATUS GetSecurity(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0,
	                            PSECURITY_DESCRIPTOR securityDescriptor, SIZE_T* pSecurityDescriptorSize) {
		FileNode* fileNode = GetFileNode(fileNode0);
		std::shared_ptr<FileNode> mainFileNodeShared;

		if (!fileNode->IsMainNode()) {
			mainFileNodeShared = fileNode->GetMainNode().lock();
			fileNode = mainFileNodeShared.get();
		}

		if (fileNode->fileSecurity.WantedByteSize() > *pSecurityDescriptorSize) {
			*pSecurityDescriptorSize = fileNode->fileSecurity.WantedByteSize();
			return STATUS_BUFFER_OVERFLOW;
		}

		*pSecurityDescriptorSize = fileNode->fileSecurity.WantedByteSize();
		if (securityDescriptor != nullptr) {
			memcpy_s(securityDescriptor, *pSecurityDescriptorSize, fileNode->fileSecurity.Struct(), fileNode->fileSecurity.WantedByteSize());
		}

		return STATUS_SUCCESS;
	}

	NTSTATUS SetSecurity(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0,
	                            SECURITY_INFORMATION securityInformation, PSECURITY_DESCRIPTOR modificationDescriptor) {
		FileNode* fileNode = GetFileNode(fileNode0);
		std::shared_ptr<FileNode> mainFileNodeShared;

		PSECURITY_DESCRIPTOR newSecurityDescriptor;

		if (!fileNode->IsMainNode()) {
			mainFileNodeShared = fileNode->GetMainNode().lock();
			fileNode = mainFileNodeShared.get();
		}

		NTSTATUS result = FspSetSecurityDescriptor(
			fileNode->fileSecurity.Struct(),
			securityInformation,
			modificationDescriptor,
			&newSecurityDescriptor);
		if (!NT_SUCCESS(result)) {
			return result;
		}

		const SIZE_T fileSecuritySize = GetSecurityDescriptorLength(newSecurityDescriptor);
		try {
			fileNode->fileSecurity = DynamicStruct<SECURITY_DESCRIPTOR>(fileSecuritySize);

			memcpy_s(fileNode->fileSecurity.Struct(), fileNode->fileSecurity.ByteSize(), newSecurityDescriptor, fileSecuritySize);
			FspDeleteSecurityDescriptor(newSecurityDescriptor, (NTSTATUS(*)())FspSetSecurityDescriptor);
		} catch (...) {
			FspDeleteSecurityDescriptor(newSecurityDescriptor, (NTSTATUS(*)())FspSetSecurityDescriptor);
			return STATUS_INSUFFICIENT_RESOURCES;
		}

		return STATUS_SUCCESS;
	}
}
