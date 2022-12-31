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


#if defined(MEMFS_NAMED_STREAMS)
typedef struct _MEMFS_GET_STREAM_INFO_CONTEXT
{
    PVOID Buffer;
    ULONG Length;
    PULONG PBytesTransferred;
} MEMFS_GET_STREAM_INFO_CONTEXT;



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
