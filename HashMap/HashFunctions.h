#pragma once
#include <stdint.h>

constexpr inline size_t hash(char key, size_t seed = 0) noexcept
{
	return size_t(key) ^ seed;
}
constexpr inline size_t hash(unsigned char key, size_t seed = 0) noexcept
{
	return size_t(key) ^ seed;
}
constexpr inline size_t hash(signed char key, size_t seed = 0) noexcept
{
	return size_t(key) ^ seed;
}
constexpr inline size_t hash(unsigned short key, size_t seed = 0) noexcept
{
	return size_t(key) ^ seed;
}
constexpr inline size_t hash(short key, size_t seed = 0) noexcept
{
	return size_t(key) ^ seed;
}
constexpr inline size_t hash(unsigned int key, size_t seed = 0) noexcept
{
	return key ^ seed;
}
constexpr inline size_t hash(int key, size_t seed = 0) noexcept
{
	return size_t(key) ^ seed;
}
constexpr inline size_t hash(uint64_t key, size_t seed = 0) noexcept
{
	if constexpr (sizeof(size_t) >= sizeof(uint64_t))
	{
		return key ^ seed;
	}
	return size_t(((key >> (8 * sizeof(size_t) - 1)) ^ key) & (~0U)) ^ seed;
}
constexpr inline size_t hash(int64_t key, size_t seed = 0) noexcept
{
	return hash(uint64_t(key), seed);
}
template <class T>
inline size_t hash(const T* key, size_t seed) noexcept
{
	return hash(reinterpret_cast<size_t>(key), seed);
}

// template<typename T>
// inline size_t hash(const T& t, size_t seed = 0) = delete;

template <typename T>
inline size_t hash(const T& t, size_t seed) noexcept(noexcept(hash(t)))
{
	return hash(t) ^ seed;
};
