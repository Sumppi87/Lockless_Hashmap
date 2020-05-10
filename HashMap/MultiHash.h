#pragma once
#include <atomic>
#include <type_traits>
#include <random> 
#include "Hashfunctions.h"

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

template<typename K, typename V>
class BucketItem
{
public:
	BucketItem() :
		h(),
		k(),
		v(),
		in_use(false)
	{
	}

	// Hash-value of this bucket
	size_t h;
	K k;
	V v;

	std::atomic_bool in_use;
};

template<typename K,
	typename V>
	class MultiHash
{
	static size_t GenerateSeed()
	{
		std::random_device rd{};

		// Use Mersenne twister engine to generate pseudo-random numbers.
		if constexpr (sizeof(size_t) == 4)
		{
			std::mt19937 engine{ rd() };
			return engine();
		}
		else if constexpr (sizeof(size_t) == 8)
		{
			std::mt19937_64 engine{ rd() };
			return engine();
		}
		else { static_assert(false, "Unsupported platform"); }
	}
public:
	constexpr static size_t ComputeHashKeyCount(const size_t count)
	{
		size_t v = count * 2;

		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}

	typedef BucketItem<K, V> BucketItem;

	MultiHash(std::atomic<BucketItem*>* pHash, BucketItem* pStorage, const size_t max_elements)
		: m_hash(pHash)
		, m_storage(pStorage)
		, MAX_ELEMENTS(max_elements)
		, KEY_COUNT(ComputeHashKeyCount(max_elements))
		, seed(GenerateSeed())
	{
	}

	virtual ~MultiHash()
	{
	}

	void Add(const K& k, const V& v)
	{
		const size_t h = hash(k, seed);
		const size_t index = h % KEY_COUNT;

		BucketItem* pBucket = GetNextFreeBucket();
		pBucket->h = h;
		pBucket->k = k;
		pBucket->v = v;

		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			BucketItem* pNull = nullptr;
			const size_t actualIdx = (index + i) % KEY_COUNT;
			if (m_hash[actualIdx].compare_exchange_strong(pNull, pBucket))
			{
				// Found an empty slot, and stored the bucket
				break;
			}
		}
	}

	V Get(const K& k) const
	{
		const size_t h = hash(k, seed);
		const size_t index = h % KEY_COUNT;

		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			BucketItem* pHash = m_hash[actualIdx];
			if (pHash == nullptr)
				break;

			if (pHash->k == k)
			{
				return pHash->v;
			}
		}
		return V();
	}

	const size_t Get(const K& k, V* pValues, const size_t max_values) const
	{
		size_t ret = 0;

		const size_t h = hash(k, seed);
		const size_t index = h % KEY_COUNT;

		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			BucketItem* pHash = m_hash[actualIdx];
			if (pHash == nullptr || ret >= max_values)
				break;

			if (pHash->k == k)
			{
				pValues[ret] = pHash->v;
				++ret;
			}
		}
		return ret;
	}

	const size_t Count(const K& k) const
	{
		size_t ret = 0;

		const size_t h = hash(k, seed);
		const size_t index = h % KEY_COUNT;

		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			const BucketItem* pHash = m_hash[actualIdx];
			if (pHash == nullptr)
				break;

			if (pHash->k == k)
				++ret;
		}
		return ret;
	}
private:
	BucketItem* GetNextFreeBucket()
	{
		BucketItem* pRet = nullptr;
		for (size_t i = 0; i < MAX_ELEMENTS; ++i)
		{
			BucketItem* pTemp = &m_storage[i];
			bool in_use = false;
			if (pTemp->in_use.compare_exchange_strong(in_use, true))
			{
				pRet = pTemp;
				break;
			}
		}
		if (pRet == nullptr)
			throw std::bad_alloc();
		return pRet;
	}

private:
	BucketItem* m_storage;
	std::atomic<BucketItem*>* m_hash;
	const size_t KEY_COUNT;
	const size_t MAX_ELEMENTS;

	const size_t seed;
};

template<typename K,
	typename V,
	size_t MAX_ELEMENTS>
	class MultiHash_S : public MultiHash<K, V>
{
private:
	typedef MultiHash<K, V> Base;
	typedef BucketItem<K, V> BucketItem;
	constexpr static const size_t KEY_COUNT = Base::ComputeHashKeyCount(MAX_ELEMENTS);

public:
	MultiHash_S()
		: m_storage()
		, m_hash()
		, Base(&m_hash[0], &m_storage[0], MAX_ELEMENTS)
	{
	}

	~MultiHash_S() override
	{
	}

private:
	constexpr static const auto _bucket = sizeof(BucketItem);
	BucketItem m_storage[MAX_ELEMENTS];
	constexpr static const auto _storage = sizeof(m_storage);
	std::atomic<BucketItem*> m_hash[KEY_COUNT];
};

template<typename K,
	typename V>
	class MultiHash_H : public MultiHash<K, V>
{
	typedef MultiHash<K, V> Base;
	typedef BucketItem<K, V> BucketItem;

public:
	MultiHash_H(const size_t max_elements)
		: Base(
			m_hash = new std::atomic<BucketItem*>[Base::ComputeHashKeyCount(max_elements)](),
			m_storage = new BucketItem[max_elements](),
			max_elements)
	{
	}

	~MultiHash_H() override
	{
		delete[] m_hash;
		delete[] m_storage;
	}

private:
	std::atomic<BucketItem*>* m_hash;
	BucketItem* m_storage;
};
