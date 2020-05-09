#pragma once
#include <atomic>
#include <type_traits>
#include <random> 
#include "HashFunctions.h"
#include <assert.h>

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

const size_t MIN_COLLISION_SIZE = 8;

template<typename T, size_t SIZE>
struct Table
{
	inline T& operator[](const size_t idx) noexcept
	{
#ifdef _DEBUG
		assert(idx < SIZE);
#endif // DEBUG
		return _array[idx];
	}
	inline const T& operator[](const size_t idx) const noexcept
	{
#ifdef _DEBUG
		assert(idx < SIZE);
#endif // DEBUG
		return _array[idx];
	}

	T _array[SIZE];
};

template<typename K,
	typename V,
	size_t MAX_ELEMENTS,
	size_t COLLISION_SIZE_HINT = MIN_COLLISION_SIZE>
	class Hash
{
private:
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

	struct KeyHashPair
	{
		size_t hash;
		K key;

		__declspec(deprecated("** sizeof(K) is too large for lockless access"
			", suppress this warning to continue **"))
			static constexpr void NotLockFree() {}
		static constexpr bool IsLockFree()
		{
			if constexpr (sizeof(KeyHashPair) > 8)
			{
				NotLockFree();
				return false;
			}
			return true;
		}
	};

	constexpr static const bool IS_LOCK_FREE = KeyHashPair::IsLockFree();

	template<typename K, typename V>
	struct KeyValueT
	{
		KeyValueT() :
			k(),
			v()
		{
		}

		std::atomic<KeyHashPair> k;
		V v; // value

		void Reset()
		{
			k = KeyHashPair();
			v = V();
		}
	};

	typedef KeyValueT<K, V> KeyValue;

	template<typename K, typename V>
	class BucketT
	{
	public:
		BucketT() :
			m_bucket{ nullptr },
			m_usageCounter(0)
		{
		}

		void Add(KeyValue* pKeyValue)
		{
			const int usage_now = ++m_usageCounter;
			if (usage_now > COLLISION_SIZE)
			{
				// Bucket is full
				--m_usageCounter;
				throw std::bad_alloc();
			}

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

			if (!item_added)
			{
				--m_usageCounter;
				throw std::bad_alloc();
			}
		}

		bool TakeValue(const K& k, const size_t hash, KeyValue** ppKeyValue)
		{
			if (m_usageCounter == 0)
				return false;

			for (size_t i = 0; i < COLLISION_SIZE; ++i)
			{
				// Check if Bucket was emptied while accessing
				if (m_usageCounter == 0)
				{
					return false;
				}

				KeyValue* pCandidate = m_bucket[i];
				if (pCandidate == nullptr)
				{
					continue;
				}
				else if (KeyHashPair kp{ hash, k }; pCandidate->k.compare_exchange_strong(kp, KeyHashPair()))
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
			return true;
		}

	private:
		Table<std::atomic<KeyValue*>, COLLISION_SIZE> m_bucket;
		std::atomic<size_t> m_usageCounter; // Keys in bucket
	};
	typedef BucketT<K, V> Bucket;

	constexpr static const bool _K = std::is_trivially_copyable<K>::value;
	constexpr static const bool _V = std::is_trivially_copyable<V>::value;

public:
	Hash()
		: m_hash()
		, m_recycle{ nullptr }
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
	Table<Bucket, KEY_COUNT> m_hash;
	Table<KeyValue, MAX_ELEMENTS> m_keyStorage;

	Table<std::atomic<KeyValue*>, MAX_ELEMENTS> m_recycle;
	std::atomic<size_t> m_usedNodes;

	const size_t seed;

	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _recycle = sizeof(m_recycle);
	constexpr static const size_t _key = sizeof(KeyValue);
	constexpr static const size_t _bucket = sizeof(Bucket);
};