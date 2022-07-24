#pragma once
#include <type_traits>
#include <assert.h>
#include "HashDefines.h"
#include "Debug.h"

template <typename T>
struct Array
{
	inline T& operator[](const uint32_t idx) noexcept
	{
#ifdef _DEBUG
		assert(idx < _size);
#endif // DEBUG
		return _array[idx];
	}
	inline const T& operator[](const uint32_t idx) const noexcept
	{
#ifdef _DEBUG
		assert(idx < _size);
#endif // DEBUG
		return _array[idx];
	}

	T* _array;
	uint32_t _size;
};

template <typename T, uint32_t SIZE>
struct StaticArray
{
	static_assert(SIZE > 0, "Size cannot be zero");

	inline T& operator[](const uint32_t idx) noexcept
	{
#ifdef _DEBUG
		assert(idx < SIZE);
#endif // DEBUG
		return _array[idx];
	}
	inline const T& operator[](const uint32_t idx) const noexcept
	{
#ifdef _DEBUG
		assert(idx < SIZE);
#endif // DEBUG
		return _array[idx];
	}

	T _array[SIZE];
};

template <typename T>
struct PtrArray : public Array<T>
{
	constexpr const static auto _T = sizeof(T);

	explicit PtrArray(const uint32_t size) noexcept
	{
		Array<T>::_array = new T[size]{};
		Array<T>::_size = size;
	}

	inline ~PtrArray() noexcept
	{
		delete[] Array<T>::_array;
		Array<T>::_array = nullptr;
	}

	DISABLE_COPY_MOVE(PtrArray)
};

template <typename T>
struct ExtArray : public Array<T>
{
	constexpr const static auto _T = sizeof(T);

	inline ExtArray(T* array, const uint32_t size) noexcept
	{
		Init(array, size);
	}

	inline void Init(T* ptr, const uint32_t size) noexcept
	{
		memset(ptr, 0, size * sizeof(T));
		Array<T>::_array = ptr;
		Array<T>::_size = size;
	}

	DISABLE_COPY_MOVE(ExtArray)
};

template <typename T, AllocatorType TYPE, uint32_t SIZE = 0>
struct Container
    : public std::conditional<
          std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_HEAP>::value, // condition of outer
          PtrArray<T>, // If 1st condition is true
          typename std::conditional<
              std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_STATIC>::value, // Condition of
                                                                                                        // inner (and
                                                                                                        // non-true case
                                                                                                        // of outer)
              StaticArray<T, SIZE>, // If 2nd condition is true
              ExtArray<T> // If 2nd condition is false
              >::type // Extract the type from inner std::conditional
          >::type // Extract the type from outer std::conditional
{
	typedef typename std::conditional<
	    std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_HEAP>::value, // condition of outer
	    PtrArray<T>, // If 1st condition is true
	    typename std::conditional<
	        std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_STATIC>::value, // Condition of
	                                                                                                  // inner (and
	                                                                                                  // non-true case
	                                                                                                  // of outer)
	        StaticArray<T, SIZE>, // If 2nd condition is true
	        ExtArray<T>>::type>::type Base;

	typedef std::integral_constant<AllocatorType, TYPE> ALLOCATION_TYPE;

	template <typename AT = ALLOCATION_TYPE,
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	inline Container(void) noexcept
	    : Base(nullptr, 0)
	{
	}

	template <typename AT = ALLOCATION_TYPE,
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	inline Container(T* ptr, const uint32_t size) noexcept
	    : Base(ptr, size)
	{
	}

	template <typename AT = ALLOCATION_TYPE,
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	inline void Init(T* ptr, const uint32_t size) noexcept
	{
		Base::Init(ptr, size);
	}

	template <typename AT = ALLOCATION_TYPE,
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	inline Container(void) = delete;

	template <typename AT = ALLOCATION_TYPE,
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	inline Container(const uint32_t size) noexcept
	    : Base(size)
	{
	}

	template <typename AT = ALLOCATION_TYPE,
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	constexpr static const uint32_t NeededHeap(const uint32_t size) noexcept
	{
		return sizeof(T) * size;
	}

	template <typename AT = ALLOCATION_TYPE,
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type* = nullptr>
	inline Container(void) noexcept
	    : Base()
	{
	}

	DISABLE_COPY_MOVE(Container)
};
