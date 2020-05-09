#pragma once
#include <atomic>
#include <type_traits>
#include <random> 
#include "HashFunctions.h"

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

const size_t MIN_COLLISION_SIZE = 1;

template<typename K,
	typename V,
	size_t MAX_ELEMENTS,
	size_t COLLISION_SIZE_HINT = MIN_COLLISION_SIZE>
	class Hash
{
private:
	constexpr static const int BUCKET_RELEASED = INT_MIN / 2;
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
		else { static_assert(0, "Unsupported platform"); }
	}

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

	constexpr static const size_t KEY_COUNT = ComputeHashKeyCount(MAX_ELEMENTS);
	constexpr static const size_t MASK = KEY_COUNT - 1;
	constexpr static const size_t COLLISION_SIZE = COLLISION_SIZE_HINT > MIN_COLLISION_SIZE ? COLLISION_SIZE_HINT : MIN_COLLISION_SIZE;

	template<typename K, typename V>
	class KeyValueT
	{
	public:
		KeyValueT() :
			k(),
			v(),
			//recycled(true),
			access_counter(0)
		{

		}

		union Key
		{
			const Key& Set(const K& k)
			{
				this->k = k;
				return *this;
			}

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

		std::atomic<K> k;
		V v; // value
		//std::atomic_bool recycled;
		std::atomic<int> access_counter;

		void Reset()
		{
			//k = Key();
			k = K();
			v = V();
			//recycled = false;
			access_counter = 0;
		}

	private:
	};
	typedef KeyValueT<K, V> KeyValue;

	template<typename K, typename V>
	class BucketT
	{
		template <typename T>
		struct RefCounter
		{
			RefCounter(std::atomic<T>& c)
				: counter(c)
				, counterVal(++c)
			{
			}

			~RefCounter()
			{
				--counter;
			}

			std::atomic<T>& counter;
			const T counterVal;
		};
	public:
		BucketT() :
			h(),
			m_bucket{ nullptr },
			m_usageCounter(BUCKET_RELEASED),
			m_accessCounter(0)
		{
		}

		// Hash-value of this bucket
		std::atomic<size_t> h;

		void Add(KeyValue* pKeyValue)
		{
			RefCounter c(m_usageCounter);
			const int usage_now = ++m_usageCounter;
			if (usage_now > COLLISION_SIZE)
			{
				// Bucket is full
				--m_usageCounter;
				throw std::bad_alloc();
			}
			++m_accessCounter;

			bool item_added = false;
			for (size_t i = 0; i < COLLISION_SIZE; ++i)
			{
				KeyValue* pExpected = nullptr;
				if (m_bucket[i].compare_exchange_strong(pExpected, pKeyValue))
				{
					item_added = true;
					break;
				} // else index already in use
			}
			

			--m_accessCounter;
			if (!item_added)
			{
				--m_usageCounter;
				throw std::bad_alloc();
			}
		}

		bool TakeValue(const K& k, KeyValue** ppKeyValue, bool& bucketReleased)
		{
			const int accessCount = ++m_accessCounter;
			if (accessCount < 0)
				return false;

			for (size_t i = 0; i < COLLISION_SIZE; ++i)
			{
				// Check if Bucket was emptied/released while accessing
				if (m_usageCounter < 0)
				{
					break;
				}

				KeyValue* pCandidate = m_bucket[i];
				if (pCandidate == nullptr)
				{
					continue;
				}
				else if (K _k = k; pCandidate->k.compare_exchange_strong(_k, K()))
				{
					if (!m_bucket[i].compare_exchange_strong(pCandidate, nullptr))
					{
						// This shouldn't be possible
						throw std::logic_error("HashMap went booboo");
					}
					*ppKeyValue = pCandidate;
					--m_usageCounter;
					break;
				}
			}

			// Try releasing the bucket
			bucketReleased = ReleaseBucket();
			--m_accessCounter;
			return true;
		}

		void Reset()
		{
			h = size_t();
			m_usageCounter = 0;
		}

		bool ReleaseBucket()
		{
			int usage = 0;
			return m_usageCounter.compare_exchange_strong(usage, BUCKET_RELEASED);
		}

	private:
		std::atomic<KeyValue*> m_bucket[COLLISION_SIZE];
		std::atomic<int> m_usageCounter; // Keys in bucket
		std::atomic<uint16_t> m_accessCounter; // Accessing threads in this bucket
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
		, seed(GenerateSeed())
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
		const size_t h = hash(k, seed);
		const size_t index = h % KEY_COUNT;

		Bucket* pNewBucket = GetNextFreeBucket();
		if (pNewBucket == nullptr)
			return;
		KeyValue* pKeyValue = GetNextFreeKeyValue();
		pNewBucket->h = h;
		pKeyValue->v = v;
		pKeyValue->k = k;
		//pKeyValue->k = KeyValue::Key{}.Set(k);
		//pBucket->k = k;
		//pBucket->v = v;
		pNewBucket->Add(pKeyValue);

		Bucket* pExpected = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			if (m_hash[actualIdx].compare_exchange_strong(pExpected, pNewBucket))
			{
				// Found an empty bucket, and stored the data
				break;
			}
			else // Bucket already exists, store the data there
			{
				// compare_exchange stores the
				if (pExpected == nullptr)
					throw std::logic_error("HashMap went booboo");
				pExpected->Add(pKeyValue);
			}
		}
	}

	const V Take(const K& k)
	{
		V ret = V();

		const size_t h = hash(k, seed);
		const size_t index = h % KEY_COUNT;

		//Bucket* pNull = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			const size_t actualIdx = (index + i) % KEY_COUNT;
			Bucket* pHash = m_hash[actualIdx];
			if (pHash != nullptr)
			{
				// increment access counter to notify possible other threads that acces is ongoing
				KeyValue* pKeyValue = nullptr;
				bool bucketReleased = false;
				if (pHash->TakeValue(k, &pKeyValue, bucketReleased))
				{
					// Value was found
					ret = pKeyValue->v;
				}

				if (bucketReleased)
				{
					// Bucket was released
				}
				break;
			}
		}
		return ret;
	}

private:
	Bucket* GetNextFreeBucket()
	{
		Bucket* pRet = nullptr;
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			Bucket* pExpected = m_recycleBuckets[i];
			if (pExpected == nullptr)
				continue; // already taken
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
			if (pExpected == nullptr)
				continue;
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

	void ReleaseNode(KeyValue* pKeyValue)
	{
		for (size_t i = 0; i < KEY_COUNT; ++i)
		{
			KeyValue* pNull = nullptr;
			if (m_recycle[i].compare_exchange_strong(pNull, pKeyValue))
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

	const size_t seed;

	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _recycle = sizeof(m_recycle);
	constexpr static const size_t _storage = sizeof(m_storage);
	constexpr static const size_t _key = sizeof(KeyValue);
	constexpr static const size_t _bucket = sizeof(Bucket);
};