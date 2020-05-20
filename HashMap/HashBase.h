#pragma once
#include <type_traits>
#include "HashUtils.h"
#include "HashDefines.h"

template <typename K, typename V, typename _Alloc, bool MODE_INSERT_TAKE>
struct HashBaseNormal : public _Alloc
{
protected:
	static_assert(_Alloc::COLLISION_SIZE > 0,
	              "!! LOGIC ERROR !! Collision bucket cannot be zero in this implementation");

	typedef typename _Alloc::ALLOCATION_TYPE AT;
	typedef KeyHashPairT<K> KeyHashPair;

	// Mode dependent typedefs
	typedef
	    typename std::conditional<MODE_INSERT_TAKE, KeyValueInsertTake<K, V>, KeyValueInsertRead<K, V>>::type KeyValue;

	typedef typename std::conditional<MODE_INSERT_TAKE,
	                                  BucketInsertTake<K, V, _Alloc::COLLISION_SIZE>,
	                                  BucketInsertRead<K, V, _Alloc::COLLISION_SIZE>>::type Bucket;

	STATIC_ONLY(AT)
	explicit HashBaseNormal() noexcept
	    : m_recycle()
	    , m_usedNodes(0)
	{
		for (size_t i = 0; i < _Alloc::GetMaxElements(); ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	HEAP_ONLY(AT)
	explicit HashBaseNormal(const size_t max_elements)
	    : _Alloc(max_elements)
	    , m_keyStorage(max_elements)
	    , m_recycle(max_elements)
	    , m_usedNodes(0)
	{
		for (size_t i = 0; i < max_elements; ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	EXT_ONLY(AT)
	HashBaseNormal() noexcept
	    : m_usedNodes(0)
	{
	}

	inline KeyValue* GetNextFreeKeyValue() noexcept
	{
		for (size_t i = m_usedNodes; i < _Alloc::GetMaxElements(); ++i)
		{
			KeyValue* pExpected = m_recycle[i];
			if (pExpected == nullptr)
				continue;
			if (m_recycle[i].compare_exchange_strong(pExpected, nullptr))
			{
				m_usedNodes++;
				return pExpected;
			}
		}
		return nullptr;
	}

	void ReleaseNode(KeyValue* pKeyValue) noexcept
	{
		for (size_t i = --m_usedNodes;; --i)
		{
			KeyValue* pNull = nullptr;
			if (m_recycle[i].compare_exchange_strong(pNull, pKeyValue))
			{
				break;
			}

			if (i == 0)
				break; // shouldn't get here
		}
	}

	Container<KeyValue, _Alloc::ALLOCATOR, _Alloc::MAX_ELEMENTS> m_keyStorage;
	Container<std::atomic<KeyValue*>, _Alloc::ALLOCATOR, _Alloc::MAX_ELEMENTS> m_recycle;

	std::atomic<size_t> m_usedNodes;

	constexpr static const size_t _keys = sizeof(m_keyStorage);
	constexpr static const size_t _recycle = sizeof(m_recycle);
};

template <typename K, typename V, typename _Alloc>
struct BaseAllocateItemsFromHeap : public _Alloc
{
protected:
	typedef typename _Alloc::ALLOCATION_TYPE AT;
	typedef KeyHashPairT<K> KeyHashPair;
	typedef KeyValueLinkedList<K, V> KeyValue;
	typedef BucketLinkedList<K, V> Bucket;

	STATIC_ONLY(AT)
	BaseAllocateItemsFromHeap()
	    : m_usedNodes(0)
	{
	}

	HEAP_ONLY(AT)
	explicit BaseAllocateItemsFromHeap(const size_t)
	    : m_usedNodes(0)
	{
	}

	EXT_ONLY(AT)
	BaseAllocateItemsFromHeap() noexcept
	    : m_usedNodes(0)
	{
	}

	inline KeyValue* GetNextFreeKeyValue() noexcept
	{
		m_usedNodes++;
		return new (std::nothrow) KeyValue();
	}

	inline void ReleaseNode(KeyValue* pKeyValue) noexcept
	{
		m_usedNodes--;
		delete pKeyValue;
	}

	std::atomic<size_t> m_usedNodes;
};
