#pragma once
#include <type_traits>
#include "HashUtils.h"
#include "HashDefines.h"

template<typename K, typename V, typename _Alloc, bool MODE_INSERT_TAKE>
struct HashBaseNormal
{
	static_assert(_Alloc::COLLISION_SIZE > 0, "!! LOGIC ERROR !! Collision bucket cannot be zero in this implementation");

	typedef typename _Alloc::ALLOCATION_TYPE AT;
	typedef KeyHashPairT<K> KeyHashPair;

	// Mode dependent typedefs
	typedef typename std::conditional<MODE_INSERT_TAKE,
		KeyValueInsertTake<K, V>,
		KeyValueInsertRead<K, V>
	>::type
		KeyValue;

	typedef typename std::conditional<MODE_INSERT_TAKE,
		BucketInsertTake<K, V, _Alloc::COLLISION_SIZE>,
		BucketInsertRead<K, V, _Alloc::COLLISION_SIZE>
	>::type
		Bucket;

	STATIC_ONLY(AT) explicit HashBaseNormal() noexcept
		: m_recycle()
	{
		for (size_t i = 0; i < _Alloc::GetMaxElements(); ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	HEAP_ONLY(AT) explicit HashBaseNormal(const size_t max_elements)
		: m_keyStorage(max_elements)
		, m_recycle(max_elements)
	{
		for (size_t i = 0; i < max_elements; ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	EXT_ONLY(AT) HashBaseNormal() noexcept
	{
	}

	Container<KeyValue, _Alloc::ALLOCATOR, _Alloc::MAX_ELEMENTS> m_keyStorage;
	Container<std::atomic<KeyValue*>, _Alloc::ALLOCATOR, _Alloc::MAX_ELEMENTS> m_recycle;

	constexpr static const size_t _keys = sizeof(m_keyStorage);
	constexpr static const size_t _recycle = sizeof(m_recycle);
};

template<typename K, typename V, typename _Alloc>
struct BaseAllocateItemsFromHeap
{
	typedef typename _Alloc::ALLOCATION_TYPE AT;
	typedef KeyHashPairT<K> KeyHashPair;
	typedef KeyValueLinkedList<K, V> KeyValue;
	typedef BucketLinkedList<K, V> Bucket;

	STATIC_ONLY(AT) BaseAllocateItemsFromHeap() {}

	HEAP_ONLY(AT) explicit BaseAllocateItemsFromHeap(const size_t) {}

	EXT_ONLY(AT) BaseAllocateItemsFromHeap() noexcept {}
};
