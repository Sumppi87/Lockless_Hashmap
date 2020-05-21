#pragma once
#include <type_traits>

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

#define HEAP_ONLY(ALLOCATION_TYPE) \
	template <typename AT = ALLOCATION_TYPE, \
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
#define STATIC_ONLY(ALLOCATION_TYPE) \
	template <typename AT = ALLOCATION_TYPE, \
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type* = nullptr>
#define EXT_ONLY(ALLOCATION_TYPE) \
	template <typename AT = ALLOCATION_TYPE, \
	          typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>

#define MODE_TAKE_ONLY(_MODE) \
	template <typename _M = _MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>

#define MODE_NOT_TAKE(_MODE) \
	template <typename _M = _MODE, typename std::enable_if<!std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>

#define IS_INSERT_TAKE(x) std::is_same<std::integral_constant<MapMode, x>, MODE_INSERT_TAKE>::value
#define IS_INSERT_READ_FROM_HEAP(x) \
	std::is_same<std::integral_constant<MapMode, x>, MODE_INSERT_READ_HEAP_BUCKET>::value

#define HEAP_ONLY_IMPL \
	template <typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type*>
#define STATIC_ONLY_IMPL \
	template <typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type*>
#define EXT_ONLY_IMPL \
	template <typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type*>

#define MODE_TAKE_ONLY_IMPL \
	template <typename _M, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>

#define MODE_NOT_TAKE_IMPL \
	template <typename _M, typename std::enable_if<!std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>

// Convenience macro for resolving Hash base-class
#define RESOLVE_BASE_CLASS(OP_MODE, K, V, _Alloc) \
	std::conditional< \
	    std::is_same<std::integral_constant<MapMode, OP_MODE>, MODE_INSERT_READ_HEAP_BUCKET>::value, \
	    BaseAllocateItemsFromHeap<K, V, _Alloc>, \
	    HashBaseNormal<K, \
	                   V, \
	                   _Alloc, \
	                   std::is_same<std::integral_constant<MapMode, OP_MODE>, MODE_INSERT_TAKE>::value>>::type

#define DISABLE_COPY_MOVE(_class) \
	inline _class& operator=(const _class&) noexcept = delete; \
	inline _class& operator=(const _class&&) noexcept = delete; \
	inline _class(const _class&) noexcept = delete; \
	inline _class(const _class&&) noexcept = delete;

// Number of slots in a single bucket
const uint32_t DEFAULT_COLLISION_SIZE = 16;

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
