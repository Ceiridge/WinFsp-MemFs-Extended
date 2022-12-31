#include "globalincludes.h"
#include "comparisons.h"

namespace Memfs::Utils {
	int FileNameCompare(const PCWSTR a, int alen, const PCWSTR b, int blen, const BOOLEAN caseInsensitive) {
		PCWSTR p, endp, partp, q, endq, partq;
		WCHAR c, d;
		int plen, qlen, len, res;

		if (-1 == alen)
			alen = lstrlenW(a);
		if (-1 == blen)
			blen = lstrlenW(b);

		for (p = a, endp = p + alen, q = b, endq = q + blen; endp > p && endq > q;) {
			c = d = 0;
			for (; endp > p && (L':' == *p || L'\\' == *p); p++)
				c = *p;
			for (; endq > q && (L':' == *q || L'\\' == *q); q++)
				d = *q;

			if (L':' == c)
				c = 1;
			else if (L'\\' == c)
				c = 2;
			if (L':' == d)
				d = 1;
			else if (L'\\' == d)
				d = 2;

			res = c - d;
			if (0 != res)
				return res;

			for (partp = p; endp > p && L':' != *p && L'\\' != *p; p++);
			for (partq = q; endq > q && L':' != *q && L'\\' != *q; q++);

			plen = (int)(p - partp);
			qlen = (int)(q - partq);

			len = plen < qlen ? plen : qlen;

			if (caseInsensitive) {
				res = CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, partp, plen, partq, qlen);
				if (0 != res)
					res -= 2;
				else
					res = _wcsnicmp(partp, partq, len);
			} else
				res = wcsncmp(partp, partq, len);

			if (0 == res)
				res = plen - qlen;

			if (0 != res)
				return res;
		}

		return -(endp <= p) + (endq <= q);
	}

	BOOLEAN FileNameHasPrefix(const PCWSTR a, const PCWSTR b, const BOOLEAN caseInsensitive) {
		int alen = (int)wcslen(a);
		int blen = (int)wcslen(b);

		return alen >= blen && 0 == FileNameCompare(a, blen, b, blen, caseInsensitive) &&
		(alen == blen || (1 == blen && L'\\' == b[0]) ||
			(L'\\' == a[blen] || L':' == a[blen]));
	}

	int EaNameCompare(const PCSTR a, const PCSTR b) {
		/* EA names are always case-insensitive in MEMFS (to be inline with NTFS) */

		int res;

		res = CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, a, -1, b, -1);
		if (0 != res)
			res -= 2;
		else
			res = _stricmp(a, b);

		return res;
	}

	bool EaLess::operator()(const std::string_view& a, const std::string_view& b) const {
		return 0 > EaNameCompare(a.data(), b.data());
	}

	bool FileLess::operator()(const std::wstring_view& a, const std::wstring_view& b) const {
		return 0 > FileNameCompare(a.data(), a.length(), b.data(), b.length(), this->CaseInsensitive);
	}
}
