#pragma once
#include <atomic>

template<typename K, typename V, size_t size>
class Hash
{
public:




private:

	template<typename K, typename V>
	class HashKeyT
	{
	public:
		K k;
		V v;
		size_t hash;
		std::atomic_bool recycled;
		std::atomic_bool access_ongoing;
	};
	typedef HashKeyT<K, V>  HashKey;

	HashKey m_keys[size];
};