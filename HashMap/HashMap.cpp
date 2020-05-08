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
	v.Add(7, s);
	std::string test = v.Value(7);
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
