#pragma once
#include <assert.h>
#include <functional>
#include "Internal/HashFunctions.h"
#include "Internal/HashUtils.h"
#include "Internal/UtilityFunctions.h"
#include "Internal/HashBase.h"

#include "HashIterator.h"

static_assert(__cplusplus >= 201103L, "C++11 or later required!");

template <uint32_t MAX_ELEMENTS, uint32_t BUCKET_SIZE = DEFAULT_COLLISION_SIZE>
struct StaticAllocator : public Allocator<AllocatorType::STATIC>,
                         public StaticSizes<BUCKET_SIZE, MAX_ELEMENTS, ComputeHashKeyCount(MAX_ELEMENTS)>
{
	static_assert(MAX_ELEMENTS > 0, "Element count cannot be zero");
};

template <uint32_t BUCKET_SIZE = DEFAULT_COLLISION_SIZE>
struct HeapAllocator : public Allocator<AllocatorType::HEAP>, public StaticSizes<BUCKET_SIZE>
{
};

template <uint32_t BUCKET_SIZE = DEFAULT_COLLISION_SIZE>
struct ExternalAllocator : public Allocator<AllocatorType::EXTERNAL>, public StaticSizes<BUCKET_SIZE>
{
};

template <typename K,
          typename V,
          typename _Alloc = HeapAllocator<>,
          MapMode OP_MODE = DefaultModeSelector<K, _Alloc>::MODE>
class Hash : public RESOLVE_BASE_CLASS(OP_MODE, K, V, _Alloc)
{
	typedef typename RESOLVE_BASE_CLASS(OP_MODE, K, V, _Alloc) Base;
	typedef typename std::integral_constant<MapMode, OP_MODE> MODE;
	typedef typename Base::AT AT;

public:
	typedef typename Base::KeyValue KeyValue;
	typedef typename Base::Bucket Bucket;

public: // Construction and initialization
	//! \brief
	//! \param[in]
	//! \return
	STATIC_ONLY(AT) inline explicit Hash(const uint32_t seed = 0) noexcept;

	//! \brief
	//! \param[in]
	//! \param[in]
	//! \return
	HEAP_ONLY(AT) inline Hash(const uint32_t max_elements, const uint32_t seed = 0);

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
	inline bool Init(const uint32_t max_elements,
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
	//! \param[in]
	//! \return
	MODE_NOT_TAKE(MODE) inline void Read(const K& k, const std::function<bool(const V&)>& receiver) noexcept;

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

	constexpr static const MapMode GetMapMode() noexcept;

private:
	Container<Bucket, _Alloc::ALLOCATOR, _Alloc::KEY_COUNT> m_hash;

	const uint32_t seed;

	friend class HashIterator<Hash<K, V, _Alloc, OP_MODE>>;

	// Validate
	constexpr static const KeyPropertyValidator<K, OP_MODE> VALIDATOR{};

	typedef typename Base::KeyHashPair KeyHashPair;
	typedef typename K KeyType;
	typedef typename V ValueType;

	DISABLE_COPY_MOVE(Hash)
};

/// ******************************************************************************************* ///
///																								///
//										Implementation											///
///																								///
/// ****************************************************************************************** ///

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
STATIC_ONLY_IMPL Hash<K, V, _Alloc, OP_MODE>::Hash(const uint32_t seed /*= 0*/) noexcept
    : Base()
    , m_hash()
    , seed(seed == 0 ? GenerateSeed() : seed)
{
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
HEAP_ONLY_IMPL Hash<K, V, _Alloc, OP_MODE>::Hash(const uint32_t max_elements, const uint32_t seed /*= 0*/)
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
EXT_ONLY_IMPL bool Hash<K, V, _Alloc, OP_MODE>::Init(const uint32_t max_elements,
                                                     Bucket* hash,
                                                     KeyValue* keyStorage,
                                                     std::atomic<KeyValue*>* keyRecycle) noexcept
{
	if (Base::Init(max_elements))
	{
		m_hash.Init(hash, ComputeHashKeyCount(max_elements));
		Base::m_keyStorage.Init(keyStorage, max_elements);
		Base::m_recycle.Init(keyRecycle, max_elements);

		for (uint32_t i = 0; i < Base::GetMaxElements(); ++i)
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

	const uint32_t h = hash(k, seed);
	const uint32_t index = (h & Base::GetHashMask());

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
	const uint32_t h = hash(k, seed);
	const uint32_t index = (h & Base::GetHashMask());
	KeyValue* keyVal = nullptr;
	if (m_hash[index].ReadValue(h, k, &keyVal))
		return keyVal->v;
	return V();
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
MODE_NOT_TAKE_IMPL const bool Hash<K, V, _Alloc, OP_MODE>::Read(const K& k, V& v) noexcept
{
	const uint32_t h = hash(k, seed);
	const uint32_t index = (h & Base::GetHashMask());
	return m_hash[index].ReadValue(h, k, v);
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
MODE_NOT_TAKE_IMPL void Hash<K, V, _Alloc, OP_MODE>::Read(const K& k,
                                                          const std::function<bool(const V&)>& receiver) noexcept
{
	const uint32_t h = hash(k, seed);
	const uint32_t index = (h & Base::GetHashMask());
	m_hash[index].ReadValue(h, k, receiver);
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
MODE_TAKE_ONLY_IMPL const V Hash<K, V, _Alloc, OP_MODE>::Take(const K& k) noexcept
{
	V ret = V();

	const uint32_t h = hash(k, seed);
	const uint32_t index = (h & Base::GetHashMask());
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
	const uint32_t h = hash(k, seed);
	const uint32_t index = (h & _Alloc::GetHashMask());
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
	const uint32_t h = hash(k, seed);
	const uint32_t index = (h & Base::GetHashMask());
	const auto release = [=](KeyValue* pKey) { this->ReleaseNode(pKey); };
	m_hash[index].TakeValue(k, h, receiver, release);
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
constexpr const bool Hash<K, V, _Alloc, OP_MODE>::IsAlwaysLockFree() noexcept
{
	return KeyValue::IsAlwaysLockFree();
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
constexpr const MapMode GetMapMode() noexcept
{
	return OP_MODE;
}

template <typename K, typename V, typename _Alloc, MapMode OP_MODE>
bool Hash<K, V, _Alloc, OP_MODE>::IsLockFree() const noexcept
{
	if constexpr (IsAlwaysLockFree())
	{
		return true;
	}
	else
	{
		static KeyValue k;
		return k.k.is_lock_free();
	}
}
