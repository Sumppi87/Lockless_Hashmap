#pragma once
#include <atomic>
#include <type_traits>
#include <mutex>
#include <assert.h>
#include "Buckets.h"
#include "Container.h"
#include "Debug.h"

template <AllocatorType TYPE>
struct Allocator
{
	constexpr static const std::integral_constant<AllocatorType, TYPE> ALLOCATOR{};
	typedef std::integral_constant<AllocatorType, TYPE> ALLOCATION_TYPE;
};

template <uint32_t COLLISION_SIZE, uint32_t MAX_ELEMENTS = 0, uint32_t KEY_COUNT = 0>
struct StaticSizes
{
	constexpr static const uint32_t MAX_ELEMENTS = MAX_ELEMENTS;
	constexpr static const uint32_t KEY_COUNT = KEY_COUNT;
	constexpr static const uint32_t COLLISION_SIZE = COLLISION_SIZE;

	typedef typename std::integral_constant<uint32_t, COLLISION_SIZE> BUCKET_SIZE;
};

template <typename K>
struct GeneralKeyReqs
{
	typedef typename std::bool_constant<std::is_default_constructible<K>::value && std::is_copy_assignable<K>::value>
	    GENERAL_REQS_MET;

	constexpr static const bool VALID_KEY_TYPE = GENERAL_REQS_MET::value;

	constexpr const static bool AssertAll() noexcept
	{
		static_assert(VALID_KEY_TYPE,
		              "Key type must fulfill std::is_default_constructible<K>::value and std::is_copy_assignable<K>");
		return true;
	}
};

template <typename K, bool CHECK_FOR_ATOMIC_ACCESS>
struct AtomicsRequired
{
	static constexpr const GeneralKeyReqs<K> GENERAL_REQS{};

	typedef typename std::bool_constant<std::is_trivially_copyable<K>::value && std::is_copy_constructible<K>::value
	                                    && std::is_move_constructible<K>::value && std::is_copy_assignable<K>::value
	                                    && std::is_move_assignable<K>::value>
	    STD_ATOMIC_REQS_MET;

	constexpr static const bool STD_ATOMIC_AVAILABLE = STD_ATOMIC_REQS_MET::value;

	constexpr static const bool STD_ATOMIC_ALWAYS_LOCK_FREE = []() {
		if constexpr (STD_ATOMIC_REQS_MET::value)
			return std::atomic<K>::is_always_lock_free && std::atomic<KeyHashPairT<K>>::is_always_lock_free;
		return false;
	}();

	constexpr static const bool VALID_KEY_TYPE =
	    GENERAL_REQS.VALID_KEY_TYPE && STD_ATOMIC_AVAILABLE
	    && ((CHECK_FOR_ATOMIC_ACCESS && STD_ATOMIC_ALWAYS_LOCK_FREE) || !CHECK_FOR_ATOMIC_ACCESS);

	typedef std::bool_constant<CHECK_FOR_ATOMIC_ACCESS> CHECK_TYPE;

	template <typename TYPE = CHECK_TYPE,
	          typename std::enable_if<std::is_same<TYPE, TRUE_TYPE>::value>::type* = nullptr>
	__declspec(deprecated(
	    "** sizeof(K) is too large for lock-less access, "
	    "define `SKIP_ATOMIC_LOCKLESS_CHECKS` to suppress this warning **")) constexpr static bool NotLockFree() noexcept
	{
		return false;
	}

	template <typename TYPE = CHECK_TYPE,
	          typename std::enable_if<std::is_same<TYPE, FALSE_TYPE>::value>::type* = nullptr>
	constexpr static bool NotLockFree() noexcept
	{
		return false;
	}

	constexpr static bool IsAlwaysLockFree() noexcept
	{
#ifndef SKIP_ATOMIC_LOCKLESS_CHECKS
		if constexpr (!STD_ATOMIC_ALWAYS_LOCK_FREE)
		{
			return NotLockFree();
		}
#else
#pragma message("Warning: Hash-map lockless operations are not guaranteed, " \
		"remove define `SKIP_ATOMIC_LOCKLESS_CHECKS` to ensure lock-less access")
#endif
		return true;
	}

	constexpr const static bool AssertAll() noexcept
	{
		GENERAL_REQS.AssertAll();

		static_assert(STD_ATOMIC_AVAILABLE,
		              "In MapMode::PARALLEL_INSERT_TAKE mode, key-type must fulfill std::atomic requirements.");

		IsAlwaysLockFree();

		return true;
	}
};

template <typename K, MapMode OP_MODE>
struct HashKeyProperties
    : public std::conditional<std::is_same<std::integral_constant<MapMode, OP_MODE>, MODE_INSERT_TAKE>::value,
                              AtomicsRequired<K, true>,
                              GeneralKeyReqs<K>>::type
{
	typedef typename std::integral_constant<MapMode, OP_MODE> MODE;

	typedef typename std::conditional<std::is_same<std::integral_constant<MapMode, OP_MODE>, MODE_INSERT_TAKE>::value,
	                                  AtomicsRequired<K, true>,
	                                  GeneralKeyReqs<K>>::type Base;

	constexpr static const bool VALID_KEY_TYPE = Base::VALID_KEY_TYPE;

	constexpr static bool IsValidKeyForMode() noexcept
	{
		return VALID_KEY_TYPE;
	}

	constexpr const static bool AssertAll() noexcept
	{
		return Base::AssertAll();
	}
};

template <typename K, MapMode OP_MODE>
struct KeyPropertyValidator : public HashKeyProperties<K, OP_MODE>
{
	typedef typename HashKeyProperties<K, OP_MODE> KeyProps;
	static_assert(KeyProps::AssertAll(), "Hash key failed to meet requirements");
};

template <typename K, typename _Alloc>
struct DefaultModeSelector
{
	typedef typename std::bool_constant<
	    std::is_same<typename _Alloc::BUCKET_SIZE::type, std::integral_constant<uint32_t, 0>::type>::value>
	    ZERO_SIZE_BUCKET; // Bucket size is not fixed -> Items are allocated from heap as needed

	constexpr static const MapMode MODE =
	    std::conditional<ZERO_SIZE_BUCKET::value,
	                     MODE_INSERT_READ_HEAP_BUCKET,
	                     std::conditional<AtomicsRequired<K, false>::STD_ATOMIC_AVAILABLE, // Check if requirements for
	                                                                                       // std::atomic is met by K
	                                      MODE_INSERT_TAKE, // If requirements are met
	                                      MODE_INSERT_READ // If requirements are not met
	                                      >::type>::type // Extract type selected by std::conditional (i.e.
	                                                     // MODE_INSERT_TAKE or MODE_INSERT_TAKE>
	    ::value; // Extract actual type from selected mode
};
