#pragma once
#include <type_traits>
#include "HashUtils.h"
#include "HashDefines.h"

template <typename _Alloc>
struct StaticSize
{
	constexpr static const uint32_t MAX_ELEMENTS = _Alloc::MAX_ELEMENTS;
	constexpr static const uint32_t KEY_COUNT = ComputeHashKeyCount(MAX_ELEMENTS);

	static_assert(MAX_ELEMENTS > 0, "Element count cannot be zero");

	constexpr static uint32_t GetKeyCount() noexcept
	{
		return KEY_COUNT;
	}
	constexpr static uint32_t GetHashMask() noexcept
	{
		return GetKeyCount() - 1;
	}
	constexpr static uint32_t GetMaxElements() noexcept
	{
		return MAX_ELEMENTS;
	}

	inline StaticSize() noexcept
	{
	}

private:
	DISABLE_COPY_MOVE(StaticSize)
};

struct DynamicSize
{
	explicit DynamicSize(const uint32_t count) noexcept
	    : keyCount(ComputeHashKeyCount(count))
	    , maxElements(count)
	{
	}

	inline uint32_t GetKeyCount() const noexcept
	{
		return keyCount;
	}
	inline uint32_t GetHashMask() const noexcept
	{
		return GetKeyCount() - 1;
	}
	inline uint32_t GetMaxElements() const noexcept
	{
		return maxElements;
	}

	const uint32_t keyCount;
	const uint32_t maxElements;

private:
	DISABLE_COPY_MOVE(DynamicSize)
};

struct DynamicSizeAllowInit
{
	inline DynamicSizeAllowInit() noexcept
	    : keyCount(0)
	    , maxElements(0)
	    , isInitialized(false)
	{
	}

	inline explicit DynamicSizeAllowInit(const uint32_t max_elements) noexcept
	    : keyCount(ComputeHashKeyCount(max_elements))
	    , maxElements(max_elements)
	    , isInitialized(true)
	{
	}

	inline bool Init(const uint32_t max_elements) noexcept
	{
		bool initialized = false;
		if (isInitialized.compare_exchange_strong(initialized, true))
		{
			maxElements = max_elements;
			keyCount = ComputeHashKeyCount(GetMaxElements());
			return true;
		}
		return false;
	}

	inline uint32_t GetKeyCount() const noexcept
	{
		return keyCount;
	}
	inline uint32_t GetHashMask() const noexcept
	{
		return keyCount - 1;
	}
	inline uint32_t GetMaxElements() const noexcept
	{
		return maxElements;
	}

private:
	uint32_t keyCount;
	uint32_t maxElements;
	std::atomic<bool> isInitialized;

	DISABLE_COPY_MOVE(DynamicSizeAllowInit)
};

#define RESOLVE_INTERNAL_BASE(_Alloc) \
	std::conditional< \
	    std::is_same<typename _Alloc::ALLOCATION_TYPE, ALLOCATION_TYPE_STATIC>::value, \
	    StaticSize<_Alloc>, \
	    typename std::conditional<std::is_same<typename _Alloc::ALLOCATION_TYPE, ALLOCATION_TYPE_HEAP>::value, \
	                              DynamicSize, \
	                              DynamicSizeAllowInit>::type>::type

template <typename K, typename V, typename _Alloc, bool MODE_INSERT_TAKE>
struct HashBaseNormal : public RESOLVE_INTERNAL_BASE(_Alloc)
{
protected:
	typedef typename RESOLVE_INTERNAL_BASE(_Alloc) Base;

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
		for (uint32_t i = 0; i < Base::GetMaxElements(); ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	HEAP_ONLY(AT)
	explicit HashBaseNormal(const uint32_t max_elements)
	    : Base(max_elements)
	    , m_keyStorage(max_elements)
	    , m_recycle(max_elements)
	    , m_usedNodes(0)
	{
		for (uint32_t i = 0; i < max_elements; ++i)
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
		for (uint32_t i = m_usedNodes; i < Base::GetMaxElements(); ++i)
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
		for (uint32_t i = --m_usedNodes;; --i)
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

	std::atomic<uint32_t> m_usedNodes;

	constexpr static const uint32_t _keys = sizeof(m_keyStorage);
	constexpr static const uint32_t _recycle = sizeof(m_recycle);

private:
	DISABLE_COPY_MOVE(HashBaseNormal)
};

template <typename K, typename V, typename _Alloc>
struct BaseAllocateItemsFromHeap : public RESOLVE_INTERNAL_BASE(_Alloc)
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
	explicit BaseAllocateItemsFromHeap(const uint32_t)
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

private:
	std::atomic<uint32_t> m_usedNodes;

	DISABLE_COPY_MOVE(BaseAllocateItemsFromHeap)
};
