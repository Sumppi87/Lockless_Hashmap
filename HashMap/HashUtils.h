#pragma once
#include <atomic>
#include <type_traits>
#include <random>
#include <assert.h>

// Number of slots in a single bucket
const size_t MIN_COLLISION_SIZE = 16;

enum class AllocatorType
{
	STATIC,
	HEAP,
	EXTERNAL
};

//! \brief Allocate memory from heap
typedef std::integral_constant<AllocatorType, AllocatorType::HEAP> ALLOCATION_TYPE_HEAP;

//! \brief Allocate memory from statically (i.e. raw arrays)
typedef std::integral_constant<AllocatorType, AllocatorType::STATIC> ALLOCATION_TYPE_STATIC;

//! \brief Allocate memory from external source
typedef std::integral_constant<AllocatorType, AllocatorType::EXTERNAL> ALLOCATION_TYPE_EXTERNAL;

typedef std::bool_constant<true> TRUE_TYPE;
typedef std::bool_constant<false> FALSE_TYPE;


static size_t GenerateSeed() noexcept
{
	constexpr auto sizeof_size_t = sizeof(size_t);
	static_assert(sizeof_size_t == 4 || sizeof_size_t == 8, "Unsupported platform");

	std::random_device rd{};
	// Use Mersenne twister engine to generate pseudo-random numbers.
	if constexpr (sizeof_size_t == 4U)
	{
		std::mt19937 engine{ rd() };
		return engine();
	}
	else if constexpr (sizeof_size_t == 8U)
	{
		std::mt19937_64 engine{ rd() };
		return engine();
	}
}

constexpr static size_t GetNextPowerOfTwo(const size_t value) noexcept
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

constexpr static size_t ComputeHashKeyCount(const size_t count) noexcept
{
	return GetNextPowerOfTwo(count * 2);
}

template<size_t COLLISION_SIZE = MIN_COLLISION_SIZE>
struct BucketSize
{
	constexpr static const size_t COLLISION_SIZE = COLLISION_SIZE;
};

struct Dummy {};
struct Heap {};
struct Static {};
struct External {};

template<AllocatorType TYPE>
struct Allocator : public Dummy
{
	constexpr static const std::integral_constant<AllocatorType, TYPE> ALLOCATOR{};
	typedef std::integral_constant<AllocatorType, TYPE> ALLOCATION_TYPE;
};

template<size_t MAX_ELEMENTS, size_t COLLISION_SIZE = MIN_COLLISION_SIZE>
struct StaticAllocator : public BucketSize <COLLISION_SIZE>, public Allocator<AllocatorType::STATIC>, public Static
{
	constexpr static const size_t MAX_ELEMENTS = MAX_ELEMENTS;
	constexpr static const size_t KEY_COUNT = ComputeHashKeyCount(MAX_ELEMENTS);

	static_assert(MAX_ELEMENTS > 0, "Element count cannot be zero");

	constexpr static size_t GetKeyCount() noexcept { return KEY_COUNT; }
	constexpr static size_t GetHashMask() noexcept { return GetKeyCount() - 1; }
	constexpr static size_t GetMaxElements() noexcept { return MAX_ELEMENTS; }
};

template<size_t COLLISION_SIZE = MIN_COLLISION_SIZE>
struct HeapAllocator : public BucketSize <COLLISION_SIZE>, public Allocator<AllocatorType::HEAP>, public Heap
{
	explicit HeapAllocator(const size_t count) noexcept
		: keyCount(ComputeHashKeyCount(count))
		, maxElements(count) {}

	inline size_t GetKeyCount() const noexcept { return keyCount; }
	inline size_t GetHashMask() const noexcept { return GetKeyCount() - 1; }
	inline size_t GetMaxElements() const noexcept { return maxElements; }

	const size_t keyCount;
	const size_t maxElements;

	// FIXME: Workaround to get compilation working in different allocation types
	constexpr static const size_t MAX_ELEMENTS = 0;
	constexpr static const size_t KEY_COUNT = 0;
};

template<size_t COLLISION_SIZE = MIN_COLLISION_SIZE>
struct ExternalAllocator : public BucketSize <COLLISION_SIZE>, public Allocator<AllocatorType::EXTERNAL>, public External
{
	ExternalAllocator() noexcept
		: keyCount(0)
		, maxElements(0)
		, isInitialized(false) {}

	explicit ExternalAllocator(const size_t max_elements) noexcept
		: keyCount(ComputeHashKeyCount(max_elements))
		, maxElements(max_elements)
		, isInitialized(true) {}

	bool Init(const size_t max_elements) noexcept
	{
		bool initialized = false;
		if (isInitialized.compare_exchange_strong(initialized, true))
		{
			maxElements = max_elements;
			keyCount = ComputeHashKeyCount(GetMaxElements());
			return true;
		}
		return false;
	}

	inline size_t GetKeyCount() const noexcept { return keyCount; }
	inline size_t GetHashMask() const noexcept { return keyCount - 1; }
	inline size_t GetMaxElements() const noexcept { return maxElements; }

	std::atomic<bool> isInitialized;

	// FIXME: Workaround to get compilation working in different allocation types
	//
	constexpr static const size_t MAX_ELEMENTS = 0;
	constexpr static const size_t KEY_COUNT = 0;

private:
	size_t keyCount;
	size_t maxElements;
};

template<typename T>
struct Array
{
	inline T& operator[](const size_t idx) noexcept
	{
#ifdef _DEBUG
		assert(idx < _size);
#endif // DEBUG
		return _array[idx];
	}
	inline const T& operator[](const size_t idx) const noexcept
	{
#ifdef _DEBUG
		assert(idx < _size);
#endif // DEBUG
		return _array[idx];
	}

	T* _array;
	size_t _size;
};

template<typename T, size_t SIZE>
struct StaticArray
{
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
struct PtrArray : public Array<T>
{
	constexpr const static auto _T = sizeof(T);

	explicit PtrArray(const size_t size)
	{
		Array<T>::_array = new T[size]{};
		Array<T>::_size = size;
	}

	~PtrArray() noexcept
	{
		delete[] Array<T>::_array;
		Array<T>::_array = nullptr;
	}
};

template<typename T>
struct ExtArray : public Array<T>
{
	constexpr const static auto _T = sizeof(T);

	ExtArray(T* array, const size_t size) noexcept
	{
		Init(array, size);
	}

	void Init(T* ptr, const size_t size) noexcept
	{
		memset(ptr, 0, size * sizeof(T));
		Array<T>::_array = ptr;
		Array<T>::_size = size;
	}
};

template<typename K>
struct KeyHashPairT
{
	size_t hash;
	K key;
};


//
// FIXME: Add support for utilizing CHECK_FOR_ATOMIC_ACCESS
//
template<typename K, typename V, bool CHECK_FOR_ATOMIC_ACCESS = true>
struct KeyValueT
{
	typedef KeyHashPairT<K> KeyHashPair;

	std::atomic<KeyHashPair> k;
	V v; // value

	inline void Reset() noexcept
	{
		k = KeyHashPair();
		v = V();
	}

	typedef std::bool_constant<CHECK_FOR_ATOMIC_ACCESS> CHECK_TYPE;

	template<typename TYPE = CHECK_TYPE, typename std::enable_if<std::is_same<TYPE, TRUE_TYPE>::value>::type* = nullptr>
	__declspec(deprecated("** sizeof(K) is too large for lock-less access, "
		"define `SKIP_ATOMIC_LOCKLESS_CHECKS´ to suppress this warning **"))
		constexpr static bool NotLockFree() { return false; }

	template<typename TYPE = CHECK_TYPE, typename std::enable_if<std::is_same<TYPE, FALSE_TYPE>::value>::type* = nullptr>
	constexpr static bool NotLockFree() { return false; }

	constexpr static bool IsAlwaysLockFree() noexcept
	{
#ifndef SKIP_ATOMIC_LOCKLESS_CHECKS
		if constexpr (!std::atomic<KeyHashPair>::is_always_lock_free) { return NotLockFree(); }
#else
#pragma message("Warning: Hash-map lockless operations are not guaranteed, "
		"remove define `SKIP_ATOMIC_LO-CKLESS_CHECKS` to ensure lock-less access")
#endif
		return true;
	}
};

template<typename K, typename V, size_t COLLISION_SIZE>
class BucketT
{
public:
	typedef KeyValueT<K, V> KeyValue;
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
		}
		return item_added;
	}

	inline bool TakeValue(const K& k, const size_t hash, KeyValue** ppKeyValue) noexcept
	{
		if (m_usageCounter == 0)
			return false;

		for (size_t i = 0; i < COLLISION_SIZE; ++i)
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
			else if (KeyHashPair kp{ hash, k }; pCandidate->k.compare_exchange_strong(kp, KeyHashPair()))
			{
				if (!m_bucket[i].compare_exchange_strong(pCandidate, nullptr))
				{
					// This shouldn't be possible
					//throw std::logic_error("HashMap went booboo");
					//
					// FIXME: Add return an enumerated return value
					//
					return false;
				}
				*ppKeyValue = pCandidate;
				--m_usageCounter;
				break;
			}
		}
		return true;
	}

	inline void TakeValue(const K& k,
		const size_t hash,
		const std::function<bool(const V&)>& f,
		const std::function<void(KeyValue*)>& release) noexcept
	{
		if (m_usageCounter == 0)
			return;

		for (size_t i = 0; i < COLLISION_SIZE; ++i)
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
			else if (KeyHashPair kp{ hash, k }; pCandidate->k.compare_exchange_strong(kp, KeyHashPair()))
			{
				if (!m_bucket[i].compare_exchange_strong(pCandidate, nullptr))
				{
					// This shouldn't be possible
					//throw std::logic_error("HashMap went booboo");
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

private:
	StaticArray<std::atomic<KeyValue*>, COLLISION_SIZE> m_bucket;
	std::atomic<size_t> m_usageCounter; // Keys in bucket
};

template<typename T, AllocatorType TYPE, size_t SIZE = 0>
struct Container :
	public
	std::conditional<std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_HEAP>::value, // condition of outer
	PtrArray<T>, // If 1st condition is true
	typename std::conditional<std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_STATIC>::value, // Condition of inner (and non-true case of outer)
	StaticArray<T, SIZE>, // If 2nd condition is true
	ExtArray<T>>::type>::type // If 2nd condition is false
{
	typedef typename
	std::conditional<std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_HEAP>::value, // condition of outer
	PtrArray<T>, // If 1st condition is true
	typename std::conditional<std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_STATIC>::value, // Condition of inner (and non-true case of outer)
	StaticArray<T, SIZE>, // If 2nd condition is true
	ExtArray<T>>::type>::type Base;

	typedef std::integral_constant<AllocatorType, TYPE> ALLOCATION_TYPE;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	//Container(void) noexcept {}// : Base() { Base::Init(nullptr, 0); }
	Container(void) noexcept : Base(nullptr, 0) {}// { Base::Init(nullptr, 0); }

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	Container(T* ptr, const size_t size) noexcept : Base(ptr, size) {}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	void Init(T* ptr, const size_t size) noexcept
	{
		Base::Init(ptr, size);
	}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	Container(void) = delete;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	explicit Container(const size_t size)
		: Base(size) {}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	constexpr static const size_t NeededHeap(const size_t size)
	{
		return sizeof(T) * size;
	}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type* = nullptr>
	Container(void) noexcept : Base() {}

	// FIXME:	Check usage of copy/move assignment/constructor
	//			-> Can be enabled in some types?
	//
	/*Container(Container&&) = delete;
	Container(Container&) = delete;
	Container& operator=(Container&) = delete;
	Container& operator=(Container&&) = delete;*/
};
