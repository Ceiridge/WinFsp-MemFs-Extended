#include "globalincludes.h"
#include "comparisons.h"

namespace Memfs::Utils {
	static inline unsigned UpperChar(const unsigned c)
	{
		/*
		 * Bit-twiddling upper case char:
		 *
		 * - Let signbit(x) = x & 0x100 (treat bit 0x100 as "signbit").
		 * - 'A' <= c && c <= 'Z' <=> s = signbit(c - 'A') ^ signbit(c - ('Z' + 1)) == 1
		 *     - c >= 'A' <=> c - 'A' >= 0      <=> signbit(c - 'A') = 0
		 *     - c <= 'Z' <=> c - ('Z' + 1) < 0 <=> signbit(c - ('Z' + 1)) = 1
		 * - Bit 0x20 = 0x100 >> 3 toggles uppercase to lowercase and vice-versa.
		 *
		 * This is actually faster than `(c - 'a' <= 'z' - 'a') ? (c & ~0x20) : c`, even
		 * when compiled using cmov conditional moves at least on this system (i7-1065G7).
		 *
		 * See https://godbolt.org/z/ebv131Wrh
		 */
		const unsigned s = ((c - 'a') ^ (c - ('z' + 1))) & 0x100;
		return c & ~(s >> 3);
	}

	static inline int EfficientWcsnicmp(const wchar_t* s0, const wchar_t* t0, int n)
	{
		/* Use fast loop for ASCII and fall back to CompareStringW for general case. */
		const wchar_t* s = s0;
		const wchar_t* t = t0;
		int v = 0;
		for (const void* e = t + n; e > (const void*)t; ++s, ++t)
		{
			const unsigned sc = *s;
			const unsigned tc = *t;

			if (0xffffff80 & (sc | tc))
			{
				v = CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, s0, n, t0, n);
				if (0 != v)
					return v - 2;
				else
					return _wcsnicmp(s, t, n);
			}
			if (0 != (v = UpperChar(sc) - UpperChar(tc)) || !tc)
				break;
		}
		return v;/*(0 < v) - (0 > v);*/
	}

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

			if (caseInsensitive) [[likely]] {
				res = EfficientWcsnicmp(partp, partq, len);
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

		int res = CompareStringA(LOCALE_INVARIANT, NORM_IGNORECASE, a, -1, b, -1);

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
