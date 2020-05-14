#pragma once
#include <assert.h>
#include <functional>
#include "HashFunctions.h"
#include "HashUtils.h"

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

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
	PARALLEL_INSERT_READ = 0b010
};

//! \brief
typedef std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_TAKE> MODE_INSERT_TAKE;

//! \brief
typedef std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_READ> MODE_INSERT_READ;

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
		"define `SKIP_ATOMIC_LOCKLESS_CHECKS´ to suppress this warning **"))
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

template<typename K, typename _Alloc, bool USING_ATOMIC_KEYS>// = KeyProperties<K>::STD_ATOMIC_USABLE::value>
struct HashTraits// : public KeyProperties<K>
{
	constexpr static const bool ATOMICS_IN_USE = USING_ATOMIC_KEYS;
};

template<typename K>
struct DefaultModeSelector
{
	constexpr static const MapMode MODE = std::conditional<
		AtomicsRequired<K, false>::STD_ATOMIC_AVAILABLE, // Check if requirements for std::atomic is met by K
		MODE_INSERT_TAKE, // If requirements are met
		MODE_INSERT_READ // If requirements are not met
	>::type // Extract type selected by std::conditional (i.e. MODE_INSERT_TAKE or MODE_INSERT_TAKE>
		::value; // Extract actual type from selected mode
};

template<typename K,
	typename V,
	typename _Alloc = HeapAllocator<>,
	MapMode OP_MODE = DefaultModeSelector<K>::MODE>
	//MapMode OP_MODE = MapMode::PARALLEL_INSERT_TAKE>
	class Hash : public _Alloc
{
	constexpr static const KeyPropertyValidator<K, OP_MODE> VALIDATOR{};

	static_assert(std::is_base_of<Dummy, _Alloc>::value);
private:
	typedef KeyHashPairT<K> KeyHashPair;
	typedef typename _Alloc::ALLOCATION_TYPE ALLOCATION_TYPE;

	typedef typename std::integral_constant<MapMode, OP_MODE> MODE;

public:
	typedef KeyValueT<K, V> KeyValue;
	typedef BucketT<K, V, _Alloc::COLLISION_SIZE> Bucket;

	constexpr static const bool IS_ALWAYS_LOCK_FREE = KeyValue::IsAlwaysLockFree();
	typedef typename std::bool_constant<IS_ALWAYS_LOCK_FREE> ALWAYS_LOCK_FREE;

public: // Construction and initialization
	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type* = nullptr>
	inline Hash(const size_t seed = 0) noexcept;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	inline Hash(const size_t max_elements, const size_t seed = 0);

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	inline Hash(const size_t max_elements, Bucket* hash, KeyValue* keyStorage, std::atomic<KeyValue*>* keyRecycle) noexcept;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	inline Hash() noexcept;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>
	inline bool Init(const size_t max_elements, Bucket* hash, KeyValue* keyStorage, std::atomic<KeyValue*>* keyRecycle) noexcept;

public: // Access functions
	inline bool Add(const K& k, const V& v) noexcept;

	template<typename _M = MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>
	inline const V Take(const K& k) noexcept;

	template<typename _M = MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>
	inline bool Take(const K& k, V& v) noexcept;

	template<typename _M = MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>
	inline void Take(const K& k, const std::function<bool(const V&)>& receiver) noexcept;

public: // Support functions
	constexpr static const bool IsAlwaysLockFree() noexcept;

	template<typename LOCK_FREE = ALWAYS_LOCK_FREE, typename std::enable_if<std::is_same<LOCK_FREE, TRUE_TYPE>::value>::type* = nullptr>
	inline bool IsLockFree() const noexcept;

	template<typename LOCK_FREE = ALWAYS_LOCK_FREE, typename std::enable_if<std::is_same<LOCK_FREE, FALSE_TYPE>::value>::type* = nullptr>
	inline bool IsLockFree() const noexcept;

	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
	inline constexpr static const size_t NeededHeap(const size_t max_elements) noexcept;

	typedef typename std::bool_constant<false> _NOT_SAME_;
	template<typename AT = ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value == _NOT_SAME_::value>::type* = nullptr>
	void Test() const noexcept
	{
		std::cout << "Test!";
	}

private: // Internal utility functions
	inline KeyValue* GetNextFreeKeyValue() noexcept;

	template<typename _M = MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>
	inline void ReleaseNode(KeyValue* pKeyValue) noexcept;

private:
	Container<Bucket, _Alloc::ALLOCATOR, _Alloc::KEY_COUNT> m_hash;
	Container<KeyValue, _Alloc::ALLOCATOR, _Alloc::MAX_ELEMENTS> m_keyStorage;
	Container<std::atomic<KeyValue*>, _Alloc::ALLOCATOR, _Alloc::MAX_ELEMENTS> m_recycle;
	std::atomic<size_t> m_usedNodes;

	const size_t seed;

	constexpr static const bool _K = std::is_trivially_copyable<K>::value;
	constexpr static const bool _V = std::is_trivially_copyable<V>::value;

	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _keys = sizeof(m_keyStorage);
	constexpr static const size_t _recycle = sizeof(m_recycle);
	constexpr static const size_t _key = sizeof(KeyValue);
	constexpr static const size_t _bucket = sizeof(Bucket);
};

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type*>
Hash<K, V, _Alloc, OP_MODE>::Hash(const size_t seed /*= 0*/) noexcept
	: m_hash()
	, m_recycle()
	, m_usedNodes(0)
	, seed(seed == 0 ? GenerateSeed() : seed)
{
	for (size_t i = 0; i < _Alloc::GetMaxElements(); ++i)
	{
		m_recycle[i] = &m_keyStorage[i];
	}
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type*>
Hash<K, V, _Alloc, OP_MODE>::Hash(const size_t max_elements, const size_t seed /*= 0*/)
	: _Alloc(max_elements)
	, m_hash(ComputeHashKeyCount(max_elements))
	, m_keyStorage(max_elements)
	, m_recycle(max_elements)
	, m_usedNodes(0)
	, seed(seed == 0 ? GenerateSeed() : seed)
{
	for (size_t i = 0; i < _Alloc::GetMaxElements(); ++i)
	{
		m_recycle[i] = &m_keyStorage[i];
	}
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type*>
Hash<K, V, _Alloc, OP_MODE>::Hash(const size_t max_elements, Bucket* hash, KeyValue* keyStorage, std::atomic<KeyValue*>* keyRecycle) noexcept
	: _Alloc(max_elements)
	, m_hash(hash, ComputeHashKeyCount(max_elements))
	, m_keyStorage(keyStorage, max_elements)
	, m_recycle(keyRecycle, max_elements)
	, m_usedNodes(0)
	, seed(GenerateSeed())
{
	for (size_t i = 0; i < _Alloc::GetMaxElements(); ++i)
	{
		m_recycle[i] = &m_keyStorage[i];
	}
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type*>
Hash<K, V, _Alloc, OP_MODE>::Hash() noexcept
	: _Alloc()
	, m_usedNodes(0)
	, seed(GenerateSeed())
{
}


template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type*>
bool Hash<K, V, _Alloc, OP_MODE>::Init(const size_t max_elements, Bucket* hash, KeyValue* keyStorage, std::atomic<KeyValue*>* keyRecycle) noexcept
{
	if (_Alloc::Init(max_elements))
	{
		m_hash.Init(hash, ComputeHashKeyCount(max_elements));
		m_keyStorage.Init(keyStorage, max_elements);
		m_recycle.Init(keyRecycle, max_elements);

		for (size_t i = 0; i < _Alloc::GetMaxElements(); ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
		return true;
	}
	return false;
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
bool Hash<K, V, _Alloc, OP_MODE>::Add(const K& k, const V& v) noexcept
{
	KeyValue* pKeyValue = GetNextFreeKeyValue();
	if (pKeyValue == nullptr)
		return false;

	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());


	pKeyValue->v = v;
	pKeyValue->k = KeyHashPair{ h, k };
	if (!m_hash[index].Add(pKeyValue))
	{
		ReleaseNode(pKeyValue);
		return false;
		//throw std::bad_alloc();
	}
	return true;
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename _M, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>
const V Hash<K, V, _Alloc, OP_MODE>::Take(const K& k) noexcept
{
	V ret = V();

	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	KeyValue* pKeyValue = nullptr;
	if (m_hash[index].TakeValue(k, h, &pKeyValue))
	{
		// Value was found
		ret = pKeyValue->v;

		ReleaseNode(pKeyValue);
	}
	return ret;
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename _M, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>
bool Hash<K, V, _Alloc, OP_MODE>::Take(const K& k, V& v) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	KeyValue* pKeyValue = nullptr;
	if (m_hash[index].TakeValue(k, h, &pKeyValue))
	{
		// Value was found
		v = pKeyValue->v;
		ReleaseNode(pKeyValue);
		return true;
	}
	return false;
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename _M, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>
void Hash<K, V, _Alloc, OP_MODE>::Take(const K& k, const std::function<bool(const V&)>& receiver) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	static std::function<void(KeyValue*)> release = std::bind(&Hash::ReleaseNode<>, this, std::placeholders::_1);
	m_hash[index].TakeValue(k, h, receiver, release);
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
constexpr const bool Hash<K, V, _Alloc, OP_MODE>::IsAlwaysLockFree() noexcept
{
	return IS_ALWAYS_LOCK_FREE;
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename LOCK_FREE, typename std::enable_if<std::is_same<LOCK_FREE, TRUE_TYPE>::value>::type*>
bool Hash<K, V, _Alloc, OP_MODE>::IsLockFree() const noexcept
{
	return IS_ALWAYS_LOCK_FREE;
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename LOCK_FREE, typename std::enable_if<std::is_same<LOCK_FREE, FALSE_TYPE>::value>::type*>
bool Hash<K, V, _Alloc, OP_MODE>::IsLockFree() const noexcept
{
	static KeyValue k;
	return k.k.is_lock_free();
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type*>
constexpr static const size_t Hash<K, V, _Alloc, OP_MODE>::NeededHeap(const size_t max_elements) noexcept
{
	return Container<Bucket, _Alloc::KEY_COUNT>::NeededHeap(max_elements)
		+ Container<KeyValue, _Alloc::MAX_ELEMENTS>::NeededHeap(max_elements)
		+ Container<std::atomic<KeyValue*>, _Alloc::MAX_ELEMENTS>::NeededHeap(max_elements);
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
KeyValueT<K, V>* Hash<K, V, _Alloc, OP_MODE>::GetNextFreeKeyValue() noexcept
{
	for (size_t i = m_usedNodes; i < _Alloc::GetMaxElements(); ++i)
	{
		KeyValue* pExpected = m_recycle[i];
		if (pExpected == nullptr)
			continue;
		if (m_recycle[i].compare_exchange_strong(pExpected, nullptr))
		{
			pExpected->Reset();
			m_usedNodes++;
			return pExpected;
		}
	}
	return nullptr;
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE>
template<typename _M, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>
void Hash<K, V, _Alloc, OP_MODE>::ReleaseNode(KeyValue* pKeyValue) noexcept
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
