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

enum class Allocator
{
	STATIC,
	HEAP,
	EXTERNAL
};

typedef std::integral_constant<Allocator, Allocator::HEAP> ALLOC_HEAP;
typedef std::integral_constant<Allocator, Allocator::STATIC> ALLOC_STATIC;
typedef std::integral_constant<Allocator, Allocator::EXTERNAL> ALLOC_EXTERNAL;

template<typename T, Allocator ALLOC_TYPE, size_t ... Args>
struct HashContainer :
	public std::conditional<ALLOC_TYPE == Allocator::STATIC, Array<T, Args ...>,
	typename std::conditional<ALLOC_TYPE == Allocator::HEAP, PtrArray<T, true>, PtrArray<T, false>>::type>::type
{
	typedef typename std::conditional<ALLOC_TYPE == Allocator::STATIC, Array<T, Args ...>,
		typename std::conditional<ALLOC_TYPE == Allocator::HEAP, PtrArray<T, true>, PtrArray<T, false>>::type>::type Base;
	typedef std::integral_constant<Allocator, ALLOC_TYPE> _THIS;

	template<typename TT = _THIS, typename std::enable_if<std::is_same<TT, ALLOC_EXTERNAL>::value>::type* = nullptr>
	HashContainer(const size_t size, T* ptr) 
	{
		Base::_array = ptr;
		Base::SIZE = size;
	}
	template<typename TT = _THIS, typename std::enable_if<std::is_same<TT, ALLOC_EXTERNAL>::value>::type* = nullptr>
	HashContainer() = delete;

	template<typename TT = _THIS, typename std::enable_if<std::is_same<TT, ALLOC_HEAP>::value>::type* = nullptr>
	HashContainer(const size_t size) {
		Base::_array = new T[size]{ T() };
		Base::SIZE = size;
	}

	template<typename TT = _THIS, typename std::enable_if<std::is_same<TT, ALLOC_STATIC>::value>::type* = nullptr>
	HashContainer() {
	}

	HashContainer(HashContainer&&) = delete;
	HashContainer(HashContainer&) = delete;
	HashContainer& operator=(HashContainer&) = delete;
	HashContainer& operator=(HashContainer&&) = delete;
};

template<size_t SIZE = 0>
struct Base1 {
	//constexpr static const size_t _T = T::_SIZE;
	constexpr static const size_t _T = SIZE;
	constexpr const static bool base1 = false;
	inline static const char* base = "Base1";
	static_assert(SIZE > 0, "Size must be greater than zero");
};

struct Base2 {
	constexpr const static bool base2 = false;
};

template<Allocator ALLOC_TYPE, size_t ... Args>
struct Test :
	public std::conditional<ALLOC_TYPE == Allocator::HEAP, Base1<Args ...>, Base2>::type
{
};

template<typename K,
	typename V,
	size_t MAX_ELEMENTS,
	size_t COLLISION_SIZE_HINT = MIN_COLLISION_SIZE,
	Allocator ALLOC_TYPE = Allocator::HEAP>
	class Hash
{
	static_assert(std::is_trivially_copyable<K>::value, "Template type K must be trivially copyable.");
	typedef KeyHashPairT<K> KeyHashPair;
	typedef KeyValueT<K, V> KeyValue;

private:
	constexpr static const size_t KEY_COUNT = ComputeHashKeyCount(MAX_ELEMENTS);
	constexpr static const size_t COLLISION_SIZE = COLLISION_SIZE_HINT > MIN_COLLISION_SIZE ? COLLISION_SIZE_HINT : MIN_COLLISION_SIZE;
	constexpr static const bool IS_LOCK_FREE = KeyHashPair::IsLockFree();
	typedef BucketT<K, V, COLLISION_SIZE> Bucket;

	constexpr static const bool _K = std::is_trivially_copyable<K>::value;
	constexpr static const bool _V = std::is_trivially_copyable<V>::value;

public:
	Hash()
		: m_hash()
		, m_recycle()
		, m_usedNodes(0)
		, seed(GenerateSeed())
	{
		for (size_t i = 0; i < MAX_ELEMENTS; ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	void Add(const K& k, const V& v)
	{
		const size_t h = hash(k, seed);
		const size_t index = h % KEY_COUNT;

		KeyValue* pKeyValue = GetNextFreeKeyValue();
		pKeyValue->v = v;
		pKeyValue->k = KeyHashPair{ h, k };

		m_hash[index].Add(pKeyValue);
	}

	const V Take(const K& k)
	{
		V ret = V();

		const size_t h = hash(k, seed);
		const size_t index = h % KEY_COUNT;
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
		const size_t index = h % KEY_COUNT;
		std::function<void(KeyValue*)> release = std::bind(&Hash::ReleaseNode, this, std::placeholders::_1);
		m_hash[index].TakeValue(k, h, receiver, release);
	}

private:
	KeyValue* GetNextFreeKeyValue()
	{
		KeyValue* pRet = nullptr;
		for (size_t i = m_usedNodes; i < KEY_COUNT; ++i)
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
	Array<Bucket, KEY_COUNT> m_hash;
	Array<KeyValue, MAX_ELEMENTS> m_keyStorage;

	//HashContainer<Bucket, ALLOC_TYPE, KEY_COUNT> m_test;

	Array<std::atomic<KeyValue*>, MAX_ELEMENTS> m_recycle;
	std::atomic<size_t> m_usedNodes;

	const size_t seed;

	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _keys = sizeof(m_keyStorage);
	constexpr static const size_t _recycle = sizeof(m_recycle);
	constexpr static const size_t _key = sizeof(KeyValue);
	constexpr static const size_t _bucket = sizeof(Bucket);
};
