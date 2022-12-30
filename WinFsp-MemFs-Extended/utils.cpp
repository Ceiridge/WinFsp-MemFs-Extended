#include <Windows.h>

#include "utils.h"

namespace Memfs::Utils {
	uint64_t GetSystemTime() {
		FILETIME fileTime;
		GetSystemTimeAsFileTime(&fileTime);
		return ((PLARGE_INTEGER)&fileTime)->QuadPart;
	}

	SuffixView PathSuffix(const std::wstring_view& path, const std::wstring_view& root) {
		SuffixView result = {
			.RemainPrefix = path,
			.Suffix = path
		};

		const size_t suffixBegin = path.find_last_of(L'\\');
		if (suffixBegin != std::wstring_view::npos) {
			// There is an edge case, which is different: "\\\\\\" etc. This most likely won't make a difference
			result.RemainPrefix = path.substr(0, suffixBegin);
			result.Suffix = path.substr(suffixBegin + 1);

			if (result.RemainPrefix.empty()) {
				result.RemainPrefix = root;
			}
		} else {
			result.Suffix = L"";
		}

		return result;
	}
}
