#pragma once
#include "Hash.h"

template <typename _Hash>
class HashIterator
{
	typedef typename _Hash::KeyType K;
	typedef typename _Hash::ValueType V;
	typedef typename _Hash::Bucket Bucket;
	typedef typename Bucket::Iterator Iterator;

public:
	explicit HashIterator(_Hash& hash) noexcept;

	~HashIterator() noexcept;

	inline HashIterator& SetKey(const K& k) noexcept;

	//! \brief Resets the iterator back to initial position (i.e. Same as calling SetKey again)
	inline HashIterator& Reset() noexcept;

	inline bool Next() noexcept;

	inline V& Value() noexcept;

	inline const V& Value() const noexcept;

private:
	MODE_NOT_TAKE(_Hash::MODE) inline void SetIter() noexcept;
	MODE_TAKE_ONLY(_Hash::MODE) inline void SetIter() noexcept;

private:
	_Hash& _hash;
	Iterator _iter;
	K _k;
	uint32_t _h;
	typename _Hash::Bucket* _bucket;
	typename _Hash::KeyValue* _keyValue;

	std::function<void(typename _Hash::KeyValue*)> _release;
#if defined(_DEBUG) || defined(VALIDATE_ITERATOR_NON_CONCURRENT_ACCESS)
	std::atomic<uint32_t> _counter;
#endif // !_DEBUG
};

template <typename _Hash>
HashIterator<_Hash>::HashIterator(_Hash& hash) noexcept
    : _hash(hash)
    , _h(0)
    , _bucket(nullptr)
    , _keyValue(nullptr)
{
}

template <typename _Hash>
HashIterator<_Hash>::~HashIterator() noexcept
{
	if (_keyValue)
		_hash.ReleaseNode(_keyValue);
}

template <typename _Hash>
HashIterator<_Hash>& HashIterator<_Hash>::SetKey(const K& k) noexcept
{
	CHECK_CONCURRENT_ACCESS(_counter);

	TRACE(typeid(Iterator).name(), " SetKey()");

	_k = k;
	_h = _hash.GetKeyHash(k);
	const auto index = _hash.GetKeyIndex(_h);

	_bucket = &_hash.m_hash[index];

	SetIter();
	return *this;
}

//! \brief Resets the iterator back to initial position (i.e. Same as calling SetKey again)
template <typename _Hash>
HashIterator<_Hash>& HashIterator<_Hash>::Reset() noexcept
{
	CHECK_CONCURRENT_ACCESS(_counter);
	SetIter();
	return *this;
}

template <typename _Hash>
bool HashIterator<_Hash>::Next() noexcept
{
	CHECK_CONCURRENT_ACCESS(_counter);
	TRACE(typeid(Iterator).name(), " Next()");
	return _iter.Next();
}

template <typename _Hash>
typename _Hash::ValueType& HashIterator<_Hash>::Value() noexcept
{
	CHECK_CONCURRENT_ACCESS(_counter);
	TRACE(typeid(Iterator).name(), " Value()");
	return _iter.Value();
}

template <typename _Hash>
const typename _Hash::ValueType& HashIterator<_Hash>::Value() const noexcept
{
	CHECK_CONCURRENT_ACCESS(_counter);
	TRACE(typeid(Iterator).name(), " Value() const");
	return _iter.Value();
}

template <typename _Hash>
MODE_NOT_TAKE_IMPL void HashIterator<_Hash>::SetIter() noexcept
{
	TRACE(typeid(Iterator).name(), " SetIter()");
	_iter = Iterator(_bucket, _h, _k);
}

template <typename _Hash>
MODE_TAKE_ONLY_IMPL void HashIterator<_Hash>::SetIter() noexcept
{
	TRACE(typeid(Iterator).name(), " SetIter()");
	_release = [=](typename _Hash::KeyValue* pKey) { _hash.ReleaseNode(pKey); };
	_iter = Iterator(_bucket, _h, _k, _release);
}
