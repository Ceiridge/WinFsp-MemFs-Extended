#pragma once

#include <winfsp/winfsp.h>

#include "memfs.h"

namespace Memfs::Interface {
	static constexpr UINT16 MAX_VOLUME_LABEL_STR_LENGTH = 32;

	static inline MemFs* GetMemFs(const FSP_FILE_SYSTEM* fileSystem) {
		return static_cast<MemFs*>(fileSystem->UserContext);
	}

	static inline FileNode* GetFileNode(const PVOID fileNode0) {
		return static_cast<FileNode*>(fileNode0);
	}


	static NTSTATUS GetVolumeInfo(FSP_FILE_SYSTEM* fileSystem, FSP_FSCTL_VOLUME_INFO* volumeInfo);

	static NTSTATUS SetVolumeLabel(FSP_FILE_SYSTEM* fileSystem, PWSTR volumeLabel, FSP_FSCTL_VOLUME_INFO* volumeInfo);

	static NTSTATUS GetSecurityByName(FSP_FILE_SYSTEM* fileSystem, PWSTR fileName, PUINT32 pFileAttributes, PSECURITY_DESCRIPTOR securityDescriptor, SIZE_T* pSecurityDescriptorSize);

	static NTSTATUS Create(FSP_FILE_SYSTEM* fileSystem,
	                       PWSTR fileName, UINT32 createOptions, UINT32 grantedAccess,
	                       UINT32 fileAttributes, PSECURITY_DESCRIPTOR securityDescriptor, UINT64 allocationSize,
	                       PVOID extraBuffer, ULONG extraLength, BOOLEAN extraBufferIsReparsePoint,
	                       PVOID* pFileNode, FSP_FSCTL_FILE_INFO* fileInfo);

	static NTSTATUS Open(FSP_FILE_SYSTEM* fileSystem,
	                     PWSTR fileName, UINT32 createOptions, UINT32 grantedAccess,
	                     PVOID* pFileNode, FSP_FSCTL_FILE_INFO* fileInfo);

	static NTSTATUS Overwrite(FSP_FILE_SYSTEM* fileSystem,
	                          PVOID fileNode0, UINT32 fileAttributes, BOOLEAN replaceFileAttributes, UINT64 allocationSize,
	                          PFILE_FULL_EA_INFORMATION ea, ULONG eaLength,
	                          FSP_FSCTL_FILE_INFO* fileInfo);

	static VOID Cleanup(FSP_FILE_SYSTEM* fileSystem,
	                    PVOID fileNode0, PWSTR fileName, ULONG flags);

	static VOID Close(FSP_FILE_SYSTEM* fileSystem,
	                  PVOID fileNode0);

	static NTSTATUS Read(FSP_FILE_SYSTEM* fileSystem,
	                     PVOID fileNode0, PVOID buffer, UINT64 offset, ULONG length,
	                     PULONG pBytesTransferred);

	static NTSTATUS Write(FSP_FILE_SYSTEM* fileSystem,
	                      PVOID fileNode0, PVOID buffer, UINT64 offset, ULONG length,
	                      BOOLEAN writeToEndOfFile, BOOLEAN constrainedIo,
	                      PULONG pBytesTransferred, FSP_FSCTL_FILE_INFO* fileInfo);

	NTSTATUS Flush(FSP_FILE_SYSTEM* fileSystem,
	               PVOID fileNode0,
	               FSP_FSCTL_FILE_INFO* fileInfo);

	static NTSTATUS GetFileInfo(FSP_FILE_SYSTEM* fileSystem,
	                            PVOID fileNode0,
	                            FSP_FSCTL_FILE_INFO* fileInfo);

	static NTSTATUS SetBasicInfo(FSP_FILE_SYSTEM* fileSystem,
	                             PVOID fileNode0, UINT32 fileAttributes,
	                             UINT64 creationTime, UINT64 lastAccessTime, UINT64 lastWriteTime, UINT64 changeTime,
	                             FSP_FSCTL_FILE_INFO* fileInfo);

	static NTSTATUS SetFileSize(FSP_FILE_SYSTEM* fileSystem,
	                            PVOID fileNode0, UINT64 newSize, BOOLEAN setAllocationSize,
	                            FSP_FSCTL_FILE_INFO* fileInfo);

	static NTSTATUS CanDelete(FSP_FILE_SYSTEM* fileSystem,
	                          PVOID fileNode0, PWSTR fileName);

	static NTSTATUS Rename(FSP_FILE_SYSTEM* fileSystem,
	                       PVOID fileNode0,
	                       PWSTR fileName, PWSTR newFileName, BOOLEAN replaceIfExists);

	static NTSTATUS GetSecurity(FSP_FILE_SYSTEM* fileSystem,
	                            PVOID fileNode0,
	                            PSECURITY_DESCRIPTOR securityDescriptor, SIZE_T* pSecurityDescriptorSize);

	static NTSTATUS SetSecurity(FSP_FILE_SYSTEM* fileSystem,
	                            PVOID fileNode0,
	                            SECURITY_INFORMATION securityInformation, PSECURITY_DESCRIPTOR modificationDescriptor);


	static NTSTATUS ReadDirectory(FSP_FILE_SYSTEM* fileSystem,
	                              PVOID fileNode0, PWSTR pattern, PWSTR marker,
	                              PVOID buffer, ULONG length, PULONG pBytesTransferred);

	static NTSTATUS GetDirInfoByName(FSP_FILE_SYSTEM* fileSystem,
	                                 PVOID parentNode0, PWSTR fileName,
	                                 FSP_FSCTL_DIR_INFO* dirInfo);

	static NTSTATUS ResolveReparsePoints(FSP_FILE_SYSTEM* fileSystem,
	                                     PWSTR fileName, UINT32 reparsePointIndex, BOOLEAN resolveLastPathComponent,
	                                     PIO_STATUS_BLOCK pIoStatus, PVOID buffer, PSIZE_T pSize);

	static NTSTATUS GetReparsePoint(FSP_FILE_SYSTEM* fileSystem,
	                                PVOID fileNode0,
	                                PWSTR fileName, PVOID buffer, PSIZE_T pSize);

	static NTSTATUS SetReparsePoint(FSP_FILE_SYSTEM* fileSystem,
	                                PVOID fileNode0,
	                                PWSTR fileName, PVOID buffer, SIZE_T size);

	static NTSTATUS DeleteReparsePoint(FSP_FILE_SYSTEM* fileSystem,
	                                   PVOID fileNode0,
	                                   PWSTR fileName, PVOID buffer, SIZE_T size);

	static NTSTATUS GetStreamInfo(FSP_FILE_SYSTEM* fileSystem,
	                              PVOID fileNode0, PVOID buffer, ULONG length,
	                              PULONG pBytesTransferred);

	static NTSTATUS Control(FSP_FILE_SYSTEM* fileSystem,
	                        PVOID fileNode, UINT32 controlCode,
	                        PVOID inputBuffer, ULONG inputBufferLength,
	                        PVOID outputBuffer, ULONG outputBufferLength, PULONG pBytesTransferred);

	static NTSTATUS GetEa(FSP_FILE_SYSTEM* fileSystem,
	                      PVOID fileNode0,
	                      PFILE_FULL_EA_INFORMATION ea, ULONG eaLength, PULONG pBytesTransferred);

	static NTSTATUS SetEa(FSP_FILE_SYSTEM* fileSystem,
	                      PVOID fileNode0,
	                      PFILE_FULL_EA_INFORMATION ea, ULONG eaLength,
	                      FSP_FSCTL_FILE_INFO* fileInfo);

	static FSP_FILE_SYSTEM_INTERFACE Interface{
		GetVolumeInfo,
		SetVolumeLabel,
		GetSecurityByName,
		nullptr,
		Open,
		nullptr,
		Cleanup,
		Close,
		Read,
		Write,
		Flush,
		GetFileInfo,
		SetBasicInfo,
		SetFileSize,
		CanDelete,
		Rename,
		GetSecurity,
		SetSecurity,
		ReadDirectory,
		ResolveReparsePoints,
		GetReparsePoint,
		SetReparsePoint,
		DeleteReparsePoint,
		GetStreamInfo,
		GetDirInfoByName,
		Control,
		nullptr,
		Create,
		Overwrite,
		GetEa,
		SetEa
	};
}
