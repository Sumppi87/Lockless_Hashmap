// HashMap.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include "Hash.h"
#include "MultiHash.h"

template <>
size_t hash(const int& k, const size_t seed)
{
	return size_t(k) ^ seed;
}

struct TT
{
	uint16_t a;
	uint16_t b;
	uint8_t c;
};

bool operator==(const TT& o, const TT& t)
{
	return o.a == t.a && o.b == t.b && o.c == t.c;
}

template <>
size_t hash(const TT& k, const size_t seed)
{
	return size_t((k.b << (k.c^k.b)) ^ k.a) ^ seed;
}

template <>
size_t hash(const std::string& s, const size_t seed)
{
#define FNV_PRIME_32         16777619
#define FNV_OFFSET_BASIS_32  2166136261

	uint32_t fnv = FNV_OFFSET_BASIS_32;
	for (size_t i = 0; i < s.size(); ++i)
	{
		fnv = fnv ^ (s[i]);
		fnv = fnv * FNV_PRIME_32;
	}
	return hash(fnv, seed);
}

static MultiHash_S<TT, std::string, 1000> V;

int main()
{

	{
		MultiHash_S<int, int, 10191> i;
		i.Add(100, 12);
		i.Add(100, 13);
		i.Add(100, 15);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(84548, 17);
		i.Add(100, 20);
		int a[5] = { 0 };
		auto c = i.Get(100, a, 7);
		auto cc = i.Count(100);
		const auto t = i.Get(84548);
		c = c;
	}
	{
		MultiHash_H<int, int> i(10191);
		i.Add(100, 12);
		i.Add(100, 13);
		i.Add(100, 15);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(84548, 17);
		i.Add(100, 20);
		int a[5] = { 0 };
		auto c = i.Get(100, a, 7);
		auto cc = i.Count(100);
		const auto t = i.Get(84548);
		c = c;
	}
	{
		constexpr size_t max_elements = 10191;
		typedef BucketT<int, int> Bucket;
		uint8_t storage[sizeof(Bucket) * max_elements]{ 0 };
		uint8_t hash[sizeof(std::atomic<Bucket*>*) * MultiHash<int, int>::ComputeHashKeyCount(max_elements * 2)]{ 0 };

		MultiHash<int, int> i((std::atomic<Bucket*>*)&hash, (Bucket*)&storage, max_elements);
		i.Add(100, 12);
		i.Add(100, 13);
		i.Add(100, 15);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(84548, 17);
		i.Add(100, 20);
		int a[5] = { 0 };
		auto c = i.Get(100, a, 7);
		auto cc = i.Count(100);
		const auto t = i.Get(84548);
		c = c;
	}


	Hash<int, int, 100> t;
	constexpr auto t_ = sizeof(t);
	Hash<int, std::string, 1000> v;
	constexpr auto v_ = sizeof(v);
	t.Add(rand(), 2);
	int a = 2;
	int b = 3;
	t.Add(rand(), 2);
	v.Add(rand(), "");
	std::string s("Test");
	v.Add(29382, s);
	v.Add(93932, "Test 2");
	std::string test = v.Get(29382);
	std::cout << "Hello World!\n";

	constexpr const auto tt = sizeof(TT);
	Hash<TT, int, 100>().Add(TT{ 1,2,3 }, 1);
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
