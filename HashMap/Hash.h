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
	typedef std::bool_constant<sizeof...(Args) == 0> ALLOCATION_TYPE;

	typedef typename std::conditional<(sizeof...(Args) > 0),
		StaticParams<Args ...>, PtrParams<Args ...>>::type ParamsBase;

public:
	typedef KeyValueT<K, V> KeyValue;
	typedef BucketT<K, V, ParamsBase::COLLISION_SIZE> Bucket;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_STATICALLY>::value>::type* = nullptr>
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

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_FROM_HEAP>::value>::type* = nullptr>
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

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_FROM_HEAP>::value>::type* = nullptr>
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

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_FROM_HEAP>::value>::type* = nullptr>
	constexpr static const size_t NeededHeap(const size_t max_elements)
	{
		return Container<Bucket, ParamsBase::KEY_COUNT>::NeededHeap(max_elements)
			+ Container<KeyValue, ParamsBase::MAX_ELEMENTS>::NeededHeap(max_elements)
			+ Container<std::atomic<KeyValue*>, ParamsBase::MAX_ELEMENTS>::NeededHeap(max_elements);
	}

	inline void Add(const K& k, const V& v);

	inline const V Take(const K& k);

	inline void Take(const K& k, const std::function<bool(const V&)>& receiver);

private:
	inline KeyValue* GetNextFreeKeyValue();
	inline void ReleaseNode(KeyValue* pKeyValue);

private:
	Container<Bucket, ParamsBase::KEY_COUNT> m_hash;
	Container<KeyValue, ParamsBase::MAX_ELEMENTS> m_keyStorage;
	Container<std::atomic<KeyValue*>, ParamsBase::MAX_ELEMENTS> m_recycle;
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

template<typename K, typename V, size_t ... Args>
void Hash<K, V, Args ...>::Add(const K& k, const V& v)
{
	const size_t h = hash(k, seed);
	const size_t index = h % ParamsBase::GetKeyCount();

	KeyValue* pKeyValue = GetNextFreeKeyValue();
	pKeyValue->v = v;
	pKeyValue->k = KeyHashPair{ h, k };
	m_hash;
	m_hash[index].Add(pKeyValue);
}


template<typename K, typename V, size_t ... Args>
const V Hash<K, V, Args ...>::Take(const K& k)
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


template<typename K, typename V, size_t ... Args>
void Hash<K, V, Args ...>::Take(const K& k, const std::function<bool(const V&)>& receiver)
{
	const size_t h = hash(k, seed);
	const size_t index = h % ParamsBase::GetKeyCount();
	std::function<void(KeyValue*)> release = std::bind(&Hash::ReleaseNode, this, std::placeholders::_1);
	m_hash[index].TakeValue(k, h, receiver, release);
}

template<typename K, typename V, size_t ... Args>
KeyValueT<K, V>* Hash<K, V, Args ...>::GetNextFreeKeyValue()
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

template<typename K, typename V, size_t ... Args>
void Hash<K, V, Args ...>::ReleaseNode(KeyValue* pKeyValue)
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

