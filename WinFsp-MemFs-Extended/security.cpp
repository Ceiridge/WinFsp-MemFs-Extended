#include "memfs-interface.h"
#include "memfs.h"

namespace Memfs::Interface {
	static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* fileSystem, PWSTR fileName, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR securityDescriptor, SIZE_T* pSecurityDescriptorSize) {
		MemFs memfs = GetMemFs(fileSystem);
		NTSTATUS result;

		MEMFS_FILE_NODE* fileNode = MemfsFileNodeMapGet(memfs->FileNodeMap, fileName);

		if (fileNode == nullptr) {
			result = STATUS_OBJECT_NAME_NOT_FOUND;

			if (FspFileSystemFindReparsePoint(fileSystem, GetReparsePointByName, nullptr,
			                                  fileName, pFileAttributes)) {
				result = STATUS_REPARSE;
			} else {
				MemfsFileNodeMapGetParent(memfs->FileNodeMap, fileName, &result);
			}

			return result;
		}

		UINT32 fileAttributesMask = ~(UINT32)0;
		if (fileNode->MainFileNode != nullptr) {
			fileAttributesMask = ~(UINT32)FILE_ATTRIBUTE_DIRECTORY;
			fileNode = fileNode->MainFileNode;
		}

		if (pFileAttributes != nullptr) {
			*pFileAttributes = fileNode->FileInfo.FileAttributes & fileAttributesMask;
		}

		if (pSecurityDescriptorSize != nullptr) {
			if (fileNode->FileSecuritySize > *pSecurityDescriptorSize) {
				*pSecurityDescriptorSize = fileNode->FileSecuritySize;
				return STATUS_BUFFER_OVERFLOW;
			}

			*pSecurityDescriptorSize = fileNode->FileSecuritySize;
			if (securityDescriptor != nullptr) {
				memcpy_s(securityDescriptor, sizeof(SECURITY_DESCRIPTOR), fileNode->FileSecurity, fileNode->FileSecuritySize);
			}
		}

		return STATUS_SUCCESS;
	}

	// This code is slightly duplicated :)
	static NTSTATUS GetSecurity(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0,
	                            PSECURITY_DESCRIPTOR securityDescriptor, SIZE_T* pSecurityDescriptorSize) {
		MEMFS_FILE_NODE* fileNode = (MEMFS_FILE_NODE*)FileNode0;

		if (fileNode->MainFileNode != nullptr) {
			fileNode = fileNode->MainFileNode;
		}

		if (fileNode->FileSecuritySize > *pSecurityDescriptorSize) {
			*pSecurityDescriptorSize = fileNode->FileSecuritySize;
			return STATUS_BUFFER_OVERFLOW;
		}

		*pSecurityDescriptorSize = fileNode->FileSecuritySize;
		if (securityDescriptor != nullptr) {
			memcpy_s(securityDescriptor, sizeof(SECURITY_DESCRIPTOR), fileNode->FileSecurity, fileNode->FileSecuritySize);
		}

		return STATUS_SUCCESS;
	}

	static NTSTATUS SetSecurity(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode0,
	                            SECURITY_INFORMATION securityInformation, PSECURITY_DESCRIPTOR modificationDescriptor) {
		MEMFS_FILE_NODE* fileNode = (MEMFS_FILE_NODE*)FileNode0;
		PSECURITY_DESCRIPTOR newSecurityDescriptor;

		if (fileNode->MainFileNode != nullptr) {
			fileNode = fileNode->MainFileNode;
		}

		NTSTATUS result = FspSetSecurityDescriptor(
			fileNode->FileSecurity,
			SecurityInformation,
			ModificationDescriptor,
			&newSecurityDescriptor);
		if (!NT_SUCCESS(result)) {
			return result;
		}

		SIZE_T fileSecuritySize = GetSecurityDescriptorLength(newSecurityDescriptor);
		PSECURITY_DESCRIPTOR fileSecurity = (PSECURITY_DESCRIPTOR)malloc(fileSecuritySize); // TODO: NO! NO NONO NONO
		if (fileSecurity == nullptr) {
			FspDeleteSecurityDescriptor(newSecurityDescriptor, (NTSTATUS(*)())FspSetSecurityDescriptor);
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		memcpy(fileSecurity, newSecurityDescriptor, fileSecuritySize); // TODO: NO!
		FspDeleteSecurityDescriptor(newSecurityDescriptor, (NTSTATUS(*)())FspSetSecurityDescriptor);

		free(fileNode->FileSecurity); // TODO: ALSO NO
		fileNode->FileSecuritySize = fileSecuritySize;
		fileNode->FileSecurity = fileSecurity;

		return STATUS_SUCCESS;
	}
}
