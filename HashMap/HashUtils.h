#pragma once
#include <atomic>
#include <random> 
#include <assert.h>

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
	//else { static_assert(0, "Unsupported platform"); }
}

constexpr static size_t GetNextPowerOfTwo(const size_t value)
{
	size_t v = value;

	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

constexpr static size_t ComputeHashKeyCount(const size_t count)
{
	return GetNextPowerOfTwo(count * 2);
}

template<typename T, size_t SIZE>
struct Array
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

template<typename K>
struct KeyHashPairT
{
	size_t hash;
	K key;

	__declspec(deprecated("** sizeof(K) is too large for lockless access, "
		"define `SKIP_ATOMIC_LOCKLESS_CHECKS´ to suppress this warning **"))
		static constexpr bool NotLockFree() { return false; }
	static constexpr bool IsLockFree()
	{
#ifndef SKIP_ATOMIC_LOCKLESS_CHECKS
		if constexpr (sizeof(KeyHashPairT) > 8) { return NotLockFree(); }
#else
#pragma message("Warning: Hash-map lockless operations are not quaranteed, "
		"remove define `SKIP_ATOMIC_LOCKLESS_CHECKS` to ensure lockless access")
#endif
		return true;
	}
};

template<typename K, typename V>
struct KeyValueT
{
	KeyValueT() :
		k(),
		v()
	{
	}
	typedef KeyHashPairT<K> KeyHashPair;

	std::atomic<KeyHashPair> k;
	V v; // value

	void Reset()
	{
		k = KeyHashPair();
		v = V();
	}
};

template<typename K, typename V, size_t COLLISION_SIZE>
class BucketT
{
public:
	BucketT() :
		m_bucket{ nullptr },
		m_usageCounter(0)
	{
	}

	typedef KeyValueT<K, V> KeyValue;
	typedef KeyHashPairT<K> KeyHashPair;

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

	void TakeValue(const K& k, const size_t hash, const std::function<bool(const V&)>& f, const std::function<void(KeyValue*)>& release)
	{
		if (m_usageCounter == 0)
			return;

		for (size_t i = 0; i < COLLISION_SIZE; ++i)
		{
			// Check if Bucket was emptied while accessing
			if (m_usageCounter == 0)
			{
				break;
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
				--m_usageCounter;

				if (!f(pCandidate->v))
					break;

				release(pCandidate);
			}
		}
	}

private:
	Array<std::atomic<KeyValue*>, COLLISION_SIZE> m_bucket;
	std::atomic<size_t> m_usageCounter; // Keys in bucket
};