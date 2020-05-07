#pragma once
#include <atomic>
#include <intrin.h>

#pragma intrinsic(_BitScanReverse)

template<typename K, typename V, size_t MAX_ELEMENTS>
class Hash
{
private:
	constexpr static size_t ComputeHashKeyCount()
	{
		size_t v = MAX_ELEMENTS * 2;

		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}
	constexpr static const size_t KEY_COUNT = ComputeHashKeyCount();
	constexpr static const size_t MASK = KEY_COUNT - 1;
public:
	Hash() :
		m_hash{nullptr}
	{

	}



private:

	template<typename K, typename V>
	class HashKeyT
	{
	public:
		HashKeyT() :
			k(),
			v(),
			hash(),
			recycled(true),
			accessing(false)
		{

		}
		K k;
		V v;
		size_t hash;
		std::atomic_bool recycled;
		std::atomic_bool accessing;
	};
	typedef HashKeyT<K, V>  HashKey;

	std::atomic<HashKey*> m_hash[MAX_ELEMENTS];
	HashKey m_recycle[KEY_COUNT];
};