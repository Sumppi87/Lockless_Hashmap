#pragma once
#include <atomic>
#include <type_traits>
#include <assert.h>
#include <functional>
#include "HashFunctions.h"
#include "HashUtils.h"

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

const size_t MIN_COLLISION_SIZE = 8;
typedef std::bool_constant<true> ALLOC_HEAP;
typedef std::bool_constant<false> ALLOC_STATIC;

template<typename T, size_t SIZE = 0>
struct Container :
	public std::conditional<SIZE == 0, PtrArray<T>, Array<T, SIZE>>::type
{
	typedef typename std::conditional<SIZE == 0, PtrArray<T>, Array<T, SIZE>>::type Base;
	typedef std::bool_constant<SIZE == 0> ALLOCATION_TYPE;

	template<typename TT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<TT, ALLOC_HEAP>::value>::type* = nullptr>
	Container(const size_t size, T* ptr)
		: Base(size, ptr) {}

	template<typename TT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<TT, ALLOC_HEAP>::value>::type* = nullptr>
	Container(void) = delete;

	template<typename TT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<TT, ALLOC_HEAP>::value>::type* = nullptr>
	Container(const size_t size)
		: Base(size) {}

	template<typename TT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<TT, ALLOC_STATIC>::value>::type* = nullptr>
	Container(void) {}

	Container(Container&&) = delete;
	Container(Container&) = delete;
	Container& operator=(Container&) = delete;
	Container& operator=(Container&&) = delete;
};

template <size_t COLLISION_SIZE_HINT>
struct CollisionCalc
{
	constexpr static const size_t COLLISION_SIZE =
		COLLISION_SIZE_HINT > MIN_COLLISION_SIZE
		? COLLISION_SIZE_HINT
		: MIN_COLLISION_SIZE;
};

template <size_t COLLISION_SIZE_HINT = 0, size_t MAX_ELEMENTS = 0>
struct StaticParams : public CollisionCalc<COLLISION_SIZE_HINT>
{
	static_assert(MAX_ELEMENTS > 0, "Element count cannot be zero");
	constexpr static const size_t KEY_COUNT = ComputeHashKeyCount(MAX_ELEMENTS);
	constexpr size_t GetKeyCount() const { return KEY_COUNT; }
	constexpr size_t GetMaxElements() const { return MAX_ELEMENTS; }
};

template <size_t COLLISION_SIZE_HINT = 0, size_t ... Args>
struct PtrParams : public CollisionCalc<COLLISION_SIZE_HINT>
{
	PtrParams(const size_t count)
		: keyCount(ComputeHashKeyCount(count))
		, maxElements(count) {}

	size_t GetKeyCount() const { return keyCount; }
	size_t GetMaxElements() const { return maxElements; }
	const size_t keyCount;
	const size_t maxElements;
};


template<typename K,
	typename V,
	size_t ... Args>
	class Hash : public std::conditional<(sizeof...(Args) > 0),
	StaticParams<Args ...>, PtrParams<Args ...>>::type
{
	static_assert(std::is_trivially_copyable<K>::value, "Template type K must be trivially copyable.");
private:
	typedef KeyHashPairT<K> KeyHashPair;
	constexpr static const bool IS_LOCK_FREE = KeyHashPair::IsLockFree();
	typedef std::bool_constant<sizeof...(Args) == 0> HEAP_ALLOC;

	typedef typename std::conditional<(sizeof...(Args) > 0),
		StaticParams<Args ...>, PtrParams<Args ...>>::type ParamsBase;

public:
	typedef KeyValueT<K, V> KeyValue;
	typedef BucketT<K, V, ParamsBase::COLLISION_SIZE> Bucket;

	template<typename TT = HEAP_ALLOC, typename std::enable_if<std::is_same<TT, ALLOC_STATIC>::value>::type* = nullptr>
	Hash()
		: m_hash()
		, m_recycle()
		, m_usedNodes(0)
		, seed(GenerateSeed())
	{
		for (size_t i = 0; i < ParamsBase::GetMaxElements(); ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	template<typename TT = HEAP_ALLOC, typename std::enable_if<std::is_same<TT, ALLOC_HEAP>::value>::type* = nullptr>
	Hash(const size_t max_elements)
		: ParamsBase(max_elements)
		, m_hash(ComputeHashKeyCount(max_elements))
		, m_keyStorage(max_elements)
		, m_recycle(max_elements)
		, m_usedNodes(0)
		, seed(GenerateSeed())
	{
		for (size_t i = 0; i < ParamsBase::GetMaxElements(); ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	template<typename TT = HEAP_ALLOC, typename std::enable_if<std::is_same<TT, ALLOC_HEAP>::value>::type* = nullptr>
	Hash(size_t max_elements, Bucket* hash, KeyValue* keyStorage, std::atomic<KeyValue*>* keyRecycle)
		: ParamsBase(max_elements)
		, m_hash(ComputeHashKeyCount(max_elements), hash)
		, m_keyStorage(max_elements, keyStorage)
		, m_recycle(max_elements, keyRecycle)
		, m_usedNodes(0)
		, seed(GenerateSeed())
	{
		for (size_t i = 0; i < ParamsBase::GetMaxElements(); ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	void Add(const K& k, const V& v)
	{
		const size_t h = hash(k, seed);
		const size_t index = h % ParamsBase::GetKeyCount();

		KeyValue* pKeyValue = GetNextFreeKeyValue();
		pKeyValue->v = v;
		pKeyValue->k = KeyHashPair{ h, k };
		m_hash;
		m_hash[index].Add(pKeyValue);
	}

	const V Take(const K& k)
	{
		V ret = V();

		const size_t h = hash(k, seed);
		const size_t index = h % ParamsBase::GetKeyCount();
		KeyValue* pKeyValue = nullptr;
		if (m_hash[index].TakeValue(k, h, &pKeyValue))
		{
			// Value was found
			ret = pKeyValue->v;

			ReleaseNode(pKeyValue);
		}
		return ret;
	}

	void Take(const K& k, const std::function<bool(const V&)>& receiver)
	{
		const size_t h = hash(k, seed);
		const size_t index = h % ParamsBase::GetKeyCount();
		std::function<void(KeyValue*)> release = std::bind(&Hash::ReleaseNode, this, std::placeholders::_1);
		m_hash[index].TakeValue(k, h, receiver, release);
	}

private:
	KeyValue* GetNextFreeKeyValue()
	{
		KeyValue* pRet = nullptr;
		for (size_t i = m_usedNodes; i < ParamsBase::GetKeyCount(); ++i)
		{
			KeyValue* pExpected = m_recycle[i];
			if (pExpected == nullptr)
				continue;
			if (m_recycle[i].compare_exchange_strong(pExpected, nullptr))
			{
				pExpected->Reset();
				pRet = pExpected;
				m_usedNodes++;
				break;
			}
		}
		return pRet;
	}

	void ReleaseNode(KeyValue* pKeyValue)
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

private:

	template <size_t COLLISION_SIZE_HINT = 0, size_t MAX_ELEMENTS = 0>
	struct ContainerParamExtractor
	{
		constexpr static const size_t MAX_ELEMENTS = MAX_ELEMENTS;
		constexpr static const size_t KEY_COUNT = MAX_ELEMENTS > 0 ? ComputeHashKeyCount(MAX_ELEMENTS) : 0;
	};
	Container<Bucket, ContainerParamExtractor<Args ...>::KEY_COUNT> m_hash;
	Container<KeyValue, ContainerParamExtractor<Args ...>::MAX_ELEMENTS> m_keyStorage;
	Container<std::atomic<KeyValue*>, ContainerParamExtractor<Args ...>::MAX_ELEMENTS> m_recycle;
	std::size_t m_keyCount;
	std::size_t m_maxElements;
	std::atomic<size_t> m_usedNodes;

	const size_t seed;

	constexpr static const bool _K = std::is_trivially_copyable<K>::value;
	constexpr static const bool _V = std::is_trivially_copyable<V>::value;

	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _keys = sizeof(m_keyStorage);
	constexpr static const size_t _recycle = sizeof(m_recycle);
	constexpr static const size_t _key = sizeof(KeyValue);
	constexpr static const size_t _bucket = sizeof(Bucket);
};
