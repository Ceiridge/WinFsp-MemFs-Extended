#pragma once
#include <memory>
#include <type_traits>

template <typename T>
class DynamicStruct {
public:
	static_assert(std::is_trivially_copyable_v<T>, "T must be a trivially copyable type, for example a struct");

	DynamicStruct() = default;

	/**
	 * \brief Allocates a dynamic struct
	 * \param size The exact size in bytes of the struct
	 */
	explicit DynamicStruct(std::size_t size) {
		const bool fraction = size % sizeof(int64_t) != 0;
		const std::size_t requiredInt64s = size / sizeof(int64_t) + (fraction ? 1 : 0); // + 1 if not divisible

		wantedSize_ = size;
		size_ = requiredInt64s * sizeof(int64_t);
		data_ = std::make_unique<int64_t[]>(requiredInt64s);
	}

	~DynamicStruct() = default;

	DynamicStruct(const DynamicStruct& other) {
		wantedSize_ = other.wantedSize_;
		size_ = other.size_;
		data_ = std::make_unique<int64_t[]>(size_);
		std::memcpy(data_.get(), other.data_.get(), size_ * sizeof(int64_t));
	}

	DynamicStruct(DynamicStruct&& other) noexcept = default;

	DynamicStruct& operator=(const DynamicStruct& other) {
		if (this != &other) {
			wantedSize_ = other.wantedSize_;
			size_ = other.size_;
			data_ = std::make_unique<int64_t[]>(size_);
			std::memcpy(data_.get(), other.data_.get(), size_ * sizeof(int64_t));
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
		return this->size_;
	}

	[[nodiscard]] std::size_t WantedByteSize() const {
		return this->wantedSize_;
	}

	[[nodiscard]] bool HoldsStruct() const {
		return this->size_ != 0;
	}

private:
	std::size_t size_{0};
	std::size_t wantedSize_{ 0 };
	std::unique_ptr<int64_t[]> data_;
};
