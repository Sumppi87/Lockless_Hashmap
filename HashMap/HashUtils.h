#pragma once
#include <atomic>
#include <random>
#include <assert.h>

static size_t GenerateSeed()
{
	constexpr auto sizeof_size_t = sizeof(size_t);
	static_assert(sizeof_size_t == 4 || sizeof_size_t == 8, "Unsupported platform");

	std::random_device rd{};
	// Use Mersenne twister engine to generate pseudo-random numbers.
	if constexpr (sizeof_size_t == 4)
	{
		std::mt19937 engine{ rd() };
		return engine();
	}
	else if constexpr (sizeof_size_t == 8)
	{
		std::mt19937_64 engine{ rd() };
		return engine();
	}
}

constexpr static size_t GetNextPowerOfTwo(const size_t value)
{
	// Algorithm from https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2

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
	Array() : _array{} {}
	static_assert(SIZE > 0, "Size cannot be zero");
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

template<typename T>
struct PtrArray
{
	PtrArray(const size_t size)
		: _array(new T[size])
		, SIZE(size)
		, _deleteArray(true)
	{
	}

	PtrArray(const size_t size, T* ptr)
		: _array(ptr)
		, SIZE(size)
		, _deleteArray(false)
	{
	}


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

	T* _array;
	size_t SIZE;
	const bool _deleteArray;

	~PtrArray()
	{
		if (_deleteArray)
		{
			delete[] _array;
			_array = nullptr;
		}
	}
};

template<typename K>
struct KeyHashPairT
{
	size_t hash;
	K key;

	__declspec(deprecated("** sizeof(K) is too large for lock-less access, "
		"define `SKIP_ATOMIC_LOCKLESS_CHECKS´ to suppress this warning **"))
		static constexpr bool NotLockFree() { return false; }
	static constexpr bool IsLockFree()
	{
#ifndef SKIP_ATOMIC_LOCKLESS_CHECKS
		if constexpr (sizeof(KeyHashPairT) > 8) { return NotLockFree(); }
#else
#pragma message("Warning: Hash-map lockless operations are not guaranteed, "
		"remove define `SKIP_ATOMIC_LO-CKLESS_CHECKS` to ensure lock-less access")
#endif
		return true;
	}
};

template<typename K, typename V>
struct KeyValueT
{
	KeyValueT() :
		k(),
		v() {}

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
		m_bucket{ },
		m_usageCounter(0) {}

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

const size_t MIN_COLLISION_SIZE = 16;

//! \brief Allocate memory from heap
typedef std::bool_constant<true> ALLOCATE_FROM_HEAP;

//! \brief Allocate memory from statically (i.e. raw arrays)
typedef std::bool_constant<false> ALLOCATE_STATICALLY;

template<typename T, size_t SIZE = 0>
struct Container :
	public std::conditional<SIZE == 0, PtrArray<T>, Array<T, SIZE>>::type
{
	typedef typename std::conditional<SIZE == 0, PtrArray<T>, Array<T, SIZE>>::type Base;
	typedef std::bool_constant<SIZE == 0> ALLOCATION_TYPE;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_FROM_HEAP>::value>::type* = nullptr>
	Container(const size_t size, T* ptr)
		: Base(size, ptr) {}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_FROM_HEAP>::value>::type* = nullptr>
	Container(void) = delete;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_FROM_HEAP>::value>::type* = nullptr>
	Container(const size_t size)
		: Base(size) {}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_FROM_HEAP>::value>::type* = nullptr>
	constexpr static const size_t NeededHeap(const size_t size)
	{
		return sizeof(T) * size;
	}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATE_STATICALLY>::value>::type* = nullptr>
	Container(void) {}

	Container(Container&&) = delete;
	Container(Container&) = delete;
	Container& operator=(Container&) = delete;
	Container& operator=(Container&&) = delete;
};

template <size_t COLLISION_SIZE_HINT>
struct CollisionCalc
{
	constexpr static const size_t COLLISION_SIZE =
		COLLISION_SIZE_HINT > MIN_COLLISION_SIZE
		? COLLISION_SIZE_HINT
		: MIN_COLLISION_SIZE;
};

template <size_t _MAX_ELEMENTS = 0, size_t ... Args>
struct SizeCalculator
{
	constexpr static const size_t MAX_ELEMENTS = _MAX_ELEMENTS;
	constexpr static const size_t KEY_COUNT = _MAX_ELEMENTS > 0 ? ComputeHashKeyCount(_MAX_ELEMENTS) : 0;
};

template <size_t COLLISION_SIZE_HINT = 0, size_t ... Args>
struct StaticParams : public CollisionCalc<COLLISION_SIZE_HINT>, public SizeCalculator<Args ...>
{
	typedef typename SizeCalculator<Args ...> Base;

	static_assert(Base::MAX_ELEMENTS > 0, "Element count cannot be zero");

	constexpr size_t GetKeyCount() const { return Base::KEY_COUNT; }
	constexpr size_t GetMaxElements() const { return Base::MAX_ELEMENTS; }
};

template <size_t COLLISION_SIZE_HINT = 0, size_t ... Args>
struct PtrParams : public CollisionCalc<COLLISION_SIZE_HINT>, public SizeCalculator<Args ...>
{
	PtrParams(const size_t count)
		: keyCount(ComputeHashKeyCount(count))
		, maxElements(count) {}

	inline size_t GetKeyCount() const { return keyCount; }
	inline size_t GetMaxElements() const { return maxElements; }

	const size_t keyCount;
	const size_t maxElements;
};
