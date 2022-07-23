#pragma once
#include <atomic>
#include <type_traits>
#include <random>
#include <mutex>
#include <assert.h>
#include "Container.h"
#include "Debug.h"

template <typename K>
struct KeyHashPairT
{
	uint32_t hash;
	K key;
};

//
// FIXME: Add support for utilizing CHECK_FOR_ATOMIC_ACCESS
//
template <typename K, typename V, bool CHECK_FOR_ATOMIC_ACCESS = true>
struct KeyValueInsertTake
{
	typedef KeyHashPairT<K> KeyHashPair;

	std::atomic<KeyHashPair> k;
	V v; // value

	typedef std::bool_constant<CHECK_FOR_ATOMIC_ACCESS> CHECK_TYPE;

	template <typename TYPE = CHECK_TYPE,
	          typename std::enable_if<std::is_same<TYPE, TRUE_TYPE>::value>::type* = nullptr>
	__declspec(deprecated(
	    "** sizeof(K) is too large for lock-less access, "
	    "define `SKIP_ATOMIC_LOCKLESS_CHECKS´ to suppress this warning **")) constexpr static bool NotLockFree()
	{
		return false;
	}

	template <typename TYPE = CHECK_TYPE,
	          typename std::enable_if<std::is_same<TYPE, FALSE_TYPE>::value>::type* = nullptr>
	constexpr static bool NotLockFree()
	{
		return false;
	}

	constexpr static bool IsAlwaysLockFree() noexcept
	{
#ifndef SKIP_ATOMIC_LOCKLESS_CHECKS
		if constexpr (!std::atomic<KeyHashPair>::is_always_lock_free)
		{
			return NotLockFree();
		}
#else
#pragma message("Warning: Hash-map lockless operations are not guaranteed, "
		"remove define `SKIP_ATOMIC_LO-CKLESS_CHECKS` to ensure lock-less access")
#endif
		return true;
	}
};

template <typename K, typename V>
struct KeyValueInsertRead
{
	typedef KeyHashPairT<K> KeyHashPair;
	KeyHashPair k;
	V v; // value

	constexpr static bool IsAlwaysLockFree() noexcept
	{
		return false;
	}
};

template <typename K, typename V>
struct KeyValueLinkedList : public KeyValueInsertRead<K, V>
{
	std::atomic<KeyValueLinkedList*> pNext;

	constexpr static bool IsAlwaysLockFree() noexcept
	{
		return std::atomic<KeyValueLinkedList*>::is_always_lock_free;
	}
};

template <typename K, typename V>
struct BucketLinkedList
{
	typedef KeyValueLinkedList<K, V> KeyValue;

	inline bool Add(KeyValue* pKeyValue) noexcept
	{
		KeyValue* pNext = nullptr;
		if (m_pFirst.compare_exchange_strong(pNext, pKeyValue))
			return true;

		while (pNext)
		{
			KeyValue* pExpected = nullptr;
			if (pNext->pNext.compare_exchange_strong(pExpected, pKeyValue))
			{
				return true;
			}
			pNext = pExpected;
		}
		return false;
	}

	inline bool Get(const uint32_t h, const K& k, V& v) noexcept
	{
		if (KeyValue* keyValue = GetKeyValue(m_pFirst, h, k))
		{
			v = keyValue->v;
			return true;
		}
		return false;
	}

	inline static KeyValue* GetKeyValue(KeyValue* pNext, const uint32_t h, const K& k) noexcept
	{
		while (pNext)
		{
			if ((pNext->k.hash == h) && (pNext->k.key == k))
			{
				break;
			}
			pNext = pNext->pNext;
		}
		return pNext;
	}

	class Iterator
	{
	public:
		typedef BucketLinkedList<K, V> Bucket;

		inline Iterator() noexcept
		    : _bucket(nullptr)
		    , _current(nullptr)
		    , _h(0)
		{
		}

		inline explicit Iterator(Bucket* bucket, const uint32_t h, const K& k) noexcept
		    : _bucket(bucket)
		    , _current(nullptr)
		    , _h(h)
		    , _k(k)
		{
		}

		inline bool Next() noexcept
		{
			TRACE(typeid(Iterator).name(), " Next()");

			KeyValue* keyValue = (_current == nullptr) ? (_current = _bucket->m_pFirst) : (_current->pNext.load());
			if (KeyValue* next = GetKeyValue(keyValue, _h, _k))
			{
				_current = next;
				return true;
			}

			return false;
		}

		inline V& Value() noexcept
		{
			TRACE(typeid(Iterator).name(), " Value()");
			return _current->v;
		}

		inline const V& Value() const noexcept
		{
			TRACE(typeid(Iterator).name(), " Value() const");
			return _current->v;
		}

	private:
		Bucket* _bucket;
		KeyValue* _current;
		uint32_t _h;
		K _k;
	};

	inline ~BucketLinkedList()
	{
		KeyValue* pDelete = m_pFirst;
		while (pDelete)
		{
			KeyValue* next = pDelete->pNext;
			delete pDelete;
			pDelete = next;
		}
	}

private:
	std::atomic<KeyValue*> m_pFirst;
};

template <typename K, typename V, uint32_t COLLISION_SIZE>
class BucketInsertRead
{
public:
	typedef KeyValueInsertRead<K, V> KeyValue;
	typedef KeyHashPairT<K> KeyHashPair;

	inline bool Add(KeyValue* pKeyValue) noexcept
	{
		// Increment the usage counter atomically -> Guarantees that only one thread gets a certain index
		const auto myIndex = m_usageCounter++;
		if (myIndex >= COLLISION_SIZE)
		{
			//
			// FIXME: Add return value
			//
			// Bucket is full
			--m_usageCounter;
			return false;
		}
		KeyValue* pExpected = nullptr;
		const bool ret = m_bucket[myIndex].compare_exchange_strong(pExpected, pKeyValue);
#ifdef _DEBUG
		if (!ret)
		{
			assert(0);
		}
#endif // _DEBUG
		return ret; // On release build, compiler will optimize "ret" away, and directly returns
	}

	inline bool ReadValue(const uint32_t hash, const K& k, V& v) noexcept
	{
		KeyValue* keyval = nullptr;
		if (ReadValue(k, hash, &keyval))
		{
			v = keyval->v;
			return true;
		}
		return false;
	}

	inline bool ReadValue(const uint32_t hash, const K& k, KeyValue** ppKeyValue) noexcept
	{
		if (m_usageCounter == 0)
			return false;

		for (uint32_t i = 0; i < COLLISION_SIZE; ++i)
		{
			KeyValue* pCandidate = m_bucket[i];
			if (pCandidate == nullptr)
			{
				break; // No more items
			}
			else if (pCandidate->k.hash == hash && pCandidate->k.key == k)
			{
				(*ppKeyValue) = pCandidate;
				return true;
			}
		}
		return false;
	}

	inline void ReadValues(const uint32_t hash, const K& k, const std::function<bool(const V&)>& f) noexcept
	{
		if (m_usageCounter == 0)
			return;

		for (uint32_t i = 0; i < COLLISION_SIZE; ++i)
		{
			KeyValue* pCandidate = m_bucket[i];
			if (pCandidate == nullptr)
			{
				break;
			}
			else if (pCandidate->k.hash == hash && pCandidate->k.key == k)
			{
				if (!f(pCandidate->v))
					break;
			}
		}
	}

	class Iterator
	{
	public:
		typedef BucketInsertRead<K, V, COLLISION_SIZE> Bucket;

		inline Iterator() noexcept
		    : _bucket(nullptr)
		    , _current(nullptr)
		    , _currentIndex(0)
		    , _hash(0)
		{
		}

		inline explicit Iterator(Bucket* bucket, const uint32_t h, const K& k) noexcept
		    : _bucket(bucket)
		    , _current(nullptr)
		    , _currentIndex(0)
		    , _hash(h)
		    , _k(k)
		{
		}

		inline bool Next() noexcept
		{
			TRACE(typeid(Iterator).name(), " Next()");
			_current = nullptr;
			return _bucket->ReadValueFromIndex(_currentIndex, _hash, _k, &_current);
		}

		inline V& Value() noexcept
		{
			TRACE(typeid(Iterator).name(), " Value()");
			return _current->v;
		}

		inline const V& Value() const noexcept
		{
			TRACE(typeid(Iterator).name(), " Value() const");
			return _current->v;
		}

	private:
		Bucket* _bucket;
		KeyValue* _current;
		uint32_t _currentIndex;
		uint32_t _hash;

		K _k;
	};

private:
	// Special implementation for Iterator
	inline bool
	    ReadValueFromIndex(uint32_t& startIndex, const uint32_t hash, const K& k, KeyValue** ppKeyValue) noexcept
	{
		if (m_usageCounter == 0)
		{
			return false;
		}

		for (uint32_t i = 0; i < COLLISION_SIZE; ++i)
		{
			const uint32_t actualIdx = (i + startIndex) % COLLISION_SIZE;

			KeyValue* pCandidate = m_bucket[actualIdx];
			if (pCandidate == nullptr)
			{
				break;
			}
			else if (pCandidate->k.hash == hash && pCandidate->k.key == k)
			{
				(*ppKeyValue) = pCandidate;
				startIndex = ((actualIdx + 1) % COLLISION_SIZE);
				return true;
			}
		}
		return false;
	}

private:
	StaticArray<std::atomic<KeyValue*>, COLLISION_SIZE> m_bucket;
	std::atomic<uint32_t> m_usageCounter; // Keys in bucket
};

template <typename K, typename V, uint32_t COLLISION_SIZE>
class BucketInsertTake
{
public:
	typedef KeyValueInsertTake<K, V> KeyValue;
	typedef KeyHashPairT<K> KeyHashPair;

	inline bool Add(KeyValue* pKeyValue) noexcept
	{
		const auto usage_now = ++m_usageCounter;
		if (usage_now > COLLISION_SIZE)
		{
			//
			// FIXME: Add return value
			//
			// Bucket is full
			--m_usageCounter;
			return false;
		}

		for (uint32_t i = 0; i < COLLISION_SIZE; ++i)
		{
			KeyValue* pExpected = nullptr;
			if (m_bucket[i].compare_exchange_strong(pExpected, pKeyValue))
			{
				return true;
			} // else index already in use
		}

		// Item was not added
		--m_usageCounter;
		return false;
	}

	inline bool TakeValue(const K& k, const uint32_t hash, KeyValue** ppKeyValue) noexcept
	{
		if (m_usageCounter == 0)
			return false;

		for (uint32_t i = 0; i < COLLISION_SIZE; ++i)
		{
			// Check if Bucket was emptied while accessing
			if (m_usageCounter == 0)
			{
				//
				// FIXME: Add return value
				//
				return false;
			}

			KeyValue* pCandidate = m_bucket[i];
			if (pCandidate == nullptr)
			{
				continue;
			}
			else if (KeyHashPair kp{hash, k}; pCandidate->k.compare_exchange_strong(kp, KeyHashPair()))
			{
				if (!m_bucket[i].compare_exchange_strong(pCandidate, nullptr))
				{
					// This shouldn't be possible
					// throw std::logic_error("HashMap went booboo");
					//
					// FIXME: Add return an enumerated return value
					//
					ERROR("LOGIC ERROR!");
					return false;
				}
				*ppKeyValue = pCandidate;
				--m_usageCounter;
				return true;
			}
		}
		return false;
	}

	inline void TakeValue(const K& k,
	                      const uint32_t hash,
	                      const std::function<bool(const V&)>& f,
	                      const std::function<void(KeyValue*)>& release) noexcept
	{
		if (m_usageCounter == 0)
			return;

		for (uint32_t i = 0; i < COLLISION_SIZE; ++i)
		{
			// Check if Bucket was emptied while accessing
			if (m_usageCounter == 0)
			{
				//
				// FIXME: Add return value ?
				//
				break;
			}

			KeyValue* pCandidate = m_bucket[i];
			if (pCandidate == nullptr)
			{
				continue;
			}
			else if (KeyHashPair kp{hash, k}; pCandidate->k.compare_exchange_strong(kp, KeyHashPair()))
			{
				if (!m_bucket[i].compare_exchange_strong(pCandidate, nullptr))
				{
					// This shouldn't be possible
					// throw std::logic_error("HashMap went booboo");
					//
					// FIXME: Add return value
					//
					return;
				}
				--m_usageCounter;

				if (!f(pCandidate->v))
					break;

				release(pCandidate);
			}
		}
	}

	class Iterator
	{
	public:
		typedef BucketInsertTake<K, V, COLLISION_SIZE> Bucket;

		inline Iterator() noexcept
		    : _release(nullptr)
		    , _bucket(nullptr)
		    , _current(nullptr)
		    , _currentIndex(0)
		    , _hash(0)
		{
		}

		inline explicit Iterator(Bucket* bucket,
		                         const uint32_t h,
		                         const K& k,
		                         const std::function<void(KeyValue*)>& release) noexcept
		    : _release(release)
		    , _bucket(bucket)
		    , _current(nullptr)
		    , _currentIndex(0)
		    , _hash(h)
		    , _k(k)
		{
		}

		inline bool Next() noexcept
		{
			TRACE(typeid(Iterator).name(), " Next()");
			if (_current)
				_release(_current);
			_current = nullptr;

			return _bucket->TakeValue(_currentIndex, _k, _hash, &_current);
		}

		inline V& Value() noexcept
		{
			TRACE(typeid(Iterator).name(), " Value()");
			return _current->v;
		}

		inline const V& Value() const noexcept
		{
			TRACE(typeid(Iterator).name(), " Value() const");
			return _current->v;
		}

		inline ~Iterator() noexcept
		{
			if (_current)
				_release(_current);
		}

	private:
		std::function<void(KeyValue*)> _release;
		Bucket* _bucket;
		KeyValue* _current;
		uint32_t _currentIndex;
		uint32_t _hash;

		K _k;
	};

private:
	// Special implementation for Iterator
	inline bool TakeValue(uint32_t& startIndex, const K& k, const uint32_t hash, KeyValue** ppKeyValue) noexcept
	{
		TRACE(typeid(BucketInsertTake<K, V, COLLISION_SIZE>).name(), " TakeValue() from ", startIndex);
		if (m_usageCounter == 0)
		{
			DEBUG(typeid(BucketInsertTake<K, V, COLLISION_SIZE>).name(), " TakeValue() Bucket is empty");
			return false;
		}

		for (uint32_t i = 0; i < COLLISION_SIZE; ++i)
		{
			// Check if Bucket was emptied while accessing
			if (m_usageCounter == 0)
			{
				//
				// FIXME: Add return value
				//
				return false;
			}
			const uint32_t actualIdx = (i + startIndex) % COLLISION_SIZE;

			KeyValue* pCandidate = m_bucket[actualIdx];
			if (pCandidate == nullptr)
			{
				continue;
			}
			else if (KeyHashPair kp{hash, k}; pCandidate->k.compare_exchange_strong(kp, KeyHashPair()))
			{
				TRACE(typeid(BucketInsertTake<K, V, COLLISION_SIZE>).name(),
				      " TakeValue() item found on index ",
				      actualIdx);

				if (!m_bucket[actualIdx].compare_exchange_strong(pCandidate, nullptr))
				{
					// This shouldn't be possible
					// throw std::logic_error("HashMap went booboo");
					//
					// FIXME: Add return an enumerated return value
					//
					ERROR(typeid(BucketInsertTake<K, V, COLLISION_SIZE>).name(),
					      " TakeValue() ERROR: Failed to take ownership of ",
					      actualIdx);
					return false;
				}

				*ppKeyValue = pCandidate;
				--m_usageCounter;

				startIndex = ((actualIdx + 1) % COLLISION_SIZE);
				return true;
			}
		}
		return false;
	}

private:
	StaticArray<std::atomic<KeyValue*>, COLLISION_SIZE> m_bucket;
	std::atomic<uint32_t> m_usageCounter; // Keys in bucket
};
