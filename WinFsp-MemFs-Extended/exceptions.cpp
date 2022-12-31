#include "globalincludes.h"
#include "exceptions.h"

namespace Memfs {
	CreateException::CreateException(const NTSTATUS status) : status(status) {
	}

	char const* CreateException::what() const {
		return (std::string("Create Exception with NTStatus: ") + std::to_string(this->status)).c_str();
	}

	NTSTATUS CreateException::Which() const {
		return this->status;
	}

	char const* FileNameTooLongException::what() const {
		return "The file name is too long.";
	}
}
