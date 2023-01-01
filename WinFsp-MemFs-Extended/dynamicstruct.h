#pragma once

#include "globalincludes.h"


template <typename T>
class DynamicStruct {
public:
	static_assert(std::is_trivially_copyable_v<T>, "T must be a trivially copyable type, for example a struct");

	DynamicStruct() = default;

	/**
	 * \brief Allocates a dynamic struct
	 * \param size The exact size in bytes of the struct
	 */
	explicit DynamicStruct(const std::size_t size) {
		wantedSize_ = size;
		data_ = std::unique_ptr<int64_t[]>(new int64_t[this->RequiredInt64Amount()]);
	}

	~DynamicStruct() = default;

	DynamicStruct(const DynamicStruct& other) {
		wantedSize_ = other.wantedSize_;
		data_ = std::unique_ptr<int64_t[]>(new int64_t[this->RequiredInt64Amount()]);
		std::memcpy(data_.get(), other.data_.get(), this->ByteSize());
	}

	DynamicStruct(DynamicStruct&& other) noexcept = default;

	DynamicStruct& operator=(const DynamicStruct& other) {
		if (this != &other) {
			wantedSize_ = other.wantedSize_;
			data_ = std::unique_ptr<int64_t[]>(new int64_t[this->RequiredInt64Amount()]);
			std::memcpy(data_.get(), other.data_.get(), this->ByteSize());
		}

		return *this;
	}

	DynamicStruct& operator=(DynamicStruct&& other) noexcept = default;


	T* Struct() {
		return reinterpret_cast<T*>(data_.get());
	}

	[[nodiscard]] const T* Struct() const {
		return reinterpret_cast<const T*>(data_.get());
	}

	[[nodiscard]] std::size_t ByteSize() const {
		return this->RequiredInt64Amount() * sizeof(int64_t);
	}

	[[nodiscard]] std::size_t WantedByteSize() const {
		return this->wantedSize_;
	}

	[[nodiscard]] bool HoldsStruct() const {
		if (this->data_) {
			return true;
		}

		return false;
	}

private:
	[[nodiscard]] std::size_t RequiredInt64Amount() const {
		const bool fraction = this->wantedSize_ % sizeof(int64_t) != 0;
		return this->wantedSize_ / sizeof(int64_t) + (fraction ? 1 : 0); // + 1 if not divisible
	}

	std::size_t wantedSize_{0};
	std::unique_ptr<int64_t[]> data_;
};
