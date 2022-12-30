#pragma once

namespace Memfs::Utils {
	static int FileNameCompare(PWSTR a, int alen, PWSTR b, int blen, BOOLEAN caseInsensitive);
	static BOOLEAN FileNameHasPrefix(PWSTR a, PWSTR b, BOOLEAN caseInsensitive);
	static int EaNameCompare(PSTR a, PSTR b);

	struct EaLess {
		bool operator()(PSTR a, PSTR b) const;
	};

	struct FileLess {
		bool CaseInsensitive;

		bool operator()(PWSTR a, PWSTR b) const;
	};
}
