// HashMap.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include "Hash.h"
#include "MultiHash.h"
#include <chrono>


template <>
size_t hash(const int& k, const size_t seed)
{
	return size_t(k) ^ seed;
}

struct TT
{
	uint16_t a;
	uint8_t b;
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
		MultiHash_S<int, int, 912> test;
		auto start = std::chrono::steady_clock::now();
		test.Add(181, 1);
		test.Add(191, 1);
		test.Add(201, 1);
		test.Add(211, 1);
		test.Add(221, 1);
		auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
		std::cout << duration.count();
		//return test.Count(181) + test.Count(191) + test.Count(201) + test.Count(211) + test.Count(221);
	}
	{
		MultiHash_S<const char*, int, 912> i;
		i.Add("Message from Hash", 7);
		i.Add("Message from Hash2", 17);
		int val = i.Get("Message from Hash");
		int val2 = i.Get("Message from Hash2");
		val = val;

	}
	{
		MultiHash_S<std::string, int, 9172> i;
		i.Add("Message from Hash", 7);
		i.Add("Message from Hash2", 17);
		int val = i.Get("Message from Hash");
		int val2 = i.Get("Message from Hash2");
		val = val;

	}
	{
		V.Add({ 1, 2, 3 }, "Message from Hash");
		V.Add({ 3, 1, 2 }, "Message from Hash_2");
		V.Add({ 3, 2, 1 }, "Message from Hash_3");
		std::string test = V.Get({ 1, 2, 3 });
		std::string test2 = V.Get({ 3, 1, 2 });
		std::string test3 = V.Get({ 3, 2, 1 });
		auto t = test.size();
	}
	{
		MultiHash_S<int, int, 9172> i;
		constexpr auto size = sizeof(i) / 1024;
		i.Add(100, 12);
		i.Add(100, 13);
		i.Add(100, 15);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(84548, 17);
		i.Add(100, 20);
		int a[5] = { 0 };
		auto c = i.Get(100, a, 5);
		auto cc = i.Count(100);
		const auto t = i.Get(84548);
		c = c;
	}
	{
		MultiHash_H<int, int> i(9172);
		constexpr auto size = sizeof(i);
		i.Add(100, 12);
		i.Add(100, 13);
		i.Add(100, 15);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(100, 17);
		i.Add(84548, 17);
		i.Add(100, 20);
		int a[5] = { 0 };
		auto c = i.Get(100, a, 5);
		auto cc = i.Count(100);
		const auto t = i.Get(84548);
		c = c;
	}
	{
		Hash<int, int, 100> t;
		constexpr auto t_ = sizeof(t);
		Hash<int, std::string, 1000> v;
		constexpr auto v_ = sizeof(v);
		t.Add(rand(), 2);
		int b = 3;
		t.Add(rand(), 2);
		v.Add(rand(), "");
		std::string s("Test");
		v.Add(29382, s);
		v.Add(93932, "Test 2");
		std::string test = v.Take(29382);
	}
	{
		constexpr const auto tt = sizeof(TT);
		Hash<TT, int, 100> a;
		TT t1{ 1,2,3 };
		TT t2{ 3,1,2 };
		TT t3{ 1,3,2 };
		TT t4{ 2,1,3 };
		a.Add(t1, 1);
		a.Add(t2, 2);
		a.Add(t3, 3);
		a.Add(t3, 777);
		a.Add(t3, 4);
		a.Add(t4, 5);
		int t3_ = a.Take(t3);
		auto f = [](const int& obj)
		{
			std::cout << "Hello, " << obj << std::endl;
			return true;
		};
		a.Take(t3, f);
		t3_ = t3_;
	}
	std::cout << "Hello World!\n";
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
