#pragma once

#include <exception>
#include <winfsp/winfsp.h>

namespace Memfs {
	class CreateException final : public std::exception {
	public:
		explicit CreateException(const NTSTATUS status);

		[[nodiscard]] char const* what() const override;
		[[nodiscard]] NTSTATUS Which() const;

	private:
		NTSTATUS status;
	};

	class FileNameTooLongException final : public std::exception {
		[[nodiscard]] char const* what() const override;
	};
}
