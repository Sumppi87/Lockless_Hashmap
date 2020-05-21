#pragma once
#include <type_traits>
#include <random>
#include "HashDefines.h"

static uint32_t GenerateSeed() noexcept
{
	std::random_device rd{};
	std::mt19937 engine{rd()};
	return engine();
}

constexpr static uint32_t GetNextPowerOfTwo(const uint32_t value) noexcept
{
	// Algorithm from https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2

	uint32_t v = value;
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

constexpr static uint32_t ComputeHashKeyCount(const uint32_t count) noexcept
{
	return GetNextPowerOfTwo(count * 2);
}
