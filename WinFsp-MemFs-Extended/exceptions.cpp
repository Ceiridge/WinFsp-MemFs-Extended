#include "exceptions.h"

#include <string>

namespace Memfs {
	CreateException::CreateException(const NTSTATUS status) : status(status) {
	}

	char const* CreateException::what() const {
		return (std::string("Create Exception with NTStatus: ") + std::to_string(this->status)).c_str();
	}
}
