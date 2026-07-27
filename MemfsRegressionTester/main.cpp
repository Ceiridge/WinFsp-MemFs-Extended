#include "tests.h"
#include "testlib.h"

#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace TestLib;

namespace {
	void Usage() {
		wprintf(
			L"usage: MemfsRegressionTester.exe <target-directory> [options]\n"
			L"\n"
			L"  Drives a directory through the regression tests T1..T7 using plain Win32 APIs.\n"
			L"  Point it at an NTFS directory for a control run and at a memefs mount point to\n"
			L"  test the file system. Exit code = number of failed checks.\n"
			L"\n"
			L"options:\n"
			L"  --tests=T1,T3,...    only run the listed tests (default: all)\n"
			L"  --mass-files=N       number of files for T5 (default: 20000)\n"
			L"  --mass-dirs=N        number of subdirectories for T5 (default: 50)\n"
			L"  --stress-seconds=N   duration of T7 (default: 60)\n"
			L"  --sd-scan-limit=N    max files per T2 sweep (default: 400)\n"
			L"  --provoke-rounds=N   rounds for T8 (default: 200)\n"
			L"  --keep               do not delete the work directory afterwards\n");
	}

	bool StartsWith(const std::wstring& s, const wchar_t* prefix) {
		const size_t len = wcslen(prefix);
		return s.length() >= len && 0 == wcsncmp(s.c_str(), prefix, len);
	}

	bool Selected(const std::wstring& selection, const wchar_t* test) {
		if (selection.empty()) {
			return true;
		}
		return std::wstring::npos != selection.find(test);
	}
}

int wmain(const int argc, wchar_t** argv) {
	_setmode(_fileno(stdout), _O_U8TEXT);

	if (argc < 2) {
		Usage();
		return 2;
	}

	std::wstring target = argv[1];
	if (StartsWith(target, L"--") || StartsWith(target, L"-?") || StartsWith(target, L"/?")) {
		Usage();
		return 2;
	}
	while (target.length() > 3 && (target.back() == L'\\' || target.back() == L'/')) {
		target.pop_back();
	}

	TestOptions options;
	std::wstring selection;
	bool keep = false;

	for (int i = 2; i < argc; i++) {
		const std::wstring arg = argv[i];
		if (StartsWith(arg, L"--tests=")) {
			selection = arg.substr(8);
		} else if (StartsWith(arg, L"--mass-files=")) {
			options.MassFiles = _wtoi(arg.substr(13).c_str());
		} else if (StartsWith(arg, L"--mass-dirs=")) {
			options.MassDirs = _wtoi(arg.substr(12).c_str());
		} else if (StartsWith(arg, L"--stress-seconds=")) {
			options.StressSeconds = _wtoi(arg.substr(17).c_str());
		} else if (StartsWith(arg, L"--sd-scan-limit=")) {
			options.SdScanLimit = _wtoi(arg.substr(16).c_str());
		} else if (StartsWith(arg, L"--provoke-rounds=")) {
			options.ProvokeRounds = _wtoi(arg.substr(17).c_str());
		} else if (arg == L"--keep") {
			keep = true;
		} else {
			wprintf(L"unknown option: %s\n\n", arg.c_str());
			Usage();
			return 2;
		}
	}

	if (options.MassDirs < 1) {
		options.MassDirs = 1;
	}

	if (!Exists(target)) {
		wprintf(L"target directory '%s' does not exist\n", target.c_str());
		return 2;
	}

	options.WorkDir = PathJoin(target, L"memfs-regtest");
	RemoveTree(options.WorkDir);
	if (!MakeDir(options.WorkDir)) {
		wprintf(L"cannot create work directory '%s': %s\n",
		        options.WorkDir.c_str(), ErrText(GetLastError()).c_str());
		return 2;
	}

	wprintf(L"MemfsRegressionTester\n");
	wprintf(L"  target:      %s\n", target.c_str());
	wprintf(L"  work dir:    %s\n", options.WorkDir.c_str());
	wprintf(L"  mass files:  %d in %d directories\n", options.MassFiles, options.MassDirs);
	wprintf(L"  stress:      %d seconds\n", options.StressSeconds);
	fflush(stdout);

	const ULONGLONG startTicks = GetTickCount64();

	if (Selected(selection, L"T1")) {
		T1_RenameOverOpenFile(options);
		T2_SweepAfter(options, L"T1");
	}
	if (Selected(selection, L"T2")) {
		T2_SecurityDescriptorIntegrity(options);
	}
	if (Selected(selection, L"T3")) {
		T3_NamedStreamOutlivesMainFile(options);
		T2_SweepAfter(options, L"T3");
	}
	if (Selected(selection, L"T4")) {
		T4_ParallelCreateCollision(options);
		T2_SweepAfter(options, L"T4");
	}
	if (Selected(selection, L"T5")) {
		T5_MassFilesAndCapacity(options);
		T2_SweepAfter(options, L"T5");
	}
	if (Selected(selection, L"T6")) {
		T6_ListingAfterRenameChains(options);
		T2_SweepAfter(options, L"T6");
	}
	if (Selected(selection, L"T7")) {
		T7_Concurrency(options);
		T2_SweepAfter(options, L"T7");
	}
	if (Selected(selection, L"T8")) {
		T8_UnlinkAndRecreateProvocation(options);
		T2_SweepAfter(options, L"T8");
	}

	const ULONGLONG elapsed = GetTickCount64() - startTicks;

	if (!keep) {
		RemoveTree(options.WorkDir);
	}

	wprintf(L"\n==========================================================\n");
	wprintf(L"  %d checks, %d failures, %llu ms\n", TotalChecks(), TotalFailures(), elapsed);
	wprintf(L"  %s\n", 0 == TotalFailures() ? L"ALL GREEN" : L"FAILURES PRESENT");
	wprintf(L"==========================================================\n");
	fflush(stdout);

	return TotalFailures();
}
