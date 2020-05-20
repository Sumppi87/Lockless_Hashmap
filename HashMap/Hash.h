#pragma once
#include <assert.h>
#include <functional>
#include "HashFunctions.h"
#include "HashUtils.h"
#include "UtilityFunctions.h"
#include "HashBase.h"

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

#define C17 __cplusplus >= 201703L
#define C14 __cplusplus >= 201402L

//#define MODE_READ_ONLY(_MODE) template<typename _M = _MODE, typename std::enable_if<std::is_same<_M,
// MODE_INSERT_READ>::value>::type* = nullptr>
#define MODE_READ_ONLY(_MODE) \
	template <typename _M = _MODE, \
	          typename std::enable_if<std::is_same<_M, MODE_INSERT_READ>::value \
	                                  || std::is_same<_M, MODE_INSERT_READ_HEAP_BUCKET>::value>::type* = nullptr>
#define MODE_TAKE_ONLY(_MODE) \
	template <typename _M = _MODE, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>

#define MODE_NOT_TAKE(_MODE) \
	template <typename _M = _MODE, typename std::enable_if<!std::is_same<_M, MODE_INSERT_TAKE>::value>::type* = nullptr>

#define MODE_READ_HEAP_BUCKET_ONLY(_MODE) \
	template <typename _M = _MODE, \
	          typename std::enable_if<std::is_same<_M, MODE_INSERT_READ_HEAP_BUCKET>::value>::type* = nullptr>

#define IS_INSERT_TAKE(x) std::is_same<std::integral_constant<MapMode, x>, MODE_INSERT_TAKE>::value
#define IS_INSERT_READ_FROM_HEAP(x) \
	std::is_same<std::integral_constant<MapMode, x>, MODE_INSERT_READ_HEAP_BUCKET>::value

// Iterator for specific keys
template <typename _Hash>
class KeyIterator;

template <typename K,
          typename V,
          typename _Alloc = HeapAllocator<>,
          MapMode OP_MODE = DefaultModeSelector<K, _Alloc>::MODE>
class Hash : public std::conditional<IS_INSERT_READ_FROM_HEAP(OP_MODE), // Check the operation mode of the map
                                     BaseAllocateItemsFromHeap<K, V, _Alloc>, // Inherit if requirements are met
                                     HashBaseNormal<K, V, _Alloc, IS_INSERT_TAKE(OP_MODE)> // Inherit if requirements
                                                                                           // are not met
                                     >::type
{
	typedef typename std::conditional<IS_INSERT_READ_FROM_HEAP(OP_MODE), // Check the operation mode of the map
	                                  BaseAllocateItemsFromHeap<K, V, _Alloc>, // Inherit if requirements are met
	                                  HashBaseNormal<K, V, _Alloc, IS_INSERT_TAKE(OP_MODE)> // Inherit if requirements
	                                                                                        // are not met
	                                  >::type Base;

private:
	typedef typename Base::ALLOCATION_TYPE AT;

	typedef typename std::integral_constant<MapMode, OP_MODE> MODE;
	typedef typename K KeyType;
	typedef typename V ValueType;

public:
	typedef typename Base::KeyHashPair KeyHashPair;
	typedef typename Base::KeyValue KeyValue;
	typedef typename Base::Bucket Bucket;

public: // Construction and initialization
	//! \brief
	//! \param[in]
	//! \return
	STATIC_ONLY(AT) inline explicit Hash(const size_t seed = 0) noexcept;

	//! \brief
	//! \param[in]
	//! \param[in]
	//! \return
	HEAP_ONLY(AT) inline Hash(const size_t max_elements, const size_t seed = 0);

	//! \brief
	//! \return
	EXT_ONLY(AT) inline Hash() noexcept;

	//! \brief
	//! \param[in]
	//! \param[in]
	//! \param[in]
	//! \param[in]
	//! \return
	EXT_ONLY(AT)
	inline bool Init(const size_t max_elements,
	                 Bucket* hash,
	                 KeyValue* keyStorage,
	                 std::atomic<KeyValue*>* keyRecycle) noexcept;

public: // Access functions
	//! \brief
	//! \param[in]
	//! \param[in]
	//! \return
	inline bool Add(const K& k, const V& v) noexcept;

	//! \brief
	//! \param[in]
	//! \return
	MODE_NOT_TAKE(MODE) inline const V Read(const K& k) noexcept;

	//! \brief
	//! \param[in]
	//! \param[in]
	//! \return
	MODE_NOT_TAKE(MODE) inline const bool Read(const K& k, V& v) noexcept;

	//! \brief
	//! \param[in]
	//! \return
	MODE_TAKE_ONLY(MODE) inline const V Take(const K& k) noexcept;

	//! \brief
	//! \param[in]
	//! \param[in]
	//! \return
	MODE_TAKE_ONLY(MODE) inline bool Take(const K& k, V& v) noexcept;

	//! \brief
	//! \param[in]
	//! \param[in]
	//! \return
	MODE_TAKE_ONLY(MODE) inline void Take(const K& k, const std::function<bool(const V&)>& receiver) noexcept;

public: // Support functions
	//! \brief
	//! \return
	constexpr static const bool IsAlwaysLockFree() noexcept;

	//! \brief
	//! \return
	inline bool IsLockFree() const noexcept;

private:
	Container<Bucket, _Alloc::ALLOCATOR, _Alloc::KEY_COUNT> m_hash;

	const size_t seed;

	constexpr static const bool _K = std::is_trivially_copyable<K>::value;
	constexpr static const bool _V = std::is_trivially_copyable<V>::value;

	constexpr static const size_t _hash = sizeof(m_hash);
	constexpr static const size_t _key = sizeof(KeyValue);
	constexpr static const size_t _bucket = sizeof(Bucket);

	friend class KeyIterator<Hash<K, V, _Alloc, OP_MODE>>;

	// Validate
	constexpr static const KeyPropertyValidator<K, OP_MODE> VALIDATOR{};
};

/// ******************************************************************************************* ///
///																								///
//										Implementation											///
///																								///
/// ****************************************************************************************** ///

#define HEAP_ONLY_IMPL \
	template <typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_HEAP>::value>::type*>
#define STATIC_ONLY_IMPL \
	template <typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_STATIC>::value>::type*>
#define EXT_ONLY_IMPL \
	template <typename AT, typename std::enable_if<std::is_same<AT, ALLOCATION_TYPE_EXTERNAL>::value>::type*>

#define MODE_READ_ONLY_IMPL_ \
	template <typename _M, \
	          typename std::enable_if<std::is_same<_M, MODE_INSERT_READ>::value \
	                                  || std::is_same<_M, MODE_INSERT_READ_HEAP_BUCKET>::value>::type*>
#define MODE_TAKE_ONLY_IMPL \
	template <typename _M, typename std::enable_if<std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>

#define MODE_NOT_TAKE_IMPL \
	template <typename _M, typename std::enable_if<!std::is_same<_M, MODE_INSERT_TAKE>::value>::type*>

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
STATIC_ONLY_IMPL Hash<K, V, _Alloc, OP_MODE>::Hash(const size_t seed /*= 0*/) noexcept
    : Base()
    , m_hash()
    , seed(seed == 0 ? GenerateSeed() : seed)
{
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
HEAP_ONLY_IMPL Hash<K, V, _Alloc, OP_MODE>::Hash(const size_t max_elements, const size_t seed /*= 0*/)
    : Base(max_elements)
    , m_hash(ComputeHashKeyCount(max_elements))
    , seed(seed == 0 ? GenerateSeed() : seed)
{
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
EXT_ONLY_IMPL Hash<K, V, _Alloc, OP_MODE>::Hash() noexcept
    : seed(GenerateSeed())
{
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
EXT_ONLY_IMPL bool Hash<K, V, _Alloc, OP_MODE>::Init(const size_t max_elements,
                                                     Bucket* hash,
                                                     KeyValue* keyStorage,
                                                     std::atomic<KeyValue*>* keyRecycle) noexcept
{
	if (Base::Init(max_elements))
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

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
bool Hash<K, V, _Alloc, OP_MODE>::Add(const K& k, const V& v) noexcept
{
	KeyValue* pKeyValue = Base::GetNextFreeKeyValue();
	if (pKeyValue == nullptr)
		return false;

	const size_t h = hash(k, seed);
	const size_t index = (h & Base::GetHashMask());

	pKeyValue->v = v;
	pKeyValue->k = KeyHashPair{h, k};
	if (!m_hash[index].Add(pKeyValue))
	{
		Base::ReleaseNode(pKeyValue);
		return false;
		// throw std::bad_alloc();
	}
	return true;
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
MODE_NOT_TAKE_IMPL const V Hash<K, V, _Alloc, OP_MODE>::Read(const K& k) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	KeyValue* keyVal = nullptr;
	if (m_hash[index].ReadValue(h, k, &keyVal))
		return keyVal->v;
	return V();
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
MODE_NOT_TAKE_IMPL const bool Hash<K, V, _Alloc, OP_MODE>::Read(const K& k, V& v) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	return m_hash[index].ReadValue(h, k, v);
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
MODE_TAKE_ONLY_IMPL const V Hash<K, V, _Alloc, OP_MODE>::Take(const K& k) noexcept
{
	V ret = V();

	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	KeyValue* pKeyValue = nullptr;
	if (m_hash[index].TakeValue(k, h, &pKeyValue))
	{
		// Value was found
		ret = pKeyValue->v;

		Base::ReleaseNode(pKeyValue);
	}
	return ret;
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
MODE_TAKE_ONLY_IMPL bool Hash<K, V, _Alloc, OP_MODE>::Take(const K& k, V& v) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	KeyValue* pKeyValue = nullptr;
	if (m_hash[index].TakeValue(k, h, &pKeyValue))
	{
		// Value was found
		v = pKeyValue->v;
		Base::ReleaseNode(pKeyValue);
		return true;
	}
	return false;
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
MODE_TAKE_ONLY_IMPL void Hash<K, V, _Alloc, OP_MODE>::Take(const K& k,
                                                           const std::function<bool(const V&)>& receiver) noexcept
{
	const size_t h = hash(k, seed);
	const size_t index = (h & _Alloc::GetHashMask());
	const auto release = [=](KeyValue* pKey) { this->ReleaseNode(pKey); };
	m_hash[index].TakeValue(k, h, receiver, release);
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
constexpr const bool Hash<K, V, _Alloc, OP_MODE>::IsAlwaysLockFree() noexcept
{
	return KeyValue::IsAlwaysLockFree();
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
bool Hash<K, V, _Alloc, OP_MODE>::IsLockFree() const noexcept
{
	if constexpr (KeyValue::IsAlwaysLockFree())
	{
		return true;
	}
	else
	{
		static KeyValue k;
		return k.k.is_lock_free();
	}
}
