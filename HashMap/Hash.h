#pragma once
#include <assert.h>
#include <functional>
#include "HashFunctions.h"
#include "HashUtils.h"

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

#define HEAP_ONLY(ALLOC) template<typename AT = ALLOC::ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type* = nullptr>
#define STATIC_ONLY(ALLOC) template<typename AT = ALLOC::ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type* = nullptr>
#define EXT_ONLY(ALLOC) template<typename AT = ALLOC::ALLOCATION_TYPE, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type* = nullptr>

//#define MODE_READ_ONLY(_MODE) template<typename _M = _MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_READ>::value>::type* = nullptr>
#define MODE_READ_ONLY(_MODE) template<typename _M = _MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_READ>::value || std::is_same<_M, MODE_INSERT_READ_HEAP_BUCKET>::value>::type* = nullptr>
#define MODE_TAKE_ONLY(_MODE) template<typename _M = _MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>
#define MODE_READ_HEAP_BUCKET_ONLY(_MODE) template<typename _M = _MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_READ_HEAP_BUCKET>::value>::type* = nullptr>

#define IS_INSERT_TAKE(x) std::is_same<std::integral_constant<MapMode, x>, MODE_INSERT_TAKE>::value
#define IS_INSERT_READ_FROM_HEAP(x) std::is_same<std::integral_constant<MapMode, x>, MODE_INSERT_READ_HEAP_BUCKET>::value


template<typename K, typename V, typename _Alloc, bool MODE_INSERT_TAKE>
struct HashBaseNormal
{
	static_assert(_Alloc::COLLISION_SIZE > 0, "!! LOGIC ERROR !! Collision bucket cannot be zero in this implementation");

	typedef typename _Alloc::ALLOCATION_TYPE ALLOCATION_TYPE;
	typedef KeyHashPairT<K> KeyHashPair;

	// Mode dependent typedefs
	typedef typename std::conditional<MODE_INSERT_TAKE,
		KeyValueInsertTake<K, V>,
		KeyValueInsertRead<K, V>
	>::type
		KeyValue;

	typedef typename std::conditional<MODE_INSERT_TAKE,
		BucketInsertTake<K, V, _Alloc::COLLISION_SIZE>,
		BucketInsertRead<K, V, _Alloc::COLLISION_SIZE>
	>::type
		Bucket;

	STATIC_ONLY(_Alloc) explicit HashBaseNormal() noexcept
		: m_recycle()
	{
		for (size_t i = 0; i < _Alloc::GetMaxElements(); ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	HEAP_ONLY(_Alloc) explicit HashBaseNormal(const size_t max_elements)
		: m_keyStorage(max_elements)
		, m_recycle(max_elements)
	{
		for (size_t i = 0; i < max_elements; ++i)
		{
			m_recycle[i] = &m_keyStorage[i];
		}
	}

	EXT_ONLY(_Alloc) HashBaseNormal() noexcept
	{
	}

	Container<KeyValue, _Alloc::ALLOCATOR, _Alloc::MAX_ELEMENTS> m_keyStorage;
	Container<std::atomic<KeyValue*>, _Alloc::ALLOCATOR, _Alloc::MAX_ELEMENTS> m_recycle;

	constexpr static const size_t _keys = sizeof(m_keyStorage);
	constexpr static const size_t _recycle = sizeof(m_recycle);
};

template<typename K, typename V, typename _Alloc>
struct BaseAllocateItemsFromHeap
{
	typedef typename _Alloc::ALLOCATION_TYPE ALLOCATION_TYPE;
	typedef KeyHashPairT<K> KeyHashPair;
	typedef KeyValueLinkedList<K, V> KeyValue;
	typedef BucketLinkedList<K, V> Bucket;

	STATIC_ONLY(_Alloc) BaseAllocateItemsFromHeap() {}

	HEAP_ONLY(_Alloc) explicit BaseAllocateItemsFromHeap(const size_t) {}

	EXT_ONLY(_Alloc) BaseAllocateItemsFromHeap() noexcept {}
};

// Iterator for specific keys
template<typename _Hash>
class KeyIterator;


template<typename K, typename V, typename _Alloc = HeapAllocator<>, MapMode OP_MODE = DefaultModeSelector<K, _Alloc>::MODE>
//MapMode OP_MODE = MapMode::PARALLEL_INSERT_TAKE>
class Hash :
	public _Alloc,
	public std::conditional<
	IS_INSERT_READ_FROM_HEAP(OP_MODE), // Check the operation mode of the map
	BaseAllocateItemsFromHeap<K, V, _Alloc>, // Inherit if requirements are met
	HashBaseNormal<K, V, _Alloc, IS_INSERT_TAKE(OP_MODE)>  // Inherit if requirements are not met
	>::type
{
	typedef typename std::conditional<
		IS_INSERT_READ_FROM_HEAP(OP_MODE), // Check the operation mode of the map
		BaseAllocateItemsFromHeap<K, V, _Alloc>, // Inherit if requirements are met
		HashBaseNormal<K, V, _Alloc, IS_INSERT_TAKE(OP_MODE)>  // Inherit if requirements are not met
	>::type Base;

	constexpr static const KeyPropertyValidator<K, OP_MODE> VALIDATOR{};

	static_assert(std::is_base_of<Dummy, _Alloc>::value);
private:
	typedef typename _Alloc::ALLOCATION_TYPE ALLOCATION_TYPE;

	typedef typename std::integral_constant<MapMode, OP_MODE> MODE;
	typedef typename K KeyType;
	typedef typename V ValueType;

public:
	typedef typename Base::KeyHashPair KeyHashPair;
	typedef typename Base::KeyValue KeyValue;
	typedef typename Base::Bucket Bucket;

	constexpr static const bool IS_ALWAYS_LOCK_FREE = KeyValue::IsAlwaysLockFree();
	typedef typename std::bool_constant<IS_ALWAYS_LOCK_FREE> ALWAYS_LOCK_FREE;

public: // Construction and initialization
	STATIC_ONLY(_Alloc) inline explicit Hash(const size_t seed = 0) noexcept;

	HEAP_ONLY(_Alloc) inline Hash(const size_t max_elements, const size_t seed = 0);

	EXT_ONLY(_Alloc) inline Hash() noexcept;

	EXT_ONLY(_Alloc) inline bool Init(const size_t max_elements, Bucket* hash, KeyValue* keyStorage, std::atomic<KeyValue*>* keyRecycle) noexcept;

public: // Access functions
	inline bool Add(const K& k, const V& v) noexcept;

	MODE_READ_ONLY(MODE) inline const V Read(const K& k) noexcept;

	MODE_READ_ONLY(MODE) inline const bool Read(const K& k, V& v) noexcept;

	MODE_TAKE_ONLY(MODE) inline const V Take(const K& k) noexcept;

	MODE_TAKE_ONLY(MODE) inline bool Take(const K& k, V& v) noexcept;

	MODE_TAKE_ONLY(MODE) inline void Take(const K& k, const std::function<bool(const V&)>& receiver) noexcept;

public: // Support functions
	constexpr static const bool IsAlwaysLockFree() noexcept;

	template<typename LOCK_FREE = ALWAYS_LOCK_FREE, typename std::enable_if<std::is_same<LOCK_FREE, TRUE_TYPE>::value>::type* = nullptr>
	inline bool IsLockFree() const noexcept;

	template<typename LOCK_FREE = ALWAYS_LOCK_FREE, typename std::enable_if<std::is_same<LOCK_FREE, FALSE_TYPE>::value>::type* = nullptr>
	inline bool IsLockFree() const noexcept;

private: // Internal utility functions
	MODE_TAKE_ONLY(MODE) inline KeyValue* GetNextFreeKeyValue() noexcept
	{
		for (size_t i = m_usedNodes; i < _Alloc::GetMaxElements(); ++i)
		{
			KeyValue* pExpected = Base::m_recycle[i];
			if (pExpected == nullptr)
				continue;
			if (Base::m_recycle[i].compare_exchange_strong(pExpected, nullptr))
			{
				pExpected->Reset();
				m_usedNodes++;
				return pExpected;
			}
		}
		return nullptr;
	}

	MODE_READ_ONLY(MODE) inline KeyValue* GetNextFreeKeyValue() noexcept
	{
		m_usedNodes++;
		return new(std::nothrow) KeyValue();
	}

	MODE_TAKE_ONLY(MODE) inline void ReleaseNode(KeyValue* pKeyValue) noexcept;
	MODE_READ_ONLY(MODE) inline void ReleaseNode(KeyValue* pKeyValue) noexcept
	{
		m_usedNodes--;
		delete pKeyValue;
	}

private:
	Container<Bucket, _Alloc::ALLOCATOR, _Alloc::KEY_COUNT> m_hash;
	std::atomic<size_t> m_usedNodes;

	const size_t seed;

	constexpr static const bool _K = std::is_trivially_copyable<K>::value;
	constexpr static const bool _V = std::is_trivially_copyable<V>::value;

	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _key = sizeof(KeyValue);
	constexpr static const size_t _bucket = sizeof(Bucket);

	friend class KeyIterator<Hash<K, V, _Alloc, OP_MODE>>;
};

template<typename _Hash>
class KeyIterator
{
	typedef typename _Hash::KeyType K;
	typedef typename _Hash::ValueType V;
	typedef typename _Hash::Bucket Bucket;
	typedef typename Bucket::Iterator Iterator;

public:
	explicit KeyIterator(_Hash& hash) noexcept
		: _hash(hash)
		, _h(0)
		, _bucket(nullptr)
		, _keyValue(nullptr) {}

	~KeyIterator()
	{
		if (_keyValue)
			_hash.ReleaseNode(_keyValue);
	}

	inline KeyIterator& SetKey(const K& k) noexcept
	{
		_k = k;
		_h = hash(k, _hash.seed);
		const size_t index = (_h & _hash.GetHashMask());
		_bucket = &_hash.m_hash[index];

		SetIter();
		return *this;
	}

	//! \brief Resets the iterator back to initial position (i.e. Same as calling SetKey again)
	inline KeyIterator& Reset() noexcept
	{
		SetIter();
		return *this;
	}
	inline const bool Next() noexcept
	{
		return _iter.Next();
	}

	inline V& Value() noexcept
	{
		return _iter.Value();
	}

	inline const V& Value() const noexcept
	{
		return _iter.Value();
	}

private:
	MODE_READ_ONLY(_Hash::MODE) inline void SetIter() noexcept
	{
		_iter = Iterator(_bucket, _h, _k);
	}

	MODE_TAKE_ONLY(_Hash::MODE) inline void SetIter() noexcept
	{
		_release = [=](typename _Hash::KeyValue* pKey) {_hash.ReleaseNode(pKey); };
		_iter = Iterator(_bucket, _h, _k, _release);
	}
private:
	_Hash& _hash;
	Iterator _iter;
	K _k;
	size_t _h;
	typename _Hash::Bucket* _bucket;
	typename _Hash::KeyValue* _keyValue;

	std::function<void(typename _Hash::KeyValue*)> _release;
#ifdef _DEBUG
	std::atomic<size_t> _counter;
#endif // !_DEBUG

};

#define HEAP_ONLY_IMPL template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type*>
#define STATIC_ONLY_IMPL template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type*>
#define EXT_ONLY_IMPL template<typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type*>

#define MODE_READ_ONLY_IMPL_ template<typename _M, typename std::enable_if<std::is_same<_M, MODE_INSERT_READ>::value || std::is_same<_M, MODE_INSERT_READ_HEAP_BUCKET>::value>::type*>
#define MODE_TAKE_ONLY_IMPL template<typename _M, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>


template<typename K, typename V, typename _Alloc, MapMode OP_MODE> STATIC_ONLY_IMPL
Hash<K, V, _Alloc, OP_MODE>::Hash(const size_t seed /*= 0*/) noexcept
	: Base()
	, m_hash()
	, m_usedNodes(0)
	, seed(seed == 0 ? GenerateSeed() : seed)
{
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> HEAP_ONLY_IMPL
Hash<K, V, _Alloc, OP_MODE>::Hash(const size_t max_elements, const size_t seed /*= 0*/)
	: Base(max_elements)
	, _Alloc(max_elements)
	, m_hash(ComputeHashKeyCount(max_elements))
	, m_usedNodes(0)
	, seed(seed == 0 ? GenerateSeed() : seed)
{
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> EXT_ONLY_IMPL
Hash<K, V, _Alloc, OP_MODE>::Hash() noexcept
	: _Alloc()
	, m_usedNodes(0)
	, seed(GenerateSeed())
{
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> EXT_ONLY_IMPL
bool Hash<K, V, _Alloc, OP_MODE>::Init(const size_t max_elements, Bucket* hash, KeyValue* keyStorage, std::atomic<KeyValue*>* keyRecycle) noexcept
{
	if (_Alloc::Init(max_elements))
	{
		m_hash.Init(hash, ComputeHashKeyCount(max_elements));
		Base::m_keyStorage.Init(keyStorage, max_elements);
		Base::m_recycle.Init(keyRecycle, max_elements);

		for (size_t i = 0; i < _Alloc::GetMaxElements(); ++i)
		{
			Base::m_recycle[i] = &Base::m_keyStorage[i];
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

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> MODE_READ_ONLY_IMPL_
const V Hash<K, V, _Alloc, OP_MODE>::Read(const K& k) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	KeyValue* keyVal = nullptr;
	if (m_hash[index].ReadValue(h, k, &keyVal))
		return keyVal->v;
	return V();
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> MODE_READ_ONLY_IMPL_
const bool Hash<K, V, _Alloc, OP_MODE>::Read(const K& k, V& v) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	return m_hash[index].ReadValue(h, k, v);
}

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> MODE_TAKE_ONLY_IMPL
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

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> MODE_TAKE_ONLY_IMPL
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

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> MODE_TAKE_ONLY_IMPL
void Hash<K, V, _Alloc, OP_MODE>::Take(const K& k, const std::function<bool(const V&)>& receiver) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	const auto release = [=](KeyValue* pKey) {this->ReleaseNode(pKey); };
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

template<typename K, typename V, typename _Alloc, MapMode OP_MODE> MODE_TAKE_ONLY_IMPL
void Hash<K, V, _Alloc, OP_MODE>::ReleaseNode(KeyValue* pKeyValue) noexcept
{
	for (size_t i = --m_usedNodes;; --i)
	{
		KeyValue* pNull = nullptr;
		if (Base::m_recycle[i].compare_exchange_strong(pNull, pKeyValue))
		{
			break;
		}

		if (i == 0)
			break; // shouldn't get here
	}
}
