#pragma once
#include <atomic>
#include <type_traits>
#include <intrin.h>

#pragma intrinsic(_BitScanReverse)

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

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

template <typename K>
size_t hash(const K& k);

const size_t MIN_COLLISION_SIZE = 1;

template<typename K,
	typename V,
	size_t MAX_ELEMENTS,
	size_t COLLISION_SIZE_HINT = MIN_COLLISION_SIZE>
	class Hash
{
private:

	constexpr static const size_t KEY_COUNT = ComputeHashKeyCount(MAX_ELEMENTS * 2);
	constexpr static const size_t MASK = KEY_COUNT - 1;
	constexpr static const size_t COLLISION_SIZE = COLLISION_SIZE_HINT > MIN_COLLISION_SIZE ? COLLISION_SIZE_HINT : MIN_COLLISION_SIZE;

	template<typename K, typename V>
	class KeyValueT
	{
	public:
		KeyValueT() :
			k(),
			v(),
			recycled(true),
			access_counter(0)
		{

		}

		union Key
		{
			uint64_t combined;
			struct
			{
				// MSB indicated write-access (i.e. exclusive access)
				// Remaining 15bits are counting concurrent read access
				uint16_t accessMask;

				K k;
				static_assert(sizeof(K) <= sizeof(uint64_t) - sizeof(uint16_t), "Type K is larger than supported 48 bits/6 bytes");
			};
		};

		std::atomic<Key> k;
		V v; // value
		std::atomic_bool recycled;
		std::atomic<int> access_counter;

		void Reset()
		{
			k = Key();
			v = V();
			recycled = false;
			access_counter = 0;
		}

	private:
	};
	typedef KeyValueT<K, V> KeyValue;

	template<typename K, typename V>
	class BucketT
	{
	public:
		BucketT() :
			h(),
			bucket{ nullptr },
			usage_counter(INT_MIN)
		{

		}

		// Hash-value of this bucket
		std::atomic<size_t> h;

		void Add(KeyValue* pKeyValue)
		{
			const int usage_now = ++usage_counter;
			if (usage_now >= COLLISION_SIZE)
			{
				// Bucket is full
				--usage_counter;
				throw std::bad_alloc();
			}

			for (size_t i = 0; i < COLLISION_SIZE; ++i)
			{
				KeyValue* pExpected = nullptr;
				if (bucket[i].compare_exchange_strong(pExpected, pKeyValue))
				{
					break;
				} // else index already in use
			}

		}

		bool TakeValue(const K& k, KeyValue** ppKeyValue)
		{
			bool bucketReleased = false;

			for (size_t i = 0; i < COLLISION_SIZE; ++i)
			{
				KeyValue* pCandidate = bucket[i];
				if (pCandidate == nullptr)
				{
					continue;
				}
				/*else if (pCandidate->access_counter++;
					pCandidate->recycled)
				{
					pCandidate->access_counter--;
					continue;
				}*/
				else
				{
					/*K def = K();
					if (pCandidate->k.compare_exchange_strong(def, )
					{

					}*/
				}
			}
			return bucketReleased;
		}

		void Reset()
		{
			//k = K();
			//v = V();
			h = size_t();
			usage_counter = 0;
		}

		bool ReleaseBucket()
		{
			int usage = 0;
			if (usage_counter.compare_exchange_strong(0, INT_MIN))
			{
				ReleaseNode(this);
				// Bucket is now released
			}
		}

	private:
		std::atomic<KeyValue*> bucket[COLLISION_SIZE];

		std::atomic<int> usage_counter;
	};
	typedef BucketT<K, V> Bucket;

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
		, m_recycleBuckets{ nullptr }
		, m_recycle{ nullptr }
		, m_storage()
	{
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			m_recycleBuckets[i] = &m_storage[i];
		}

		for (size_t i = 0; i < MAX_ELEMENTS; ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	void Add(const K& k, const V& v)
	{
		const size_t h = hash(k);
		const size_t index = h % KEY_COUNT;

		Bucket* pNewBucket = GetNextFreeBucket();
		if (pNewBucket == nullptr)
			return;
		KeyValue* pKeyValue = GetNextFreeKeyValue();
		pNewBucket->h = h;
		//pBucket->k = k;
		//pBucket->v = v;
		pNewBucket->Add(pKeyValue);

		Bucket* pNull = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			if (m_hash[actualIdx].compare_exchange_strong(pNull, pNewBucket))
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
				if (m_hash[actualIdx].compare_exchange_strong(pNull, pNewBucket))
				{
					ReleaseNode(pNull);
					break;
				} // else key was already replaced
			}
#endif // 201703L
		}
	}

	const V Get(const K& k)
	{
		V ret = V();

		const size_t h = hash(k);
		const size_t index = h % KEY_COUNT;

		/*Bucket* pNull = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			Bucket* pHash = m_hash[actualIdx];
			if (pHash != nullptr)
			{
				// increment access counter to notify possible other threads that acces is ongoing
				pHash->access_counter.fetch_add(1);

				if (pHash->h != h)
				{
					pHash->access_counter.fetch_sub(1);
					break;
				}

				// Check that the item hasn't been recycled (i.e. removed from map in the mean time)
				if (pHash->recycled == false)
				{
					if (pHash->k == k)
					{
						ret = pHash->v;
						if (pHash->recycled)
						{
							ret = V();
						}
						break;
					}

				}
				else
				{
					// Item was removed while accessing it, discard value;
					pHash->access_counter.fetch_sub(1);
				}
			}
		}*/
		return ret;
	}

private:
	Bucket* GetNextFreeBucket()
	{
		Bucket* pRet = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			Bucket* pExpected = m_recycleBuckets[i];
			if (m_recycleBuckets[i].compare_exchange_strong(pExpected, nullptr))
			{
				pExpected->Reset();
				pRet = pExpected;
				break;
			}
		}
		return pRet;
	}

	KeyValue* GetNextFreeKeyValue()
	{
		KeyValue* pRet = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			KeyValue* pExpected = m_recycle[i];
			if (m_recycle[i].compare_exchange_strong(pExpected, nullptr))
			{
				pExpected->Reset();
				pRet = pExpected;
				break;
			}
		}
		return pRet;
	}

	void ReleaseNode(Bucket* pBucket)
	{
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			Bucket* pNull = nullptr;
			if (m_recycleBuckets[i].compare_exchange_strong(pNull, pBucket))
			{
				break;
			}
		}
	}

private:
	Bucket m_storage[KEY_COUNT];
	KeyValue m_keyStorage[MAX_ELEMENTS];

	std::atomic<Bucket*> m_hash[KEY_COUNT];
	std::atomic<Bucket*> m_recycleBuckets[KEY_COUNT];
	std::atomic<KeyValue*> m_recycle[MAX_ELEMENTS];

	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _recycle = sizeof(m_recycle);
	constexpr static const size_t _storage = sizeof(m_storage);
	constexpr static const size_t _key = sizeof(KeyValue);
	constexpr static const size_t _bucket = sizeof(Bucket);
};