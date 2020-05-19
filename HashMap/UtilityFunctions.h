#pragma once
#include <type_traits>
#include <random>
#include "HashDefines.h"

static size_t GenerateSeed() noexcept
{
	constexpr auto sizeof_size_t = sizeof(size_t);
	static_assert(sizeof_size_t == 4 || sizeof_size_t == 8, "Unsupported platform");

	std::random_device rd{};
	// Use Mersenne twister engine to generate pseudo-random numbers.
	if constexpr (sizeof_size_t == 4U)
	{
		std::mt19937 engine{ rd() };
		return engine();
	}
	else if constexpr (sizeof_size_t == 8U)
	{
		std::mt19937_64 engine{ rd() };
		return engine();
	}
}

constexpr static size_t GetNextPowerOfTwo(const size_t value) noexcept
{
	// Algorithm from https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2

	size_t v = value;
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

constexpr static size_t ComputeHashKeyCount(const size_t count) noexcept
{
	return GetNextPowerOfTwo(count * 2);
}