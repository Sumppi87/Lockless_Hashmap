#pragma once
#include <atomic>
#include <intrin.h>

#pragma intrinsic(_BitScanReverse)


template <typename K>
size_t hash(const K& k);

template<typename K, typename V, size_t MAX_ELEMENTS>
class Hash
{
private:
	constexpr static size_t ComputeHashKeyCount()
	{
		size_t v = MAX_ELEMENTS * 2;

		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}
	constexpr static const size_t KEY_COUNT = ComputeHashKeyCount();
	constexpr static const size_t MASK = KEY_COUNT - 1;

	template<typename K, typename V>
	class HashKeyT
	{
	public:
		HashKeyT() :
			k(),
			v(),
			h(),
			index(0),
			recycled(true),
			access_counter(0)
		{

		}
		K k;
		V v;
		size_t h;
		size_t index;
		std::atomic_bool recycled;
		std::atomic<size_t> access_counter;

		void Reset()
		{
			k = K();
			v = V();
			h = size_t();
			recycled = true;
			access_counter = 0;
		}
		size_t GetIndex() const { return index; }

	private:
	};
	typedef HashKeyT<K, V>  HashKey;

public:
	Hash()
		: m_hash{ nullptr }
		, m_recycle{ nullptr }
		, m_storage()
	{
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			m_recycle[i] = &m_storage[i];
			m_storage->index = i;
		}
	}

	void Add(const K& k, const V& v)
	{
		const size_t h = hash(k);
		const size_t index = h % KEY_COUNT;

		HashKey* pBucket = GetFreeNode();
		if (pBucket == nullptr)
			return;

		pBucket->h = h;
		pBucket->k = k;
		pBucket->v = v;

		HashKey* pNull = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			if (m_hash[actualIdx].compare_exchange_strong(pNull, pBucket))
			{
				// Found an empty bucket, and stored the data
				break;
			}
			// Key-collision, or key alreay stored
			else if (pNull->k == k)
			{
				if (m_hash[actualIdx].compare_exchange_strong(pNull, pBucket))
				{
					FreeNode(pNull);
					break;
				} // else key was already replaced
			}
		}
	}

	const V Value(const K& k)
	{
		V ret = V();

		const size_t h = hash(k);
		const size_t index = h % KEY_COUNT;

		HashKey* pNull = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			HashKey* pHash = m_hash[actualIdx];
			if (pHash && pHash->k == k)
			{
				const size_t cur = ++pHash->access_counter;
				ret = pHash->v;
				const size_t cur_ = --pHash->access_counter;
				if (pHash->recycled)
				{
					// Item was removed while accessing it, discard value;
					ret = V();
				}
				break;
			}
		}
		return ret;
	}

private:
	HashKey* GetFreeNode()
	{
		HashKey* pNull = nullptr;
		HashKey* pRet = nullptr;
		for (size_t i = 0; i < MAX_ELEMENTS; ++i)
		{
			HashKey* pTemp = m_recycle[i];
			if (pTemp && pTemp->access_counter == 0 && pTemp->recycled)
			{
				if (m_recycle[i].compare_exchange_strong(pTemp, nullptr))
				{
					pTemp->recycled = false;
					pRet = pTemp;
					break;
				}
			}
		}
		return pRet;
	}

	void FreeNode(HashKey* pNode)
	{
		pNode->Reset();
		HashKey* pNull = nullptr;
		if (m_recycle[pNode->GetIndex()].compare_exchange_strong(pNull, pNode))
		{
		}
	}

private:

	std::atomic<HashKey*> m_hash[KEY_COUNT];
	std::atomic<HashKey*> m_recycle[KEY_COUNT];
	HashKey m_storage[KEY_COUNT];


	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _recycle = sizeof(m_recycle);
	constexpr static const size_t _storage = sizeof(m_storage);
	constexpr static const size_t _key = sizeof(HashKey);

};