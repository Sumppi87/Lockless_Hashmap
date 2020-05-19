#pragma once
#include <type_traits>

#define HEAP_ONLY(ALLOCATION_TYPE) \
	template <typename AT = ALLOCATION_TYPE, \
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
#define STATIC_ONLY(ALLOCATION_TYPE) \
	template <typename AT = ALLOCATION_TYPE, \
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type* = nullptr>
#define EXT_ONLY(ALLOCATION_TYPE) \
	template <typename AT = ALLOCATION_TYPE, \
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>

// Number of slots in a single bucket
const size_t MIN_COLLISION_SIZE = 16;

enum class AllocatorType
{
	STATIC,
	HEAP,
	EXTERNAL
};

enum class MapMode
{
	//! \brief
	// Hash supports following lock-free operations in parallel:
	//	* Inserting items
	//	* Reading items with Take functions (i.e. read item is removed from map)
	//! \constrains	Key of type <K> must fulfill requirements in std::atomic<K> https://en.cppreference.com/w/cpp/atomic/atomic
	PARALLEL_INSERT_TAKE = 0b001,

	//! \brief
	// Hash supports following lock-free operations in parallel:
	//	* Inserting items
	//	* Reading items with Value functions (i.e. read item is not removed from map)
	//! \constrains Once an item is inserted into the map, it cannot be removed. Key must fulfill std::is_default_constructible
	PARALLEL_INSERT_READ = 0b010,

	//! \brief	Special case of Hash-map, where number of inserted elements is not limited
	//!			Inserted key-value pairs are stored in heap, which is allocated at insertion time
	//! \Note	This operation mode heap allocates, regardless of what Allocator has been chosen.
	//!			However, hashing map is constant size, so adding a large number of items to small hash map can affect performance
	// \details Hash supports following lock-free operations in parallel:
	//	* Inserting items
	//	* Reading items with Value functions (i.e. read item is not removed from map)
	//! \constrains Once an item is inserted into the map, it cannot be removed. Key must fulfill std::is_default_constructible
	PARALLEL_INSERT_READ_GROW_FROM_HEAP = 0b100
};

//! \brief
typedef std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_TAKE> MODE_INSERT_TAKE;

//! \brief
typedef std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_READ> MODE_INSERT_READ;

//! \brief Special case of PARALLEL_INSERT_READ
typedef std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_READ_GROW_FROM_HEAP> MODE_INSERT_READ_HEAP_BUCKET;

//! \brief Allocate memory from heap
typedef std::integral_constant<AllocatorType, AllocatorType::HEAP> ALLOCATION_TYPE_HEAP;

//! \brief Allocate memory from statically (i.e. raw arrays)
typedef std::integral_constant<AllocatorType, AllocatorType::STATIC> ALLOCATION_TYPE_STATIC;

//! \brief Allocate memory from external source
typedef std::integral_constant<AllocatorType, AllocatorType::EXTERNAL> ALLOCATION_TYPE_EXTERNAL;

typedef std::bool_constant<true> TRUE_TYPE;
typedef std::bool_constant<false> FALSE_TYPE;
