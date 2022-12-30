#pragma once
#include <string_view>

namespace Memfs::Utils {
	static int FileNameCompare(const PCWSTR a, int alen, const PCWSTR b, int blen, const BOOLEAN caseInsensitive);
	static BOOLEAN FileNameHasPrefix(const PCWSTR a, const PCWSTR b, const BOOLEAN caseInsensitive);
	static int EaNameCompare(const PCSTR a, const PCSTR b);

	struct EaLess {
		using is_transparent = std::true_type;

		bool operator()(const std::string_view& a, const std::string_view& b) const;
	};

	struct FileLess {
		using is_transparent = std::true_type; // Make it transparent, so we can use wstring_view to find keys
		bool CaseInsensitive;

		bool operator()(const std::wstring_view& a, const std::wstring_view& b) const;
	};
}
