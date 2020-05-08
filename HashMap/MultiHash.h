#pragma once
#include <atomic>
#include <type_traits>

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

template <typename K>
size_t hash(const K& k);

template<typename K, typename V>
class BucketT
{
public:
	BucketT() :
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
public:
	constexpr static size_t ComputeHashKeyCount(const size_t count)
	{
		size_t v = count;

		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}

	typedef BucketT<K, V> Bucket;

	MultiHash(std::atomic<Bucket*>* pHash, Bucket* pStorage, const size_t max_elements)
		: m_hash(pHash)
		, m_storage(pStorage)
		, MAX_ELEMENTS(max_elements)
		, KEY_COUNT(ComputeHashKeyCount(max_elements * 2))
	{
	}

	virtual ~MultiHash()
	{
	}

	void Add(const K& k, const V& v)
	{
		const size_t h = hash(k);
		const size_t index = h % KEY_COUNT;

		Bucket* pBucket = GetNextFreeBucket();
		pBucket->h = h;
		pBucket->k = k;
		pBucket->v = v;

		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			Bucket* pNull = nullptr;
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
		const size_t h = hash(k);
		const size_t index = h % KEY_COUNT;

		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			Bucket* pHash = m_hash[actualIdx];
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

		const size_t h = hash(k);
		const size_t index = h % KEY_COUNT;

		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			Bucket* pHash = m_hash[actualIdx];
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

		const size_t h = hash(k);
		const size_t index = h % KEY_COUNT;

		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			const Bucket* pHash = m_hash[actualIdx];
			if (pHash == nullptr)
				break;

			if (pHash->k == k)
				++ret;
		}
		return ret;
	}
private:
	Bucket* GetNextFreeBucket()
	{
		Bucket* pRet = nullptr;
		for (size_t i = 0; i < MAX_ELEMENTS; ++i)
		{
			Bucket* pTemp = &m_storage[i];
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
	Bucket* m_storage;
	std::atomic<Bucket*>* m_hash;
	const size_t KEY_COUNT;
	const size_t MAX_ELEMENTS;
};

template<typename K,
	typename V,
	size_t MAX_ELEMENTS>
	class MultiHash_S : public MultiHash<K, V>
{
private:
	typedef MultiHash<K, V> Base;
	typedef BucketT<K, V> Bucket;
	constexpr static const size_t KEY_COUNT = Base::ComputeHashKeyCount(MAX_ELEMENTS * 2);

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
	Bucket m_storage[MAX_ELEMENTS];
	std::atomic<Bucket*> m_hash[KEY_COUNT];
};

template<typename K,
	typename V>
	class MultiHash_H : public MultiHash<K, V>
{
	typedef MultiHash<K, V> Base;
	typedef BucketT<K, V> Bucket;

public:
	MultiHash_H(const size_t max_elements)
		: Base(
			m_hash = new std::atomic<Bucket*>[Base::ComputeHashKeyCount(max_elements * 2)](),
			m_storage = new Bucket[max_elements](),
			max_elements)
	{
	}

	~MultiHash_H() override
	{
		delete[] m_hash;
		delete[] m_storage;
	}

private:
	std::atomic<Bucket*>* m_hash;
	Bucket* m_storage;
};
