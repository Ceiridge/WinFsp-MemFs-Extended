#include <Windows.h>

#include "utils.h"

namespace Memfs::Utils {
	uint64_t GetSystemTime() {
		FILETIME fileTime;
		GetSystemTimeAsFileTime(&fileTime);
		return ((PLARGE_INTEGER)&fileTime)->QuadPart;
	}
}
