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


#include "globalincludes.h"
#include "exceptions.h"
#include "memfs.h"

using namespace Memfs;

static const std::wstring PROGNAME{L"memefs"};

#define LogInfo(format, ...)               FspServiceLog(EVENTLOG_INFORMATION_TYPE, (PWSTR)(format), __VA_ARGS__)
#define LogWarn(format, ...)               FspServiceLog(EVENTLOG_WARNING_TYPE, (PWSTR)(format), __VA_ARGS__)
#define LogFail(format, ...)               FspServiceLog(EVENTLOG_ERROR_TYPE, (PWSTR)(format), __VA_ARGS__)

#define argtos(v)                       if (arge > ++argp) v = *argp; else goto usage
#define argtol(v)                       if (arge > ++argp) v = wcstol_deflt(*argp, v); else goto usage
#define argtoll(v)                      if (arge > ++argp) v = wcstoll_deflt(*argp, v); else goto usage

static ULONG wcstol_deflt(wchar_t* w, ULONG deflt) {
	wchar_t* endp;
	ULONG ul = wcstol(w, &endp, 0);
	return L'\0' != w[0] && L'\0' == *endp ? ul : deflt;
}

static UINT64 wcstoll_deflt(wchar_t* w, UINT64 deflt) {
	wchar_t* endp;
	UINT64 ull = wcstoll(w, &endp, 0);
	return L'\0' != w[0] && L'\0' == *endp ? ull : deflt;
}

// There can only be one memfs
static std::unique_ptr<MemFs> GlobalMemFs;

NTSTATUS SvcStart(FSP_SERVICE* service, ULONG argc, PWSTR* argv) {
	wchar_t **argp, **arge;

	ULONG debugFlags{0};
	PWSTR debugLogFile{0};

	ULONG flags{MemfsDisk};
	ULONG otherFlags{0};

	ULONG fileInfoTimeout{0}; // memefs: Used to be INFINITE
	UINT64 maxFsSize{0};

	PWSTR fileSystemName{};
	PWSTR mountPoint{};
	PWSTR volumePrefix{};
	PWSTR rootSddl{};
	PWSTR volumeLabel{};

	HANDLE debugLogHandle{INVALID_HANDLE_VALUE};

	NTSTATUS result{-1};
	MemFs* memfs{};

	for (argp = argv + 1, arge = argv + argc; arge > argp; argp++) {
		if (L'-' != argp[0][0])
			break;
		switch (argp[0][1]) {
		case L'?':
			goto usage;
		case L'd':
			argtol(debugFlags);
			break;
		case L'D':
			argtos(debugLogFile);
			break;
		case L'f':
			otherFlags = MemfsFlushAndPurgeOnCleanup;
			break;
		case L'F':
			argtos(fileSystemName);
			break;
		case L'i':
			otherFlags = MemfsCaseInsensitive;
			break;
		case L'm':
			argtos(mountPoint);
			break;
		case L'S':
			argtos(rootSddl);
			break;
		case L's':
			// memefs
			argtoll(maxFsSize);
			break;
		case L'u':
			argtos(volumePrefix);
			if (nullptr != volumePrefix && L'\0' != volumePrefix[0])
				flags = MemfsNet;
			break;
		case L'l':
			argtos(volumeLabel);
			break;
		default:
			goto usage;
		}
	}

	if (arge > argp)
		goto usage;

	if (MemfsDisk == flags && 0 == mountPoint)
		goto usage;

	if (nullptr != debugLogFile) {
		if (0 == wcscmp(L"-", debugLogFile))
			debugLogHandle = GetStdHandle(STD_ERROR_HANDLE);
		else
			debugLogHandle = CreateFileW(
				debugLogFile,
				FILE_APPEND_DATA,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				nullptr,
				OPEN_ALWAYS,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);
		if (INVALID_HANDLE_VALUE == debugLogHandle) {
			LogFail(L"cannot open debug log file");
			goto usage;
		}

		FspDebugLogSetHandle(debugLogHandle);
	}

	try {
		GlobalMemFs = std::unique_ptr<MemFs>(new MemFs(flags | otherFlags, maxFsSize, fileSystemName, volumePrefix, volumeLabel, rootSddl));
	} catch (CreateException& _) {
		LogFail(L"cannot create MEMFS");
		goto exit;
	}

	memfs = GlobalMemFs.get();

	FSP_FILE_SYSTEM* rawFileSystem = memfs->GetRawFileSystem();
	FspFileSystemSetDebugLog(rawFileSystem, debugFlags);

	if (nullptr != mountPoint && L'\0' != mountPoint[0]) {
		result = FspFileSystemSetMountPoint(rawFileSystem,
		                                    L'*' == mountPoint[0] && L'\0' == mountPoint[1] ? nullptr : mountPoint);
		if (!NT_SUCCESS(result)) {
			LogFail(L"cannot mount MEMFS");
			goto exit;
		}
	}

	result = memfs->Start();
	if (!NT_SUCCESS(result)) {
		LogFail(L"cannot start MEMFS");
		goto exit;
	}

	mountPoint = FspFileSystemMountPoint(rawFileSystem);

	LogInfo(L"%s -s %lu%s%s%s%s%s%s%s%s",
	        PROGNAME.c_str(), maxFsSize,
	        rootSddl ? L" -S " : L"", rootSddl ? rootSddl : L"",
	        nullptr != volumePrefix && L'\0' != volumePrefix[0] ? L" -u " : L"",
	        nullptr != volumePrefix && L'\0' != volumePrefix[0] ? volumePrefix : L"",
	        mountPoint ? L" -m " : L"", mountPoint ? mountPoint : L"",
	        nullptr != volumeLabel && L'\0' != volumeLabel[0] ? L" -l " : L"",
	        nullptr != volumeLabel && L'\0' != volumeLabel[0] ? volumeLabel : L"");

	service->UserContext = memfs;
	result = STATUS_SUCCESS;

exit:
	if (!NT_SUCCESS(result) && memfs != nullptr) {
		memfs->Destroy();
	}

	return result;

usage: {
		// TODO: Ensure that the memory is never swapped out
		static wchar_t usage[] = L""
			L"usage: %s OPTIONS\n"
			L"\n"
			L"options:\n"
			L"    -d DebugFlags       [-1: enable all debug logs]\n"
			L"    -D DebugLogFile     [file path; use - for stderr]\n"
			L"    -i                  [case insensitive file system]\n"
			L"    -f                  [flush and purge cache on cleanup]\n"
			L"    -s MaxFsSize        [bytes of maximum total memory size]\n"
			L"    -F FileSystemName\n"
			L"    -S RootSddl         [file rights: FA, etc; NO generic rights: GA, etc.]\n"
			L"    -u \\Server\\Share  [UNC prefix (single backslash)]\n"
			L"    -m MountPoint       [X:|* (required if no UNC prefix)]\n"
			L"    -l VolumeLabel      [optional volume label name]\n";

		LogFail(usage, PROGNAME.c_str());
	}

	return STATUS_UNSUCCESSFUL;
}

NTSTATUS SvcStop(FSP_SERVICE* service) {
	MemFs* memfs = static_cast<MemFs*>(service->UserContext);

	memfs->Stop();
	memfs->Destroy();

	GlobalMemFs.reset();
	return STATUS_SUCCESS;
}

int wmain(int argc, wchar_t** argv) {
	std::wstring progName{PROGNAME};

	return FspServiceRun(progName.data(), SvcStart, SvcStop, nullptr);
}
