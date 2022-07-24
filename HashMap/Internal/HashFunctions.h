#pragma once
#include <stdint.h>

constexpr inline uint32_t hash(char key, uint32_t seed = 0) noexcept
{
	return uint32_t(key) ^ seed;
}
constexpr inline uint32_t hash(unsigned char key, uint32_t seed = 0) noexcept
{
	return uint32_t(key) ^ seed;
}
constexpr inline uint32_t hash(signed char key, uint32_t seed = 0) noexcept
{
	return uint32_t(key) ^ seed;
}
constexpr inline uint32_t hash(unsigned short key, uint32_t seed = 0) noexcept
{
	return uint32_t(key) ^ seed;
}
constexpr inline uint32_t hash(short key, uint32_t seed = 0) noexcept
{
	return uint32_t(key) ^ seed;
}
constexpr inline uint32_t hash(unsigned int key, uint32_t seed = 0) noexcept
{
	return key ^ seed;
}
constexpr inline uint32_t hash(int key, uint32_t seed = 0) noexcept
{
	return uint32_t(key) ^ seed;
}
constexpr inline uint32_t hash(uint64_t key, uint32_t seed = 0) noexcept
{
	if constexpr (sizeof(uint32_t) >= sizeof(uint64_t))
	{
		return key ^ seed;
	}
	return uint32_t(((key >> (8 * sizeof(uint32_t) - 1)) ^ key) & (~0U)) ^ seed;
}
constexpr inline uint32_t hash(int64_t key, uint32_t seed = 0) noexcept
{
	return hash(uint64_t(key), seed);
}

template <typename T>
inline uint32_t hash(const T& t, uint32_t seed) noexcept(noexcept(hash(t)))
{
	return hash(t) ^ seed;
};
