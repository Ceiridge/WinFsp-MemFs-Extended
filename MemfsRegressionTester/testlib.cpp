#include "testlib.h"

#include <cstdarg>
#include <cstdio>

namespace TestLib {
	static int g_totalChecks = 0;
	static int g_totalFailures = 0;
	static int g_testChecks = 0;
	static int g_testFailures = 0;
	static std::wstring g_currentTest;

	std::wstring Fmt(const wchar_t* fmt, ...) {
		wchar_t buf[4096];
		va_list ap;
		va_start(ap, fmt);
		_vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
		va_end(ap);
		return buf;
	}

	std::wstring ErrText(const DWORD err) {
		const wchar_t* name;
		switch (err) {
		case ERROR_SUCCESS: name = L"ERROR_SUCCESS"; break;
		case ERROR_FILE_NOT_FOUND: name = L"ERROR_FILE_NOT_FOUND"; break;
		case ERROR_PATH_NOT_FOUND: name = L"ERROR_PATH_NOT_FOUND"; break;
		case ERROR_ACCESS_DENIED: name = L"ERROR_ACCESS_DENIED"; break;
		case ERROR_INVALID_HANDLE: name = L"ERROR_INVALID_HANDLE"; break;
		case ERROR_NOT_ENOUGH_MEMORY: name = L"ERROR_NOT_ENOUGH_MEMORY"; break;
		case ERROR_NO_MORE_FILES: name = L"ERROR_NO_MORE_FILES"; break;
		case ERROR_SHARING_VIOLATION: name = L"ERROR_SHARING_VIOLATION"; break;
		case ERROR_HANDLE_DISK_FULL: name = L"ERROR_HANDLE_DISK_FULL"; break;
		case ERROR_FILE_EXISTS: name = L"ERROR_FILE_EXISTS"; break;
		case ERROR_INVALID_PARAMETER: name = L"ERROR_INVALID_PARAMETER"; break;
		case ERROR_INSUFFICIENT_BUFFER: name = L"ERROR_INSUFFICIENT_BUFFER"; break;
		case ERROR_DISK_FULL: name = L"ERROR_DISK_FULL"; break;
		case ERROR_ALREADY_EXISTS: name = L"ERROR_ALREADY_EXISTS"; break;
		case ERROR_DIR_NOT_EMPTY: name = L"ERROR_DIR_NOT_EMPTY"; break;
		case ERROR_UNKNOWN_REVISION: name = L"ERROR_UNKNOWN_REVISION  <<< the reported symptom"; break;
		case ERROR_INVALID_OWNER: name = L"ERROR_INVALID_OWNER"; break;
		case ERROR_PRIVILEGE_NOT_HELD: name = L"ERROR_PRIVILEGE_NOT_HELD"; break;
		case ERROR_INVALID_SECURITY_DESCR: name = L"ERROR_INVALID_SECURITY_DESCR"; break;
		default: name = L"?"; break;
		}
		return Fmt(L"%lu (%s)", err, name);
	}

	void BeginTest(const std::wstring& name) {
		g_currentTest = name;
		g_testChecks = 0;
		g_testFailures = 0;
		wprintf(L"\n=== %s ===\n", name.c_str());
		fflush(stdout);
	}

	int EndTest() {
		wprintf(L"--- %s: %d checks, %d failures\n", g_currentTest.c_str(), g_testChecks, g_testFailures);
		fflush(stdout);
		return g_testFailures;
	}

	bool Check(const bool condition, const std::wstring& what) {
		g_testChecks++;
		g_totalChecks++;
		if (condition) {
			wprintf(L"  PASS  %s\n", what.c_str());
		} else {
			g_testFailures++;
			g_totalFailures++;
			wprintf(L"  FAIL  %s\n", what.c_str());
		}
		fflush(stdout);
		return condition;
	}

	void Info(const std::wstring& what) {
		wprintf(L"  info  %s\n", what.c_str());
		fflush(stdout);
	}

	void Skipped(const std::wstring& what) {
		wprintf(L"  skip  %s\n", what.c_str());
		fflush(stdout);
	}

	int TotalChecks() { return g_totalChecks; }
	int TotalFailures() { return g_totalFailures; }

	// ---------------------------------------------------------------------

	std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
		if (a.empty()) {
			return b;
		}
		if (a.back() == L'\\' || a.back() == L'/') {
			return a + b;
		}
		return a + L"\\" + b;
	}

	bool Exists(const std::wstring& path) {
		return INVALID_FILE_ATTRIBUTES != GetFileAttributesW(path.c_str());
	}

	bool MakeDir(const std::wstring& path) {
		if (CreateDirectoryW(path.c_str(), nullptr)) {
			return true;
		}
		return ERROR_ALREADY_EXISTS == GetLastError();
	}

	bool WriteWholeFile(const std::wstring& path, const std::string& data) {
		const HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
		                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (INVALID_HANDLE_VALUE == h) {
			return false;
		}

		DWORD written = 0;
		const BOOL ok = data.empty()
			                ? TRUE
			                : WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr);
		CloseHandle(h);
		return ok && written == (DWORD)data.size();
	}

	bool ReadWholeFile(const std::wstring& path, std::string& out) {
		out.clear();

		const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (INVALID_HANDLE_VALUE == h) {
			return false;
		}

		char buf[65536];
		DWORD read = 0;
		while (ReadFile(h, buf, sizeof buf, &read, nullptr) && read != 0) {
			out.append(buf, read);
		}

		CloseHandle(h);
		return true;
	}

	std::vector<std::wstring> ListDir(const std::wstring& dir) {
		std::vector<std::wstring> names;

		WIN32_FIND_DATAW fd{};
		const HANDLE h = FindFirstFileW(PathJoin(dir, L"*").c_str(), &fd);
		if (INVALID_HANDLE_VALUE == h) {
			return names;
		}

		do {
			const std::wstring name = fd.cFileName;
			if (name == L"." || name == L"..") {
				continue;
			}
			names.push_back(name);
		} while (FindNextFileW(h, &fd));

		FindClose(h);
		return names;
	}

	bool ListDirContains(const std::wstring& dir, const std::wstring& name) {
		for (const auto& entry : ListDir(dir)) {
			if (0 == _wcsicmp(entry.c_str(), name.c_str())) {
				return true;
			}
		}
		return false;
	}

	void RemoveTree(const std::wstring& path) {
		const DWORD attributes = GetFileAttributesW(path.c_str());
		if (INVALID_FILE_ATTRIBUTES == attributes) {
			return;
		}

		if (0 == (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
			SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
			DeleteFileW(path.c_str());
			return;
		}

		WIN32_FIND_DATAW fd{};
		const HANDLE h = FindFirstFileW(PathJoin(path, L"*").c_str(), &fd);
		if (INVALID_HANDLE_VALUE != h) {
			do {
				const std::wstring name = fd.cFileName;
				if (name == L"." || name == L"..") {
					continue;
				}
				RemoveTree(PathJoin(path, name));
			} while (FindNextFileW(h, &fd));
			FindClose(h);
		}

		SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_DIRECTORY);
		RemoveDirectoryW(path.c_str());
	}

	unsigned long long FreeBytes(const std::wstring& path) {
		ULARGE_INTEGER freeAvailable{}, total{}, totalFree{};
		if (!GetDiskFreeSpaceExW(path.c_str(), &freeAvailable, &total, &totalFree)) {
			return 0;
		}
		return totalFree.QuadPart;
	}

	std::wstring TempDir() {
		wchar_t buf[MAX_PATH + 1]{};
		const DWORD len = GetTempPathW(MAX_PATH, buf);
		if (0 == len) {
			return L"C:\\Windows\\Temp";
		}

		std::wstring result(buf, len);
		while (!result.empty() && (result.back() == L'\\' || result.back() == L'/')) {
			result.pop_back();
		}
		return result;
	}

	// ---------------------------------------------------------------------

	static constexpr SECURITY_INFORMATION SD_INFO =
		OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;

	bool ReadSecurityDescriptor(const std::wstring& path, std::vector<BYTE>& out, DWORD& err, DWORD& needed) {
		out.clear();
		needed = 0;

		SetLastError(0);
		if (GetFileSecurityW(path.c_str(), SD_INFO, nullptr, 0, &needed)) {
			// Unexpected: a zero-sized descriptor
			err = ERROR_SUCCESS;
			return false;
		}

		err = GetLastError();
		if (ERROR_INSUFFICIENT_BUFFER != err || 0 == needed) {
			return false;
		}

		out.resize(needed);
		DWORD needed2 = 0;
		if (!GetFileSecurityW(path.c_str(), SD_INFO, out.data(), needed, &needed2)) {
			err = GetLastError();
			out.clear();
			return false;
		}

		err = ERROR_SUCCESS;
		return true;
	}

	bool CheckSecurityDescriptorOf(const std::wstring& path) {
		std::vector<BYTE> sd;
		DWORD err = 0, needed = 0;

		if (!ReadSecurityDescriptor(path, sd, err, needed)) {
			Check(false, Fmt(L"GetFileSecurityW('%s') failed: %s", path.c_str(), ErrText(err).c_str()));
			return false;
		}

		if (!IsValidSecurityDescriptor(sd.data())) {
			const BYTE revision = sd.empty() ? 0 : sd[0];
			Check(false, Fmt(L"IsValidSecurityDescriptor('%s') == FALSE (revision byte = %u, size = %lu)",
			                 path.c_str(), (unsigned)revision, needed));
			return false;
		}

		const DWORD length = (DWORD)GetSecurityDescriptorLength(sd.data());
		if (length != needed) {
			Check(false, Fmt(L"GetSecurityDescriptorLength('%s') == %lu, but GetFileSecurityW wanted %lu",
			                 path.c_str(), length, needed));
			return false;
		}

		return true;
	}

	static void WalkForSd(const std::wstring& dir, int& remaining, int& checked) {
		WIN32_FIND_DATAW fd{};
		const HANDLE h = FindFirstFileW(PathJoin(dir, L"*").c_str(), &fd);
		if (INVALID_HANDLE_VALUE == h) {
			return;
		}

		std::vector<std::wstring> subDirs;
		do {
			const std::wstring name = fd.cFileName;
			if (name == L"." || name == L"..") {
				continue;
			}
			if (remaining <= 0) {
				break;
			}

			const std::wstring full = PathJoin(dir, name);
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				subDirs.push_back(full);
				continue;
			}

			remaining--;
			checked++;
			CheckSecurityDescriptorOf(full);
		} while (FindNextFileW(h, &fd));
		FindClose(h);

		for (const auto& sub : subDirs) {
			if (remaining <= 0) {
				return;
			}
			WalkForSd(sub, remaining, checked);
		}
	}

	int CheckSecurityDescriptorsRecursive(const std::wstring& dir, const int maxFiles) {
		int remaining = maxFiles;
		int checked = 0;
		WalkForSd(dir, remaining, checked);
		return checked;
	}
}
