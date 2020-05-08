// HashMap.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include "Hash.h"

template <>
size_t hash(const int& k)
{
	return k;
}

struct TT
{
	uint16_t a;
	uint16_t b;
	uint8_t c;
};

template <>
size_t hash(const TT& k)
{
	return k.a + (k.b << 16) | k.c;
}

static Hash<int, std::string, 100000> V;

int main()
{
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
	Hash<TT, int, 100>().Add(TT{1,2,3}, 1);
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
