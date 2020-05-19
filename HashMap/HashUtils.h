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

enum class MapMode
{
	//! \brief
	// Hash supports following lock-free operations in parallel:
	//	* Inserting items
	//	* Reading items with Take functions (i.e. read item is removed from map)
	//! \constrains	Key of type <K> must fulfill requirements in std::atomic<K> https://en.cppreference.com/w/cpp/atomic/atomic
	PARALLEL_INSERT_TAKE = 0b001,

	//! \brief
	// Hash supports following lock-free operations in parallel:
	//	* Inserting items
	//	* Reading items with Value functions (i.e. read item is not removed from map)
	//! \constrains Once an item is inserted into the map, it cannot be removed. Key must fulfill std::is_default_constructible
	PARALLEL_INSERT_READ = 0b010,

	//! \brief	Special case of Hash-map, where number of inserted elements is not limited
	//!			Inserted key-value pairs are stored in heap, which is allocated at insertion time
	//! \Note	This operation mode heap allocates, regardless of what Allocator has been chosen.
	//!			However, hashing map is constant size, so adding a large number of items to small hash map can affect performance
	// \details Hash supports following lock-free operations in parallel:
	//	* Inserting items
	//	* Reading items with Value functions (i.e. read item is not removed from map)
	//! \constrains Once an item is inserted into the map, it cannot be removed. Key must fulfill std::is_default_constructible
	PARALLEL_INSERT_READ_GROW_FROM_HEAP = 0b100
};

//! \brief
typedef std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_TAKE> MODE_INSERT_TAKE;

//! \brief
typedef std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_READ> MODE_INSERT_READ;

//! \brief Special case of PARALLEL_INSERT_READ
typedef std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_READ_GROW_FROM_HEAP> MODE_INSERT_READ_HEAP_BUCKET;

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

	typedef typename std::integral_constant<size_t, COLLISION_SIZE> BUCKET_SIZE;
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
	inline ExternalAllocator() noexcept
		: keyCount(0)
		, maxElements(0)
		, isInitialized(false) {}

	inline explicit ExternalAllocator(const size_t max_elements) noexcept
		: keyCount(ComputeHashKeyCount(max_elements))
		, maxElements(max_elements)
		, isInitialized(true) {}

	inline bool Init(const size_t max_elements) noexcept
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

	inline ~PtrArray() noexcept
	{
		delete[] Array<T>::_array;
		Array<T>::_array = nullptr;
	}
};

template<typename T>
struct ExtArray : public Array<T>
{
	constexpr const static auto _T = sizeof(T);

	inline ExtArray(T* array, const size_t size) noexcept
	{
		Init(array, size);
	}

	inline void Init(T* ptr, const size_t size) noexcept
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
struct KeyValueInsertTake
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
		"define `SKIP_ATOMIC_LOCKLESS_CHECKS� to suppress this warning **"))
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

template<typename K, typename V>
struct KeyValueInsertRead
{
	typedef KeyHashPairT<K> KeyHashPair;
	KeyHashPair k;
	V v; // value

	constexpr static bool IsAlwaysLockFree() noexcept { return false; }
};

template<typename K, typename V>
struct KeyValueLinkedList : public KeyValueInsertRead<K, V>
{
	//typedef KeyHashPairT<K> KeyHashPair;
	//KeyHashPair k;
	//V v; // value

	std::atomic<KeyValueLinkedList*> pNext;

	constexpr static bool IsAlwaysLockFree() noexcept
	{
		return std::atomic<KeyValueLinkedList*>::is_always_lock_free;
	}
};

template<typename K, typename V>
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

	inline bool Get(const size_t h, const K& k, V& v) noexcept
	{
		if (KeyValue* keyValue = GetKeyValue(m_pFirst, h, k))
		{
			v = keyValue->v;
			return true;
		}
		return false;
	}

	inline static KeyValue* GetKeyValue(KeyValue* pNext, const size_t h, const K& k) noexcept
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
			: _bucket(nullptr), _current(nullptr), _h(0) {}

		inline explicit Iterator(Bucket* bucket, const size_t h, const K& k) noexcept
			: _bucket(bucket), _current(nullptr), _h(h), _k(k) {}

		inline bool Next() noexcept
		{
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
			return _current->v;
		}

		inline const V& Value() const noexcept
		{
			return _current->v;
		}

	private:
		Bucket* _bucket;
		KeyValue* _current;
		size_t _h;
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

template<typename K, typename V, size_t COLLISION_SIZE>
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

	inline bool ReadValue(const size_t hash, const K& k, V& v) noexcept
	{
		KeyValue* keyval = nullptr;
		if (ReadValue(k, hash, &keyval))
		{
			v = keyval->v;
			return true;
		}
		return false;
	}

	inline bool ReadValue(const size_t hash, const K& k, KeyValue** ppKeyValue) noexcept
	{
		if (m_usageCounter == 0)
			return false;

		for (size_t i = 0; i < COLLISION_SIZE; ++i)
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

	inline void ReadValues(const size_t hash,
		const K& k,
		const std::function<bool(const V&)>& f) noexcept
	{
		if (m_usageCounter == 0)
			return;

		for (size_t i = 0; i < COLLISION_SIZE; ++i)
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
			, _hash(0) {}

		inline explicit Iterator(Bucket* bucket, const size_t h, const K& k) noexcept
			: _bucket(bucket)
			, _current(nullptr)
			, _currentIndex(0)
			, _hash(h)
			, _k(k) {}

		inline bool Next() noexcept
		{
			_current = nullptr;
			return _bucket->ReadValueFromIndex(_currentIndex, _hash, _k, &_current);
		}

		inline V& Value() noexcept
		{
			return _current->v;
		}

		inline const V& Value() const noexcept
		{
			return _current->v;
		}

	private:
		Bucket* _bucket;
		KeyValue* _current;
		size_t _currentIndex;
		size_t _hash;

		K _k;
	};

private:
	// Special implementation for Iterator
	inline bool ReadValueFromIndex(size_t& startIndex, const size_t hash, const K& k, KeyValue** ppKeyValue) noexcept
	{
		if (m_usageCounter == 0)
		{
			return false;
		}

		for (size_t i = 0; i < COLLISION_SIZE; ++i)
		{
			const size_t actualIdx = (i + startIndex) % COLLISION_SIZE;

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
	std::atomic<size_t> m_usageCounter; // Keys in bucket
};

template<typename K, typename V, size_t COLLISION_SIZE>
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

		for (size_t i = 0; i < COLLISION_SIZE; ++i)
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
				return true;
			}
		}
		return false;
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

	class Iterator
	{
	public:
		typedef BucketInsertTake<K, V, COLLISION_SIZE> Bucket;

		inline Iterator() noexcept
			: _release(nullptr),
			_bucket(nullptr),
			_current(nullptr),
			_currentIndex(0),
			_hash(0) {}

		inline explicit Iterator(Bucket* bucket, const size_t h, const K& k, const std::function<void(KeyValue*)>& release) noexcept
			: _release(release),
			_bucket(bucket),
			_current(nullptr),
			_currentIndex(0),
			_hash(h),
			_k(k) {}

		inline bool Next() noexcept
		{
			if (_current)
				_release(_current);
			_current = nullptr;

			return _bucket->TakeValue(_currentIndex, _k, _hash, &_current);
		}

		inline V& Value() noexcept
		{
			return _current->v;
		}

		inline const V& Value() const noexcept
		{
			return _current->v;
		}

	private:
		std::function<void(KeyValue*)> _release;
		Bucket* _bucket;
		KeyValue* _current;
		size_t _currentIndex;
		size_t _hash;

		K _k;
	};

private:

	// Special implementation for Iterator
	inline bool TakeValue(size_t& startIndex, const K& k, const size_t hash, KeyValue** ppKeyValue) noexcept
	{
		if (m_usageCounter == 0)
		{
			return false;
		}

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
			const size_t actualIdx = (i + startIndex) % COLLISION_SIZE;

			KeyValue* pCandidate = m_bucket[actualIdx];
			if (pCandidate == nullptr)
			{
				continue;
			}
			else if (KeyHashPair kp{ hash, k }; pCandidate->k.compare_exchange_strong(kp, KeyHashPair()))
			{
				if (!m_bucket[actualIdx].compare_exchange_strong(pCandidate, nullptr))
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

				startIndex = ((actualIdx + 1) % COLLISION_SIZE);
				return true;
			}
		}
		return false;
	}

private:
	StaticArray<std::atomic<KeyValue*>, COLLISION_SIZE> m_bucket;
	std::atomic<size_t> m_usageCounter; // Keys in bucket
};

template<typename T, AllocatorType TYPE, size_t SIZE = 0>
struct Container : public
	std::conditional<std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_HEAP>::value, // condition of outer
	PtrArray<T>, // If 1st condition is true
	typename std::conditional<std::is_same<std::integral_constant<AllocatorType, TYPE>, ALLOCATION_TYPE_STATIC>::value, // Condition of inner (and non-true case of outer)
	StaticArray<T, SIZE>, // If 2nd condition is true
	ExtArray<T> // If 2nd condition is false
	>::type // Extract the type from inner std::conditional
	>::type // Extract the type from outer std::conditional
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
	inline Container(void) noexcept : Base(nullptr, 0) {}// { Base::Init(nullptr, 0); }

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	inline Container(T* ptr, const size_t size) noexcept : Base(ptr, size) {}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	inline void Init(T* ptr, const size_t size) noexcept
	{
		Base::Init(ptr, size);
	}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	inline Container(void) = delete;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	inline Container(const size_t size)
		: Base(size) {}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	constexpr static const size_t NeededHeap(const size_t size)
	{
		return sizeof(T) * size;
	}

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type* = nullptr>
	inline Container(void) noexcept : Base() {}

	// FIXME:	Check usage of copy/move assignment/constructor
	//			-> Can be enabled in some types?
	//
	/*Container(Container&&) = delete;
	Container(Container&) = delete;
	Container& operator=(Container&) = delete;
	Container& operator=(Container&&) = delete;*/
};


template<typename K>
struct GeneralKeyReqs
{
	typedef typename std::bool_constant<
		std::is_default_constructible<K>::value
		&& std::is_copy_assignable<K>::value> GENERAL_REQS_MET;

	constexpr static const bool VALID_KEY_TYPE = GENERAL_REQS_MET::value;

	constexpr const static bool AssertAll()
	{
		static_assert(
			VALID_KEY_TYPE,
			"Key type must fulfill std::is_default_constructible<K>::value and std::is_copy_assignable<K>");
		return true;
	}
};

template<typename K, bool CHECK_FOR_ATOMIC_ACCESS>
struct AtomicsRequired
{
	static constexpr const GeneralKeyReqs<K> GENERAL_REQS{};

	typedef typename std::bool_constant<
		std::is_trivially_copyable<K>::value
		&& std::is_copy_constructible<K>::value
		&& std::is_move_constructible<K>::value
		&& std::is_copy_assignable<K>::value
		&& std::is_move_assignable<K>::value> STD_ATOMIC_REQS_MET;

	constexpr static const bool STD_ATOMIC_AVAILABLE = STD_ATOMIC_REQS_MET::value;

	constexpr static const bool STD_ATOMIC_ALWAYS_LOCK_FREE = []()
	{
		if constexpr (STD_ATOMIC_REQS_MET::value)
			return std::atomic<K>::is_always_lock_free && std::atomic<KeyHashPairT<K>>::is_always_lock_free;
		return false;
	}();

	constexpr static const bool VALID_KEY_TYPE =
		GENERAL_REQS.VALID_KEY_TYPE
		&& STD_ATOMIC_AVAILABLE
		&& ((CHECK_FOR_ATOMIC_ACCESS && STD_ATOMIC_ALWAYS_LOCK_FREE)
			|| !CHECK_FOR_ATOMIC_ACCESS);

	typedef std::bool_constant<CHECK_FOR_ATOMIC_ACCESS> CHECK_TYPE;

	template<typename TYPE = CHECK_TYPE, typename std::enable_if<std::is_same<TYPE, TRUE_TYPE>::value>::type* = nullptr>
	__declspec(deprecated("** sizeof(K) is too large for lock-less access, "
		"define `SKIP_ATOMIC_LOCKLESS_CHECKS� to suppress this warning **"))
		constexpr static bool NotLockFree() { return false; }

	template<typename TYPE = CHECK_TYPE, typename std::enable_if<std::is_same<TYPE, FALSE_TYPE>::value>::type* = nullptr>
	constexpr static bool NotLockFree() { return false; }

	constexpr static bool IsAlwaysLockFree() noexcept
	{
#ifndef SKIP_ATOMIC_LOCKLESS_CHECKS
		if constexpr (!STD_ATOMIC_ALWAYS_LOCK_FREE) { return NotLockFree(); }
#else
#pragma message("Warning: Hash-map lockless operations are not guaranteed, "
		"remove define `SKIP_ATOMIC_LO-CKLESS_CHECKS` to ensure lock-less access")
#endif
		return true;
	}


	constexpr const static bool AssertAll()
	{
		GENERAL_REQS.AssertAll();

		static_assert(
			STD_ATOMIC_AVAILABLE,
			"In MapMode::PARALLEL_INSERT_TAKE mode, key-type must fulfill std::atomic requirements.");

		IsAlwaysLockFree();

		return true;
	}
};

template<typename K, MapMode OP_MODE>
struct HashKeyProperties :
	public std::conditional<std::is_same<std::integral_constant<MapMode, OP_MODE>, MODE_INSERT_TAKE>::value,
	AtomicsRequired<K, true>,
	GeneralKeyReqs<K>>::type
{
	typedef typename std::integral_constant<MapMode, OP_MODE> MODE;

	typedef typename
		std::conditional<std::is_same<std::integral_constant<MapMode, OP_MODE>, MODE_INSERT_TAKE>::value,
		AtomicsRequired<K, true>,
		GeneralKeyReqs<K>>::type Base;

	constexpr static const bool VALID_KEY_TYPE = Base::VALID_KEY_TYPE;

	constexpr static bool IsValidKeyForMode()
	{
		return VALID_KEY_TYPE;
	}

	constexpr const static bool AssertAll()
	{
		return Base::AssertAll();
	}
};

template<typename K, MapMode OP_MODE>
struct KeyPropertyValidator : public HashKeyProperties<K, OP_MODE>
{
	typedef typename HashKeyProperties<K, OP_MODE> KeyProps;
	static_assert(KeyProps::AssertAll(), "Hash key failed to meet requirements");
};

template<typename K, typename _Alloc>
struct DefaultModeSelector
{
	typedef typename std::bool_constant<
		std::is_same<
		typename _Alloc::BUCKET_SIZE::type,
		std::integral_constant<size_t, 0>::type
		>::value
	> ZERO_SIZE_BUCKET; // Bucket size is not fixed -> Items are allocated from heap as needed

	constexpr static const MapMode MODE = std::conditional<
		ZERO_SIZE_BUCKET::value,
		MODE_INSERT_READ_HEAP_BUCKET,
		std::conditional<
		AtomicsRequired<K, false>::STD_ATOMIC_AVAILABLE, // Check if requirements for std::atomic is met by K
		MODE_INSERT_TAKE, // If requirements are met
		MODE_INSERT_READ // If requirements are not met
		>::type
	>::type // Extract type selected by std::conditional (i.e. MODE_INSERT_TAKE or MODE_INSERT_TAKE>
		::value; // Extract actual type from selected mode
};
