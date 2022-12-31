 /*
  * This file is part of WinFsp.
  *
  * You can redistribute it and/or modify it under the terms of the GNU
  * General Public License version 3 as published by the Free Software
  * Foundation.
  *
  * Licensees holding a valid commercial license may use this software
  * in accordance with the commercial license agreement provided in
  * conjunction with the software.  The terms and conditions of any such
  * commercial license agreement shall govern, supersede, and render
  * ineffective any application of the GPLv3 license to this software,
  * notwithstanding of any reference thereto in the software or
  * associated repository.
  */

// #undef _DEBUG
#include "memfs.h"
#include <sddl.h>
#include <VersionHelpers.h>
#include <cassert>
#include <map>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

  /* SLOWIO */
#include <thread>

#define MEMFS_MAX_PATH                  512
FSP_FSCTL_STATIC_ASSERT(MEMFS_MAX_PATH > MAX_PATH,
    "MEMFS_MAX_PATH must be greater than MAX_PATH.");

/*
 * Define the MEMFS_STANDALONE macro when building MEMFS as a standalone file system.
 * This macro should be defined in the Visual Studio project settings, Makefile, etc.
 */
#define MEMFS_STANDALONE

 /*
  * Define the MEMFS_NAME_NORMALIZATION macro to include name normalization support.
  */
#define MEMFS_NAME_NORMALIZATION

  /*
   * Define the MEMFS_REPARSE_POINTS macro to include reparse points support.
   */
#define MEMFS_REPARSE_POINTS

   /*
    * Define the MEMFS_NAMED_STREAMS macro to include named streams support.
    * Named streams are not supported.
    */
#define MEMFS_NAMED_STREAMS

    /*
     * Define the MEMFS_DIRINFO_BY_NAME macro to include GetDirInfoByName.
     */
#define MEMFS_DIRINFO_BY_NAME

     /*
      * Define the MEMFS_SLOWIO macro to include delayed I/O response support.
      */
#define MEMFS_SLOWIO

      /*
       * Define the MEMFS_CONTROL macro to include DeviceControl support.
       */
#define MEMFS_CONTROL

       /*
        * Define the MEMFS_EA macro to include extended attributes support.
        */
#define MEMFS_EA

        /*
         * Define the MEMFS_WSL macro to include WSLinux support.
         */
#define MEMFS_WSL

/*
 * Use locks to ensure heap reinitialization thread safety
 * Disabled, because it does not seem to be a problem
 */
// #define MEMEFS_HEAP_LOCKS

         /*
          * Define the MEMFS_REJECT_EARLY_IRP macro to reject IRP's sent
          * to the file system prior to the dispatcher being started.
          */
#if defined(MEMFS_STANDALONE)
#define MEMFS_REJECT_EARLY_IRP
#endif

          /*
           * Define the DEBUG_BUFFER_CHECK macro on Windows 8 or above. This includes
           * a check for the Write buffer to ensure that it is read-only.
           *
           * Since ProcessBuffer support in the FSD, this is no longer a guarantee.
           */
#if !defined(NDEBUG)
           //#define DEBUG_BUFFER_CHECK
#endif

/*
 * MEMFS
 */

typedef std::map<PSTR, FILE_FULL_EA_INFORMATION*, MEMFS_FILE_NODE_EA_LESS> MEMFS_FILE_NODE_EA_MAP;

typedef struct _MEMFS_FILE_NODE
{
    WCHAR FileName[MEMFS_MAX_PATH];
    FSP_FSCTL_FILE_INFO FileInfo;
    SIZE_T FileSecuritySize;
    PVOID FileSecurity;
    // memefs: Sectors instead of LargeHeap
    MEMEFS_SECTOR_VECTOR FileDataSectors;
    std::mutex* FileDataSectorsMutex;

#if defined(MEMFS_REPARSE_POINTS)
    SIZE_T ReparseDataSize;
    PVOID ReparseData;
#endif
#if defined(MEMFS_EA)
    MEMFS_FILE_NODE_EA_MAP* EaMap;
#endif
    volatile LONG RefCount;
#if defined(MEMFS_NAMED_STREAMS)
    struct _MEMFS_FILE_NODE* MainFileNode;
#endif
} MEMFS_FILE_NODE;


typedef std::map<PWSTR, MEMFS_FILE_NODE*, MEMFS_FILE_NODE_LESS> MEMFS_FILE_NODE_MAP;

typedef struct _MEMFS
{
    FSP_FILE_SYSTEM* FileSystem;
    MEMFS_FILE_NODE_MAP* FileNodeMap;
    // memefs: Counter to track allocated memory
    volatile UINT64 AllocatedSectors;
    volatile UINT64 AllocatedSizesToBeDeleted;
    std::unordered_map<MEMFS_FILE_NODE*, UINT64>* ToBeDeletedFileNodeSizes;
    std::mutex* ToBeDeletedFileNodeSizesMutex;
    // memefs: MaxFsSize instead of max. file nodes and individual limits
    UINT64 MaxFsSize;
    UINT64 CachedMaxFsSize;
    UINT64 LastCacheTime;

#ifdef MEMFS_SLOWIO
    ULONG SlowioMaxDelay;
    ULONG SlowioPercentDelay;
    ULONG SlowioRarefyDelay;
    volatile LONG SlowioThreadsRunning;
#endif
    UINT16 VolumeLabelLength;
    WCHAR VolumeLabel[32];
} MEMFS;

// memefs: Use static global Memfs instance for easier access
static MEMFS* GlobalMemfs = 0;




typedef struct _MEMFS_FILE_NODE_MAP_ENUM_CONTEXT
{
    BOOLEAN Reference;
    MEMFS_FILE_NODE** FileNodes;
    ULONG Capacity, Count;
} MEMFS_FILE_NODE_MAP_ENUM_CONTEXT;



/*
 * FSP_FILE_SYSTEM_INTERFACE
 */


typedef struct _MEMFS_READ_DIRECTORY_CONTEXT
{
    PVOID Buffer;
    ULONG Length;
    PULONG PBytesTransferred;
} MEMFS_READ_DIRECTORY_CONTEXT;



#if defined(MEMFS_REPARSE_POINTS)
static NTSTATUS ResolveReparsePoints(FSP_FILE_SYSTEM* FileSystem,
    PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent,
    PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize)
{
    return FspFileSystemResolveReparsePoints(FileSystem, GetReparsePointByName, 0,
        FileName, ReparsePointIndex, ResolveLastPathComponent,
        PIoStatus, Buffer, PSize);
}

static NTSTATUS GetReparsePoint(FSP_FILE_SYSTEM* FileSystem,
    PVOID FileNode0,
    PWSTR FileName, PVOID Buffer, PSIZE_T PSize)
{
    MEMFS_FILE_NODE* FileNode = (MEMFS_FILE_NODE*)FileNode0;

#if defined(MEMFS_NAMED_STREAMS)
    if (0 != FileNode->MainFileNode)
        FileNode = FileNode->MainFileNode;
#endif

    if (0 == (FileNode->FileInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return STATUS_NOT_A_REPARSE_POINT;

    if (FileNode->ReparseDataSize > *PSize)
        return STATUS_BUFFER_TOO_SMALL;

    *PSize = FileNode->ReparseDataSize;
    memcpy(Buffer, FileNode->ReparseData, FileNode->ReparseDataSize);

    return STATUS_SUCCESS;
}

static NTSTATUS SetReparsePoint(FSP_FILE_SYSTEM* FileSystem,
    PVOID FileNode0,
    PWSTR FileName, PVOID Buffer, SIZE_T Size)
{
    MEMFS* Memfs = (MEMFS*)FileSystem->UserContext;
    MEMFS_FILE_NODE* FileNode = (MEMFS_FILE_NODE*)FileNode0;
    PVOID ReparseData;
    NTSTATUS Result;

#if defined(MEMFS_NAMED_STREAMS)
    if (0 != FileNode->MainFileNode)
        FileNode = FileNode->MainFileNode;
#endif

    if (MemfsFileNodeMapHasChild(Memfs->FileNodeMap, FileNode))
        return STATUS_DIRECTORY_NOT_EMPTY;

    if (0 != FileNode->ReparseData)
    {
        Result = FspFileSystemCanReplaceReparsePoint(
            FileNode->ReparseData, FileNode->ReparseDataSize,
            Buffer, Size);
        if (!NT_SUCCESS(Result))
            return Result;
    }

    ReparseData = realloc(FileNode->ReparseData, Size);
    if (0 == ReparseData && 0 != Size)
        return STATUS_INSUFFICIENT_RESOURCES;

    FileNode->FileInfo.FileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
    FileNode->FileInfo.ReparseTag = *(PULONG)Buffer;
    /* the first field in a reparse buffer is the reparse tag */
    FileNode->ReparseDataSize = Size;
    FileNode->ReparseData = ReparseData;
    memcpy(FileNode->ReparseData, Buffer, Size);

    return STATUS_SUCCESS;
}

static NTSTATUS DeleteReparsePoint(FSP_FILE_SYSTEM* FileSystem,
    PVOID FileNode0,
    PWSTR FileName, PVOID Buffer, SIZE_T Size)
{
    MEMFS_FILE_NODE* FileNode = (MEMFS_FILE_NODE*)FileNode0;
    NTSTATUS Result;

#if defined(MEMFS_NAMED_STREAMS)
    if (0 != FileNode->MainFileNode)
        FileNode = FileNode->MainFileNode;
#endif

    if (0 != FileNode->ReparseData)
    {
        Result = FspFileSystemCanReplaceReparsePoint(
            FileNode->ReparseData, FileNode->ReparseDataSize,
            Buffer, Size);
        if (!NT_SUCCESS(Result))
            return Result;
    }
    else
        return STATUS_NOT_A_REPARSE_POINT;

    free(FileNode->ReparseData);

    FileNode->FileInfo.FileAttributes &= ~FILE_ATTRIBUTE_REPARSE_POINT;
    FileNode->FileInfo.ReparseTag = 0;
    FileNode->ReparseDataSize = 0;
    FileNode->ReparseData = 0;

    return STATUS_SUCCESS;
}
#endif

#if defined(MEMFS_NAMED_STREAMS)
typedef struct _MEMFS_GET_STREAM_INFO_CONTEXT
{
    PVOID Buffer;
    ULONG Length;
    PULONG PBytesTransferred;
} MEMFS_GET_STREAM_INFO_CONTEXT;

static BOOLEAN AddStreamInfo(MEMFS_FILE_NODE* FileNode,
    PVOID Buffer, ULONG Length, PULONG PBytesTransferred)
{
    UINT8 StreamInfoBuf[sizeof(FSP_FSCTL_STREAM_INFO) + sizeof FileNode->FileName];
    FSP_FSCTL_STREAM_INFO* StreamInfo = (FSP_FSCTL_STREAM_INFO*)StreamInfoBuf;
    PWSTR StreamName;

    StreamName = wcschr(FileNode->FileName, L':');
    if (0 != StreamName)
        StreamName++;
    else
        StreamName = L"";

    StreamInfo->Size = (UINT16)(sizeof(FSP_FSCTL_STREAM_INFO) + wcslen(StreamName) * sizeof(WCHAR));
    StreamInfo->StreamSize = FileNode->FileInfo.FileSize;
    StreamInfo->StreamAllocationSize = FileNode->FileInfo.AllocationSize;
    memcpy(StreamInfo->StreamNameBuf, StreamName, StreamInfo->Size - sizeof(FSP_FSCTL_STREAM_INFO));

    return FspFileSystemAddStreamInfo(StreamInfo, Buffer, Length, PBytesTransferred);
}

static BOOLEAN GetStreamInfoEnumFn(MEMFS_FILE_NODE* FileNode, PVOID Context0)
{
    MEMFS_GET_STREAM_INFO_CONTEXT* Context = (MEMFS_GET_STREAM_INFO_CONTEXT*)Context0;

    return AddStreamInfo(FileNode,
        Context->Buffer, Context->Length, Context->PBytesTransferred);
}

static NTSTATUS GetStreamInfo(FSP_FILE_SYSTEM* FileSystem,
    PVOID FileNode0, PVOID Buffer, ULONG Length,
    PULONG PBytesTransferred)
{
    MEMFS* Memfs = (MEMFS*)FileSystem->UserContext;
    MEMFS_FILE_NODE* FileNode = (MEMFS_FILE_NODE*)FileNode0;
    MEMFS_GET_STREAM_INFO_CONTEXT Context;

    if (0 != FileNode->MainFileNode)
        FileNode = FileNode->MainFileNode;

    Context.Buffer = Buffer;
    Context.Length = Length;
    Context.PBytesTransferred = PBytesTransferred;

    if (0 == (FileNode->FileInfo.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !AddStreamInfo(FileNode, Buffer, Length, PBytesTransferred))
        return STATUS_SUCCESS;

    if (MemfsFileNodeMapEnumerateNamedStreams(Memfs->FileNodeMap, FileNode, GetStreamInfoEnumFn, &Context))
        FspFileSystemAddStreamInfo(0, Buffer, Length, PBytesTransferred);

    /* ???: how to handle out of response buffer condition? */

    return STATUS_SUCCESS;
}
#endif

#if defined(MEMFS_CONTROL)
static NTSTATUS Control(FSP_FILE_SYSTEM* FileSystem,
    PVOID FileNode, UINT32 ControlCode,
    PVOID InputBuffer, ULONG InputBufferLength,
    PVOID OutputBuffer, ULONG OutputBufferLength, PULONG PBytesTransferred)
{
    /* MEMFS also supports encryption! See below :) */
    if (CTL_CODE(0x8000 + 'M', 'R', METHOD_BUFFERED, FILE_ANY_ACCESS) == ControlCode)
    {
        if (OutputBufferLength != InputBufferLength)
            return STATUS_INVALID_PARAMETER;

        for (PUINT8 P = (PUINT8)InputBuffer, Q = (PUINT8)OutputBuffer, EndP = P + InputBufferLength;
            EndP > P; P++, Q++)
        {
            if (('A' <= *P && *P <= 'M') || ('a' <= *P && *P <= 'm'))
                *Q = *P + 13;
            else
                if (('N' <= *P && *P <= 'Z') || ('n' <= *P && *P <= 'z'))
                    *Q = *P - 13;
                else
                    *Q = *P;
        }

        *PBytesTransferred = InputBufferLength;
        return STATUS_SUCCESS;
    }

    return STATUS_INVALID_DEVICE_REQUEST;
}
#endif

#if defined(MEMFS_EA)
typedef struct _MEMFS_GET_EA_CONTEXT
{
    PFILE_FULL_EA_INFORMATION Ea;
    ULONG EaLength;
    PULONG PBytesTransferred;
} MEMFS_GET_EA_CONTEXT;

static BOOLEAN GetEaEnumFn(PFILE_FULL_EA_INFORMATION Ea, PVOID Context0)
{
    MEMFS_GET_EA_CONTEXT* Context = (MEMFS_GET_EA_CONTEXT*)Context0;

    return FspFileSystemAddEa(Ea,
        Context->Ea, Context->EaLength, Context->PBytesTransferred);
}

static NTSTATUS GetEa(FSP_FILE_SYSTEM* FileSystem,
    PVOID FileNode0,
    PFILE_FULL_EA_INFORMATION Ea, ULONG EaLength, PULONG PBytesTransferred)
{
    MEMFS_FILE_NODE* FileNode = (MEMFS_FILE_NODE*)FileNode0;
    MEMFS_GET_EA_CONTEXT Context;

    Context.Ea = Ea;
    Context.EaLength = EaLength;
    Context.PBytesTransferred = PBytesTransferred;

    if (MemfsFileNodeEnumerateEa(FileNode, GetEaEnumFn, &Context))
        FspFileSystemAddEa(0, Ea, EaLength, PBytesTransferred);

    return STATUS_SUCCESS;
}

static NTSTATUS SetEa(FSP_FILE_SYSTEM* FileSystem,
    PVOID FileNode0,
    PFILE_FULL_EA_INFORMATION Ea, ULONG EaLength,
    FSP_FSCTL_FILE_INFO* FileInfo)
{
    MEMFS_FILE_NODE* FileNode = (MEMFS_FILE_NODE*)FileNode0;
    NTSTATUS Result;

    Result = FspFileSystemEnumerateEa(FileSystem, MemfsFileNodeSetEa, FileNode, Ea, EaLength);
    if (!NT_SUCCESS(Result))
        return Result;

    MemfsFileNodeGetFileInfo(FileNode, FileInfo);

    return STATUS_SUCCESS;
}
#endif
