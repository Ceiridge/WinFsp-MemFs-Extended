#pragma once

#include <exception>
#include <winfsp/winfsp.h>

namespace Memfs {
	class CreateException final : public std::exception {
	public:
		explicit CreateException(const NTSTATUS status);

		[[nodiscard]] char const* what() const override;

	private:
		NTSTATUS status;
	};
}
