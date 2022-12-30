#pragma once
#include <cstdint>
#include <string_view>

namespace Memfs::Utils {
	static inline uint64_t GetSystemTime();

	struct SuffixView {
		std::wstring_view RemainPrefix, Suffix;
	};
	static constexpr std::wstring_view DEFAULT_ROOT = L"\\";
	/**
	 * \brief If there is no backslash, the remainder will be the path and the suffix will be empty. Otherwise, the suffix is the last element after the last backslash and the remainder is all before the slash.
	 * \param path File path
	 * \param root Mostly backslash
	 * \return Prefix remainder and suffix
	 */
	static SuffixView PathSuffix(const std::wstring_view& path, const std::wstring_view& root = DEFAULT_ROOT);
}
