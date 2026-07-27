#pragma once

#include <Windows.h>
#include <AclAPI.h>

#include <string>
#include <vector>

namespace TestLib {
	// ---- reporting -------------------------------------------------------
	std::wstring Fmt(const wchar_t* fmt, ...);
	std::wstring ErrText(DWORD err);

	void BeginTest(const std::wstring& name);
	int EndTest(); // returns the number of failures of the test that just ended

	bool Check(bool condition, const std::wstring& what);
	void Info(const std::wstring& what);
	void Skipped(const std::wstring& what);

	int TotalChecks();
	int TotalFailures();

	// ---- filesystem helpers ----------------------------------------------
	std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
	bool Exists(const std::wstring& path);
	bool MakeDir(const std::wstring& path);
	bool WriteWholeFile(const std::wstring& path, const std::string& data);
	bool ReadWholeFile(const std::wstring& path, std::string& out);
	std::vector<std::wstring> ListDir(const std::wstring& dir);
	bool ListDirContains(const std::wstring& dir, const std::wstring& name);
	void RemoveTree(const std::wstring& path);
	unsigned long long FreeBytes(const std::wstring& path);
	std::wstring TempDir();

	// ---- security descriptor helpers -------------------------------------
	// Reads the SD of a path with the two-call GetFileSecurityW pattern.
	bool ReadSecurityDescriptor(const std::wstring& path, std::vector<BYTE>& out, DWORD& err, DWORD& needed);
	// Full per-file SD check (T2), returns true when the descriptor is sound.
	bool CheckSecurityDescriptorOf(const std::wstring& path);
	// Walks dir recursively and runs CheckSecurityDescriptorOf on up to maxFiles entries.
	// Returns the number of files checked.
	int CheckSecurityDescriptorsRecursive(const std::wstring& dir, int maxFiles);
}
