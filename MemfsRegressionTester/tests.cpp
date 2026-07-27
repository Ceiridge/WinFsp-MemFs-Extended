#include "tests.h"
#include "testlib.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace TestLib;

#ifndef FILE_RENAME_FLAG_REPLACE_IF_EXISTS
#define FILE_RENAME_FLAG_REPLACE_IF_EXISTS 0x00000001
#endif
#ifndef FILE_RENAME_FLAG_POSIX_SEMANTICS
#define FILE_RENAME_FLAG_POSIX_SEMANTICS 0x00000002
#endif

// FileRenameInfoEx is only declared for NTDDI >= WIN10_RS1; use the raw value so the
// project builds against any SDK.
static constexpr FILE_INFO_BY_HANDLE_CLASS FileRenameInfoExClass = (FILE_INFO_BY_HANDLE_CLASS)22;

static constexpr DWORD SHARE_ALL = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

namespace {
	std::wstring MakeTestDir(const TestOptions& opt, const wchar_t* name) {
		const std::wstring dir = PathJoin(opt.WorkDir, name);
		RemoveTree(dir);
		MakeDir(dir);
		return dir;
	}

	// Renames the file behind handle h to newFullPath using POSIX semantics.
	// Returns ERROR_SUCCESS on success, otherwise the Win32 error.
	DWORD RenameByHandlePosix(const HANDLE h, const std::wstring& newFullPath) {
		const size_t nameBytes = newFullPath.length() * sizeof(WCHAR);
		std::vector<BYTE> buffer(sizeof(FILE_RENAME_INFO) + nameBytes + sizeof(WCHAR), 0);

		auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
		info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
		info->RootDirectory = nullptr;
		info->FileNameLength = (DWORD)nameBytes;
		memcpy(info->FileName, newFullPath.c_str(), nameBytes + sizeof(WCHAR));

		if (SetFileInformationByHandle(h, FileRenameInfoExClass, buffer.data(), (DWORD)buffer.size())) {
			return ERROR_SUCCESS;
		}
		return GetLastError();
	}

	std::string RepeatedPattern(const size_t size, const char seed) {
		std::string data(size, seed);
		for (size_t i = 0; i < size; i++) {
			data[i] = (char)('A' + (char)((i + (size_t)(unsigned char)seed) % 26));
		}
		return data;
	}
}

// ---------------------------------------------------------------------------
// T1 — rename over an open file (audit finding A1)
// ---------------------------------------------------------------------------
void T1_RenameOverOpenFile(const TestOptions& opt) {
	BeginTest(L"T1 rename over an open file (A1)");

	const std::wstring dir = MakeTestDir(opt, L"T1");
	const std::wstring pathX = PathJoin(dir, L"X.txt");
	const std::wstring pathY = PathJoin(dir, L"Y.txt");

	if (!Check(WriteWholeFile(pathX, "AAA"), Fmt(L"create X.txt: %s", ErrText(GetLastError()).c_str())) ||
		!Check(WriteWholeFile(pathY, "BBB"), Fmt(L"create Y.txt: %s", ErrText(GetLastError()).c_str()))) {
		EndTest();
		return;
	}

	const HANDLE hX = CreateFileW(pathX.c_str(), GENERIC_READ, SHARE_ALL, nullptr,
	                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!Check(INVALID_HANDLE_VALUE != hX, Fmt(L"open X.txt for reading: %s", ErrText(GetLastError()).c_str()))) {
		EndTest();
		return;
	}

	const HANDLE hY = CreateFileW(pathY.c_str(), DELETE | SYNCHRONIZE, SHARE_ALL, nullptr,
	                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!Check(INVALID_HANDLE_VALUE != hY, Fmt(L"open Y.txt with DELETE access: %s", ErrText(GetLastError()).c_str()))) {
		CloseHandle(hX);
		EndTest();
		return;
	}

	const DWORD renameErr = RenameByHandlePosix(hY, pathX);
	if (ERROR_SUCCESS == renameErr) {
		Info(L"rename used FileRenameInfoEx with POSIX semantics");
		Check(true, L"Y.txt renamed over the still-open X.txt");
	} else {
		Info(Fmt(L"FileRenameInfoEx failed with %s, falling back to MoveFileExW", ErrText(renameErr).c_str()));
		const BOOL moved = MoveFileExW(pathY.c_str(), pathX.c_str(), MOVEFILE_REPLACE_EXISTING);
		Check(!!moved, Fmt(L"MoveFileExW(Y.txt -> X.txt, REPLACE_EXISTING): %s",
		                   moved ? L"ok" : ErrText(GetLastError()).c_str()));
	}

	CloseHandle(hY);
	// Closing the handle of the file that lost its name is where the use-after-free of A1
	// becomes visible (Close -> Dereference on an already deleted node).
	CloseHandle(hX);

	Check(INVALID_FILE_ATTRIBUTES != GetFileAttributesW(pathX.c_str()),
	      Fmt(L"X.txt still exists after the rename: %s", ErrText(GetLastError()).c_str()));

	std::string content;
	if (Check(ReadWholeFile(pathX, content), Fmt(L"X.txt is readable: %s", ErrText(GetLastError()).c_str()))) {
		Check(content == "BBB", Fmt(L"X.txt has the content of Y.txt (expected 'BBB', got '%S')", content.c_str()));
	}

	Check(ListDirContains(dir, L"X.txt"), L"X.txt appears in the FindFirstFileW listing of its directory");
	Check(!ListDirContains(dir, L"Y.txt"), L"Y.txt no longer appears in the listing");
	Check(1 == (int)ListDir(dir).size(), Fmt(L"directory holds exactly one entry (got %d)", (int)ListDir(dir).size()));

	CheckSecurityDescriptorOf(pathX);
	Check(true, L"security descriptor of X.txt checked");

	EndTest();
}

// ---------------------------------------------------------------------------
// T2 — SD integrity and a real ERROR_UNKNOWN_REVISION reproduction
// ---------------------------------------------------------------------------
static void CrossVolumeSecurityCopy(const std::wstring& source) {
	// This is the path the Explorer takes when copying across volumes.
	PSECURITY_DESCRIPTOR sd = nullptr;
	PACL dacl = nullptr;
	PSID owner = nullptr, group = nullptr;

	const DWORD getErr = GetNamedSecurityInfoW(source.c_str(), SE_FILE_OBJECT,
	                                           OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
	                                           DACL_SECURITY_INFORMATION,
	                                           &owner, &group, &dacl, nullptr, &sd);
	if (!Check(ERROR_SUCCESS == getErr, Fmt(L"GetNamedSecurityInfoW('%s'): %s",
	                                        source.c_str(), ErrText(getErr).c_str()))) {
		return;
	}

	if (!Check(nullptr != sd && !!IsValidSecurityDescriptor(sd),
	           Fmt(L"security descriptor of '%s' is valid", source.c_str()))) {
		LocalFree(sd);
		return;
	}

	const std::wstring destination = PathJoin(TempDir(), Fmt(L"memfs-regtest-sdcopy-%lu.tmp", GetCurrentProcessId()));
	DeleteFileW(destination.c_str());

	if (Check(!!CopyFileW(source.c_str(), destination.c_str(), FALSE),
	          Fmt(L"CopyFileW to the temp directory (NTFS): %s", ErrText(GetLastError()).c_str()))) {
		const DWORD daclErr = SetNamedSecurityInfoW(const_cast<LPWSTR>(destination.c_str()), SE_FILE_OBJECT,
		                                            DACL_SECURITY_INFORMATION,
		                                            nullptr, nullptr, dacl, nullptr);
		Check(ERROR_SUCCESS == daclErr,
		      Fmt(L"SetNamedSecurityInfoW(DACL) on the NTFS copy: %s", ErrText(daclErr).c_str()));

		const DWORD fullErr = SetNamedSecurityInfoW(const_cast<LPWSTR>(destination.c_str()), SE_FILE_OBJECT,
		                                           OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
		                                           DACL_SECURITY_INFORMATION,
		                                           owner, group, dacl, nullptr);
		if (ERROR_SUCCESS == fullErr) {
			Check(true, L"SetNamedSecurityInfoW(OWNER|GROUP|DACL) on the NTFS copy");
		} else if (ERROR_INVALID_OWNER == fullErr || ERROR_PRIVILEGE_NOT_HELD == fullErr ||
			ERROR_ACCESS_DENIED == fullErr) {
			Skipped(Fmt(L"SetNamedSecurityInfoW(OWNER|GROUP|DACL) not permitted here: %s",
			            ErrText(fullErr).c_str()));
		} else {
			Check(false, Fmt(L"SetNamedSecurityInfoW(OWNER|GROUP|DACL) on the NTFS copy: %s",
			                 ErrText(fullErr).c_str()));
		}

		// The plain byte-level path: read the whole SD and write it back onto NTFS.
		std::vector<BYTE> raw;
		DWORD err = 0, needed = 0;
		if (ReadSecurityDescriptor(source, raw, err, needed)) {
			const BOOL setOk = SetFileSecurityW(destination.c_str(), DACL_SECURITY_INFORMATION, raw.data());
			const DWORD setErr = setOk ? ERROR_SUCCESS : GetLastError();
			Check(!!setOk, Fmt(L"SetFileSecurityW(DACL) with the raw descriptor of the source: %s",
			                   ErrText(setErr).c_str()));
		} else {
			Check(false, Fmt(L"GetFileSecurityW('%s'): %s", source.c_str(), ErrText(err).c_str()));
		}

		DeleteFileW(destination.c_str());
	}

	LocalFree(sd);
}

void T2_SecurityDescriptorIntegrity(const TestOptions& opt) {
	BeginTest(L"T2 security descriptor integrity and cross-volume copy");

	const std::wstring dir = MakeTestDir(opt, L"T2");
	const std::wstring path = PathJoin(dir, L"sd-source.txt");

	if (!Check(WriteWholeFile(path, "security descriptor test payload"),
	           Fmt(L"create sd-source.txt: %s", ErrText(GetLastError()).c_str()))) {
		EndTest();
		return;
	}

	Check(CheckSecurityDescriptorOf(path), L"GetFileSecurityW/IsValidSecurityDescriptor on a fresh file");
	CrossVolumeSecurityCopy(path);

	EndTest();
}

void T2_SweepAfter(const TestOptions& opt, const wchar_t* afterTest) {
	BeginTest(Fmt(L"T2 sweep: all security descriptors below the work directory (after %s)", afterTest));

	const int before = TotalFailures();
	const int checked = CheckSecurityDescriptorsRecursive(opt.WorkDir, opt.SdScanLimit);
	const int broken = TotalFailures() - before;

	Check(0 == broken, Fmt(L"%d of %d inspected files have a broken security descriptor", broken, checked));
	EndTest();
}

// ---------------------------------------------------------------------------
// T3 — a named stream outlives its main file (audit finding A2)
// ---------------------------------------------------------------------------
void T3_NamedStreamOutlivesMainFile(const TestOptions& opt) {
	BeginTest(L"T3 named stream outlives the main file (A2)");

	const std::wstring dir = MakeTestDir(opt, L"T3");
	const std::wstring path = PathJoin(dir, L"Z.txt");
	const std::wstring streamPath = path + L":Zone.Identifier";
	const std::string streamData = "[ZoneTransfer]\r\nZoneId=3\r\n";

	if (!Check(WriteWholeFile(path, "MAINDATA"), Fmt(L"create Z.txt: %s", ErrText(GetLastError()).c_str()))) {
		EndTest();
		return;
	}
	if (!Check(WriteWholeFile(streamPath, streamData),
	           Fmt(L"create Z.txt:Zone.Identifier: %s", ErrText(GetLastError()).c_str()))) {
		EndTest();
		return;
	}

	const HANDLE hStream = CreateFileW(streamPath.c_str(), GENERIC_READ | READ_CONTROL, SHARE_ALL, nullptr,
	                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!Check(INVALID_HANDLE_VALUE != hStream,
	           Fmt(L"open the stream with FILE_SHARE_DELETE: %s", ErrText(GetLastError()).c_str()))) {
		EndTest();
		return;
	}

	const BOOL deleted = DeleteFileW(path.c_str());
	if (!Check(!!deleted, Fmt(L"DeleteFileW(Z.txt) while the stream handle is open: %s",
	                          ErrText(GetLastError()).c_str()))) {
		CloseHandle(hStream);
		EndTest();
		return;
	}

	BY_HANDLE_FILE_INFORMATION info{};
	if (Check(!!GetFileInformationByHandle(hStream, &info),
	          Fmt(L"GetFileInformationByHandle on the orphaned stream: %s", ErrText(GetLastError()).c_str()))) {
		const unsigned long long size = ((unsigned long long)info.nFileSizeHigh << 32) | info.nFileSizeLow;
		Check(size == streamData.size(),
		      Fmt(L"stream reports its own size (expected %llu, got %llu)",
		          (unsigned long long)streamData.size(), size));
	}

	char buffer[256]{};
	DWORD read = 0;
	if (Check(!!ReadFile(hStream, buffer, sizeof buffer, &read, nullptr),
	          Fmt(L"ReadFile on the orphaned stream: %s", ErrText(GetLastError()).c_str()))) {
		Check(read == (DWORD)streamData.size() && 0 == memcmp(buffer, streamData.data(), read),
		      Fmt(L"stream content is unchanged (%lu bytes read)", read));
	}

	PSECURITY_DESCRIPTOR sd = nullptr;
	PACL dacl = nullptr;
	const DWORD secErr = GetSecurityInfo(hStream, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
	                                     nullptr, nullptr, &dacl, nullptr, &sd);
	if (Check(ERROR_SUCCESS == secErr,
	          Fmt(L"GetSecurityInfo on the orphaned stream handle: %s", ErrText(secErr).c_str()))) {
		Check(nullptr != sd && !!IsValidSecurityDescriptor(sd),
		      L"the descriptor returned for the orphaned stream is valid");
		LocalFree(sd);
	}

	CloseHandle(hStream);

	Check(!Exists(path), L"Z.txt is gone once the stream handle is closed");
	Check(!ListDirContains(dir, L"Z.txt"), L"Z.txt no longer appears in the directory listing");

	EndTest();
}

// ---------------------------------------------------------------------------
// T4 — parallel CREATE_NEW collision (audit finding A3)
// ---------------------------------------------------------------------------
void T4_ParallelCreateCollision(const TestOptions& opt) {
	BeginTest(L"T4 parallel CREATE_NEW collision (A3)");

	const std::wstring dir = MakeTestDir(opt, L"T4");
	const std::wstring path = PathJoin(dir, L"same.txt");

	unsigned threadCount = std::thread::hardware_concurrency();
	if (0 == threadCount) {
		threadCount = 4;
	}
	threadCount *= 2;

	std::atomic<int> successes{0};
	std::atomic<int> collisions{0};
	std::atomic<int> otherErrors{0};
	std::atomic<DWORD> lastOtherError{0};

	{
		std::vector<std::thread> threads;
		threads.reserve(threadCount);
		for (unsigned i = 0; i < threadCount; i++) {
			threads.emplace_back([&] {
				const HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, SHARE_ALL, nullptr,
				                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (INVALID_HANDLE_VALUE != h) {
					successes++;
					CloseHandle(h);
					return;
				}

				const DWORD err = GetLastError();
				if (ERROR_FILE_EXISTS == err || ERROR_ALREADY_EXISTS == err) {
					collisions++;
				} else {
					otherErrors++;
					lastOtherError = err;
				}
			});
		}
		for (auto& t : threads) {
			t.join();
		}
	}

	Check(1 == successes.load(), Fmt(L"exactly one CREATE_NEW succeeded (got %d of %u)",
	                                 successes.load(), threadCount));
	Check(0 == otherErrors.load(), Fmt(L"no unexpected errors (got %d, last: %s)",
	                                   otherErrors.load(), ErrText(lastOtherError.load()).c_str()));
	Check(collisions.load() == (int)threadCount - successes.load(),
	      Fmt(L"every other thread saw ERROR_FILE_EXISTS (%d collisions)", collisions.load()));

	// Every handle must refer to the same node.
	std::atomic<int> openFailures{0};
	std::vector<std::pair<DWORD, DWORD>> indices(threadCount, {0, 0});
	{
		std::vector<std::thread> threads;
		threads.reserve(threadCount);
		for (unsigned i = 0; i < threadCount; i++) {
			threads.emplace_back([&, i] {
				const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, SHARE_ALL, nullptr,
				                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (INVALID_HANDLE_VALUE == h) {
					openFailures++;
					return;
				}

				BY_HANDLE_FILE_INFORMATION info{};
				if (GetFileInformationByHandle(h, &info)) {
					indices[i] = {info.nFileIndexHigh, info.nFileIndexLow};
				} else {
					openFailures++;
				}
				CloseHandle(h);
			});
		}
		for (auto& t : threads) {
			t.join();
		}
	}

	Check(0 == openFailures.load(), Fmt(L"every OPEN_EXISTING succeeded (%d failures)", openFailures.load()));

	bool allSame = true;
	for (unsigned i = 1; i < threadCount; i++) {
		if (indices[i] != indices[0]) {
			allSame = false;
			break;
		}
	}
	Check(allSame, Fmt(L"all handles report the same file index (%08lx%08lx)", indices[0].first, indices[0].second));

	EndTest();
}

// ---------------------------------------------------------------------------
// T5 — mass test and capacity accounting (audit findings C1, C2)
// ---------------------------------------------------------------------------
void T5_MassFilesAndCapacity(const TestOptions& opt) {
	BeginTest(Fmt(L"T5 mass test with %d files, capacity accounting (C1, C2)", opt.MassFiles));

	const std::wstring dir = MakeTestDir(opt, L"T5");
	const unsigned long long freeBefore = FreeBytes(opt.WorkDir);
	Info(Fmt(L"free before: %llu bytes", freeBefore));

	const std::string payload = RepeatedPattern(4096, 'q');

	int createFailures = 0, readFailures = 0, renameFailures = 0, deleteFailures = 0;
	std::vector<std::wstring> subDirs;
	for (int d = 0; d < opt.MassDirs; d++) {
		const std::wstring sub = PathJoin(dir, Fmt(L"d%03d", d));
		if (!MakeDir(sub)) {
			createFailures++;
		}
		subDirs.push_back(sub);
	}

	for (int i = 0; i < opt.MassFiles; i++) {
		const std::wstring path = PathJoin(subDirs[i % opt.MassDirs], Fmt(L"f%06d.dat", i));
		if (!WriteWholeFile(path, payload)) {
			if (createFailures < 5) {
				Info(Fmt(L"create '%s' failed: %s", path.c_str(), ErrText(GetLastError()).c_str()));
			}
			createFailures++;
		}
	}
	Check(0 == createFailures, Fmt(L"all %d files and %d directories created (%d failures)",
	                               opt.MassFiles, opt.MassDirs, createFailures));

	for (int i = 0; i < opt.MassFiles; i++) {
		const std::wstring path = PathJoin(subDirs[i % opt.MassDirs], Fmt(L"f%06d.dat", i));
		std::string content;
		if (!ReadWholeFile(path, content) || content != payload) {
			readFailures++;
		}
	}
	Check(0 == readFailures, Fmt(L"all %d files read back byte-identical (%d failures)",
	                             opt.MassFiles, readFailures));

	for (int i = 0; i < opt.MassFiles; i++) {
		const std::wstring from = PathJoin(subDirs[i % opt.MassDirs], Fmt(L"f%06d.dat", i));
		const std::wstring to = PathJoin(subDirs[i % opt.MassDirs], Fmt(L"r%06d.dat", i));
		if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) {
			renameFailures++;
		}
	}
	Check(0 == renameFailures, Fmt(L"all %d files renamed (%d failures)", opt.MassFiles, renameFailures));

	for (int i = 0; i < opt.MassFiles; i++) {
		const std::wstring path = PathJoin(subDirs[i % opt.MassDirs], Fmt(L"r%06d.dat", i));
		if (!DeleteFileW(path.c_str())) {
			deleteFailures++;
		}
	}
	for (const auto& sub : subDirs) {
		if (!RemoveDirectoryW(sub.c_str())) {
			deleteFailures++;
		}
	}
	Check(0 == deleteFailures, Fmt(L"all files and directories deleted (%d failures)", deleteFailures));

	Check(0 == (int)ListDir(dir).size(), Fmt(L"T5 directory is empty again (%d entries left)",
	                                         (int)ListDir(dir).size()));

	const unsigned long long freeAfter = FreeBytes(opt.WorkDir);
	Info(Fmt(L"free after:  %llu bytes", freeAfter));

	const unsigned long long tolerance = freeBefore / 100 + 1;
	const unsigned long long delta = freeBefore > freeAfter ? freeBefore - freeAfter : freeAfter - freeBefore;
	Check(delta <= tolerance,
	      Fmt(L"free space back within 1%% (delta %llu bytes, tolerance %llu bytes)", delta, tolerance));

	EndTest();
}

// ---------------------------------------------------------------------------
// T6 — directory listings after rename chains (audit finding A4)
// ---------------------------------------------------------------------------
namespace {
	int CountFilesRecursive(const std::wstring& dir) {
		int count = 0;
		WIN32_FIND_DATAW fd{};
		const HANDLE h = FindFirstFileW(PathJoin(dir, L"*").c_str(), &fd);
		if (INVALID_HANDLE_VALUE == h) {
			return 0;
		}
		std::vector<std::wstring> subDirs;
		do {
			const std::wstring name = fd.cFileName;
			if (name == L"." || name == L"..") {
				continue;
			}
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				subDirs.push_back(PathJoin(dir, name));
			} else {
				count++;
			}
		} while (FindNextFileW(h, &fd));
		FindClose(h);

		for (const auto& sub : subDirs) {
			count += CountFilesRecursive(sub);
		}
		return count;
	}
}

void T6_ListingAfterRenameChains(const TestOptions& opt) {
	BeginTest(L"T6 directory listing after rename chains (A4)");

	const std::wstring dir = MakeTestDir(opt, L"T6");
	constexpr int filesPerLevel = 5;

	std::wstring current = PathJoin(dir, L"d0");
	MakeDir(current);
	std::wstring level1 = PathJoin(current, L"d1");
	MakeDir(level1);
	std::wstring level2 = PathJoin(level1, L"d2");
	MakeDir(level2);

	int expectedFiles = 0;
	for (const auto& base : {current, level1, level2}) {
		for (int i = 0; i < filesPerLevel; i++) {
			const std::wstring path = PathJoin(base, Fmt(L"f%d.txt", i));
			if (WriteWholeFile(path, "content-" + std::to_string(i))) {
				expectedFiles++;
			}
		}
	}
	Check(filesPerLevel * 3 == expectedFiles, Fmt(L"created %d files in the tree", expectedFiles));

	for (int round = 1; round <= 5; round++) {
		const std::wstring next = PathJoin(dir, Fmt(L"d0-round%d", round));
		if (!Check(!!MoveFileExW(current.c_str(), next.c_str(), 0),
		           Fmt(L"rename round %d: '%s' -> '%s': %s", round, current.c_str(), next.c_str(),
		               ErrText(GetLastError()).c_str()))) {
			EndTest();
			return;
		}
		current = next;

		const int found = CountFilesRecursive(current);
		Check(found == expectedFiles,
		      Fmt(L"round %d: recursive listing still finds %d files (got %d)", round, expectedFiles, found));

		std::string content;
		const std::wstring probe = PathJoin(PathJoin(current, L"d1\\d2"), L"f2.txt");
		Check(ReadWholeFile(probe, content) && content == "content-2",
		      Fmt(L"round %d: deepest file still readable with the right content", round));

		Check(1 == (int)ListDir(dir).size(),
		      Fmt(L"round %d: the parent holds exactly one entry (got %d)", round, (int)ListDir(dir).size()));
	}

	EndTest();
}

// ---------------------------------------------------------------------------
// T8 — a name changing owner while the old node is still open (A1, A4)
//
// Every round re-associates a path with a *different* node while a handle on the
// previous node is still alive, then closes that handle. That is the exact window in
// which RemoveNode/Cleanup would unlink the wrong map entry.
// ---------------------------------------------------------------------------
void T8_UnlinkAndRecreateProvocation(const TestOptions& opt) {
	BeginTest(Fmt(L"T8 name changes owner while a handle is open, %d rounds (A1, A4)", opt.ProvokeRounds));

	const std::wstring dir = MakeTestDir(opt, L"T8");

	int unlinkFailures = 0, renameFailures = 0, streamFailures = 0;
	int firstReported = 0;
	const auto report = [&](const std::wstring& message) {
		if (firstReported++ < 5) {
			Info(message);
		}
	};

	for (int round = 0; round < opt.ProvokeRounds; round++) {
		// --- pattern 1: POSIX unlink with an open handle, then recreate the same name ---
		{
			const std::wstring path = PathJoin(dir, Fmt(L"p1-%d.txt", round));
			WriteWholeFile(path, "old");

			const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, SHARE_ALL, nullptr,
			                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (INVALID_HANDLE_VALUE == h) {
				unlinkFailures++;
				report(Fmt(L"p1 round %d: open failed: %s", round, ErrText(GetLastError()).c_str()));
			} else {
				if (!DeleteFileW(path.c_str())) {
					report(Fmt(L"p1 round %d: DeleteFileW failed: %s", round, ErrText(GetLastError()).c_str()));
					unlinkFailures++;
				}
				// The name is free again while the old node still lives behind h.
				if (!WriteWholeFile(path, "new")) {
					report(Fmt(L"p1 round %d: recreate failed: %s", round, ErrText(GetLastError()).c_str()));
					unlinkFailures++;
				}
				CloseHandle(h); // <- the old node's Close/Cleanup lands here

				std::string content;
				if (!ReadWholeFile(path, content) || content != "new") {
					report(Fmt(L"p1 round %d: recreated file lost or wrong content ('%S')", round, content.c_str()));
					unlinkFailures++;
				}
				if (!ListDirContains(dir, Fmt(L"p1-%d.txt", round))) {
					report(Fmt(L"p1 round %d: recreated file missing from the listing", round));
					unlinkFailures++;
				}
				DeleteFileW(path.c_str());
			}
		}

		// --- pattern 2: rename with replaceIfExists over a file that is still open ---
		{
			const std::wstring pathA = PathJoin(dir, Fmt(L"p2-a%d.txt", round));
			const std::wstring pathB = PathJoin(dir, Fmt(L"p2-b%d.txt", round));
			WriteWholeFile(pathA, "old");
			WriteWholeFile(pathB, "new");

			const HANDLE hA = CreateFileW(pathA.c_str(), GENERIC_READ, SHARE_ALL, nullptr,
			                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			const HANDLE hB = CreateFileW(pathB.c_str(), DELETE | SYNCHRONIZE, SHARE_ALL, nullptr,
			                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (INVALID_HANDLE_VALUE == hA || INVALID_HANDLE_VALUE == hB) {
				renameFailures++;
				report(Fmt(L"p2 round %d: open failed: %s", round, ErrText(GetLastError()).c_str()));
			} else {
				const DWORD err = RenameByHandlePosix(hB, pathA);
				if (ERROR_SUCCESS != err && !MoveFileExW(pathB.c_str(), pathA.c_str(), MOVEFILE_REPLACE_EXISTING)) {
					report(Fmt(L"p2 round %d: rename failed: %s", round, ErrText(GetLastError()).c_str()));
					renameFailures++;
				}
				CloseHandle(hB);
				// Give the replaced name a third owner before the old node is closed.
				WriteWholeFile(pathB, "third");
				CloseHandle(hA); // <- Close/Cleanup of the node that lost the name

				std::string content;
				if (!ReadWholeFile(pathA, content) || content != "new") {
					report(Fmt(L"p2 round %d: renamed file lost or wrong content ('%S')", round, content.c_str()));
					renameFailures++;
				}
				if (!ReadWholeFile(pathB, content) || content != "third") {
					report(Fmt(L"p2 round %d: re-created source lost or wrong content ('%S')", round, content.c_str()));
					renameFailures++;
				}
				DeleteFileW(pathA.c_str());
				DeleteFileW(pathB.c_str());
			}
		}

		// --- pattern 3: the same game with a named stream on the main file ---
		{
			const std::wstring path = PathJoin(dir, Fmt(L"p3-%d.txt", round));
			const std::wstring stream = path + L":s";
			WriteWholeFile(path, "main");
			WriteWholeFile(stream, "strm");

			const HANDLE h = CreateFileW(stream.c_str(), GENERIC_READ, SHARE_ALL, nullptr,
			                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (INVALID_HANDLE_VALUE == h) {
				streamFailures++;
				report(Fmt(L"p3 round %d: open stream failed: %s", round, ErrText(GetLastError()).c_str()));
			} else {
				if (!DeleteFileW(path.c_str())) {
					report(Fmt(L"p3 round %d: DeleteFileW failed: %s", round, ErrText(GetLastError()).c_str()));
					streamFailures++;
				}
				WriteWholeFile(path, "main2");
				WriteWholeFile(stream, "strm2");
				CloseHandle(h); // <- the orphaned stream node is released here

				std::string content;
				if (!ReadWholeFile(path, content) || content != "main2") {
					report(Fmt(L"p3 round %d: recreated main file lost or wrong content ('%S')",
					           round, content.c_str()));
					streamFailures++;
				}
				if (!ReadWholeFile(stream, content) || content != "strm2") {
					report(Fmt(L"p3 round %d: recreated stream lost or wrong content ('%S')",
					           round, content.c_str()));
					streamFailures++;
				}
				DeleteFileW(path.c_str());
			}
		}
	}

	Check(0 == unlinkFailures, Fmt(L"unlink-and-recreate keeps the new file intact (%d failures)", unlinkFailures));
	Check(0 == renameFailures, Fmt(L"rename-replace over an open file keeps both names intact (%d failures)",
	                               renameFailures));
	Check(0 == streamFailures, Fmt(L"the same with named streams (%d failures)", streamFailures));
	Check(0 == (int)ListDir(dir).size(), Fmt(L"T8 directory is empty again (%d entries left)",
	                                         (int)ListDir(dir).size()));

	EndTest();
}

// ---------------------------------------------------------------------------
// T7 — concurrency (audit group B)
// ---------------------------------------------------------------------------
void T7_Concurrency(const TestOptions& opt) {
	BeginTest(Fmt(L"T7 concurrency for %d seconds (group B)", opt.StressSeconds));

	const std::wstring dir = MakeTestDir(opt, L"T7");

	unsigned threadCount = std::thread::hardware_concurrency();
	if (0 == threadCount) {
		threadCount = 4;
	}
	threadCount *= 2;

	std::atomic<unsigned long long> iterations{0};
	std::atomic<int> invalidSds{0};
	std::atomic<int> errors{0};
	std::atomic<DWORD> lastError{0};

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(opt.StressSeconds);

	const auto note = [&](const DWORD err) {
		errors++;
		lastError = err;
	};

	// Half the threads share one directory, the other half get one directory each. Namespace
	// operations inside a single directory are serialised by the WinFsp FSD, so only the
	// per-thread directories can make fileMap writes overlap unlocked lookups elsewhere.
	std::vector<std::wstring> threadDirs(threadCount, dir);
	for (unsigned t = threadCount / 2; t < threadCount; t++) {
		threadDirs[t] = PathJoin(dir, Fmt(L"d%02u", t));
		MakeDir(threadDirs[t]);
	}

	std::vector<std::thread> threads;
	threads.reserve(threadCount);
	for (unsigned t = 0; t < threadCount; t++) {
		threads.emplace_back([&, t] {
			const std::string payload = RepeatedPattern(1024, (char)('a' + (char)(t % 26)));
			const std::wstring& threadDir = threadDirs[t];
			unsigned long long i = 0;

			while (std::chrono::steady_clock::now() < deadline) {
				const std::wstring pathA = PathJoin(threadDir, Fmt(L"t%02u-a%llu.dat", t, i));
				const std::wstring pathB = PathJoin(threadDir, Fmt(L"t%02u-b%llu.dat", t, i));

				if (!WriteWholeFile(pathA, payload)) {
					note(GetLastError());
				} else {
					std::vector<BYTE> sd;
					DWORD err = 0, needed = 0;
					if (!ReadSecurityDescriptor(pathA, sd, err, needed) || !IsValidSecurityDescriptor(sd.data())) {
						invalidSds++;
						lastError = err;
					}

					std::string content;
					if (!ReadWholeFile(pathA, content) || content != payload) {
						note(GetLastError());
					}

					if (!MoveFileExW(pathA.c_str(), pathB.c_str(), MOVEFILE_REPLACE_EXISTING)) {
						note(GetLastError());
						DeleteFileW(pathA.c_str());
					} else if (!DeleteFileW(pathB.c_str())) {
						note(GetLastError());
					}
				}

				iterations++;
				i++;
			}
		});
	}
	for (auto& th : threads) {
		th.join();
	}

	Info(Fmt(L"%u threads completed %llu iterations", threadCount, iterations.load()));
	Check(0 == invalidSds.load(), Fmt(L"no invalid security descriptor observed (%d seen)", invalidSds.load()));
	Check(0 == errors.load(), Fmt(L"no unexpected errors (%d seen, last: %s)",
	                              errors.load(), ErrText(lastError.load()).c_str()));

	for (unsigned t = threadCount / 2; t < threadCount; t++) {
		RemoveDirectoryW(threadDirs[t].c_str());
	}
	Check(0 == (int)ListDir(dir).size(), Fmt(L"T7 directory is empty again (%d entries left)",
	                                         (int)ListDir(dir).size()));

	EndTest();
}
