#pragma once
#include <atomic>
#include <type_traits>
#include <intrin.h>

#pragma intrinsic(_BitScanReverse)

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus == 201703L
#define C14 ((__cplusplus == 201402L) || C17)

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

	constexpr static const bool _K = std::is_trivially_copyable<K>::value;
	constexpr static const bool _V = std::is_trivially_copyable<V>::value;

public:
	enum class FeatureSet : uint32_t
	{
		CONCURRENT_READ = 1 << 0,
		CONCURRENT_WRITE_MULTIMAP = CONCURRENT_READ << 1, // Duplicate keys are not replaced
		CONCURRENT_WRITE_REPLACE = CONCURRENT_WRITE_MULTIMAP << 1, // Duplicate keys are not replaced
		CONCURRENT_REMOVAL = CONCURRENT_WRITE_REPLACE << 1,
	};

	constexpr static uint32_t GetFeatures()
	{
		uint32_t ret = 0;
		ret |= (uint32_t)FeatureSet::CONCURRENT_READ;
		ret |= (uint32_t)FeatureSet::CONCURRENT_WRITE_MULTIMAP;
#if C17
		if constexpr (_K && _V)
		{
			ret |= (uint32_t)FeatureSet::CONCURRENT_WRITE_REPLACE;
			ret |= (uint32_t)FeatureSet::CONCURRENT_REMOVAL;
		}
#endif
		return ret;
	}
	constexpr static const uint32_t _features = GetFeatures();

	constexpr static bool IsFeatureSupported(const FeatureSet feature)
	{
		return (_features & (uint32_t)feature) == (uint32_t)feature;
	}

	constexpr static const bool _removal = IsFeatureSupported(FeatureSet::CONCURRENT_REMOVAL);

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
#if C17
			// requires C++17

			// Key-collision, or key alreay stored
			else if constexpr (_K)
			{
				if constexpr (_K)
				{

				}
				if (m_hash[actualIdx].compare_exchange_strong(pNull, pBucket))
				{
					FreeNode(pNull);
					break;
				} // else key was already replaced
			}
#endif // 201703L
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