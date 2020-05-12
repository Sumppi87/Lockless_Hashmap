// HashMap.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include "Hash.h"
#include "MultiHash.h"
#include <chrono>
#include <map>
#include <unordered_map>
#include <mutex>
#include <future>

struct TT
{
	uint16_t a;
	uint8_t b;
	uint8_t c;
};

template<typename K, typename V>
struct TT_WriteItem
{
	K key;
	V v;
};

#define TEST_HASHMAP
#define RUN_IN_THREADS

template<size_t SIZE>
struct Rand
{
	void Fill(std::mt19937& engine)
	{
		for (auto i = 0; i < SIZE; ++i)
			data[i] = engine();
	}
	uint32_t data[SIZE];
};

const uint8_t OUTER_ARR_SIZE = 16;
const uint8_t THREADS = 4;
const uint8_t ITEMS_PER_THREAD = OUTER_ARR_SIZE / THREADS;
static_assert((OUTER_ARR_SIZE % THREADS) == 0);
const size_t TEST_ARRAY_SIZE = 100000;
constexpr const size_t ITEMS = OUTER_ARR_SIZE * TEST_ARRAY_SIZE;

TT_WriteItem<int, Rand<16>> TEST_ARRAY[OUTER_ARR_SIZE][TEST_ARRAY_SIZE];
std::map<size_t/*Hash*/, size_t/*number of conflicts*/> HASHES;
std::map<size_t/*hash index*/, size_t/*hash index*/> HASHES_INDEX;

constexpr const size_t HASH_SIZE = ComputeHashKeyCount(ITEMS);

static const bool INIT_ARRAY = []()
{
	std::random_device rd{};
	std::mt19937 engine{ rd() };
	const size_t seed = engine();
	size_t maxHashCollision = 1;
	size_t maxKeyCollision = 1;

	for (uint8_t thread = 0; thread < OUTER_ARR_SIZE; ++thread)
	{
		for (size_t item = 0; item < TEST_ARRAY_SIZE; ++item)
		{
			const int key = ((size_t(thread) << 24) | item) ^ thread;
			TEST_ARRAY[thread][item].key = key;
			TEST_ARRAY[thread][item].v.Fill(engine);
			//TEST_ARRAY[thread][item].v = (int)engine();
			/*const size_t h = hash(key, seed);
			if (HASHES.find(h) == HASHES.end())
				HASHES.insert({ h, 1 });
			else
			{
				size_t& ref = HASHES.at(h);
				ref++;
				maxHashCollision = std::max(ref, maxHashCollision);
			}

			const size_t index = h & (HASH_SIZE - 1);

			if (HASHES_INDEX.find(index) == HASHES_INDEX.end())
				HASHES_INDEX.insert({ index, 1 });
			else
			{
				size_t& ref = HASHES_INDEX.at(index);
				ref++;
				maxKeyCollision = std::max(ref, maxKeyCollision);
			}*/
		}
	}


	return true;
}();

#ifdef TEST_HASHMAP
//Hash<int, Rand<16>> test2(ITEMS);
static Hash<int, Rand<16>, 16, ITEMS> test2;
#define TESTED "Lockless hashmap"
#else
#define TESTED "std::unordered_multimap"
std::mutex testlock;
std::unordered_multimap<int, Rand<16>> test;
#endif

static auto ProcessData(const unsigned int from, const unsigned int to)
{
	auto start = std::chrono::steady_clock::now();
	for (auto index = from; index < to; ++index)
	{
		for (size_t item = 0; item < TEST_ARRAY_SIZE; ++item)
		{
#ifdef TEST_HASHMAP
			test2.Add(TEST_ARRAY[index][item].key, TEST_ARRAY[index][item].v);
#else
#if defined(RUN_IN_THREADS)
			testlock.lock();
			test.insert({ TEST_ARRAY[index][item].key, TEST_ARRAY[index][item].v });
			testlock.unlock();
#else 
			test.insert({ TEST_ARRAY[index][item].key, TEST_ARRAY[index][item].v });
#endif
#endif
		}
	}

	auto end = std::chrono::steady_clock::now() - start;
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);
	return duration;
}

static void ProcessDatas()
{
	auto start = std::chrono::steady_clock::now();

#ifdef RUN_IN_THREADS
	std::vector<std::future<std::chrono::milliseconds>> vec;
	for (auto i = 0; i < THREADS; ++i)
		vec.push_back(std::async(std::launch::async, ProcessData, i * ITEMS_PER_THREAD, i * ITEMS_PER_THREAD + ITEMS_PER_THREAD));
	for (auto& v : vec)
		v.wait();

	for (size_t i = 0; i < vec.size(); ++i)
	{
		auto res = vec[i].get().count();
		std::cout << i << " - Execution time: " << res << std::endl;
	}
#else
	ProcessData(0, OUTER_ARR_SIZE);
#endif

	auto end = std::chrono::steady_clock::now() - start;
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);
	std::cout << "Total execution time: " << duration.count() << std::endl;
}

static bool ValidateData(const unsigned int from, const unsigned int to)
{
	auto start = std::chrono::steady_clock::now();

	bool OK = true;
	Rand<16> res[2]{};
	Rand<16>* pRes = &res[0];
	for (size_t thread = from; thread < to; ++thread)
	{
		for (size_t item = 0; item < TEST_ARRAY_SIZE; ++item)
		{
			int vals = 0;
			auto receiver = [pRes, &vals](const Rand<16>& val)
			{
				Rand<16>* p = pRes + vals;
				memcpy(p, &val.data, sizeof(Rand<16>));
				++vals;
				return vals < 2;
			};

			const auto tt = TEST_ARRAY[thread][item];
#ifdef TEST_HASHMAP
			test2.Take(tt.key, receiver);
#else
			vals = test.count(tt.key);
			res[0] = test.find(tt.key)->second;
#endif
			bool ok = (vals == 1) && (memcmp(res[0].data, tt.v.data, sizeof(tt.v.data)) == 0);
			//bool ok = (vals == 1) && (res[0].data == tt.v.data);
			OK &= ok;
			assert(ok);
		}
	}

#ifdef RUN_IN_THREADS
	auto end = std::chrono::steady_clock::now() - start;
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);

	static std::mutex std_cout_lock;
	{
		std::lock_guard locker(std_cout_lock);
		std::cout << unsigned int(from / ITEMS_PER_THREAD) << " - Validation execution time: " << duration.count() << std::endl;
	}
#endif //  RUN_IN_THREAD

	return OK;
}

static bool ValidateDatas()
{
	bool ret = true;

#if defined(RUN_IN_THREADS) //&& defined(TEST_HASHMAP)
	std::vector<std::future<bool>> vec;
	for (auto i = 0; i < THREADS; ++i)
		vec.push_back(std::async(std::launch::async, ValidateData, i * ITEMS_PER_THREAD, i * ITEMS_PER_THREAD + ITEMS_PER_THREAD));

	for (auto& v : vec)
		v.wait();

	for (size_t i = 0; i < vec.size(); ++i)
	{
		bool res = vec[i].get();
		//std::cout << i << " - Validation Result: " << (res ? "SUCCEEDED" : "FAILED") << std::endl;
		ret &= res;
	}
#else
	//for (auto i = 0; i < OUTER_ARR_SIZE; ++i)
	ret &= ValidateData(0, OUTER_ARR_SIZE);
#endif
	return ret;
	}

constexpr auto _TESTARRAY = sizeof(TEST_ARRAY);

int main()
{
#ifndef TEST_HASHMAP
	test.reserve(ITEMS);
#endif // !TEST_HASHMAP
	
	ProcessDatas();

	auto start = std::chrono::steady_clock::now();
	bool ret = ValidateDatas();
	std::cout << "Validation result " << (ret ? "OK" : "ERROR") << std::endl;
	auto end = std::chrono::steady_clock::now() - start;
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);
	std::cout << "Validation for " << TESTED << " took " << duration.count() << std::endl;
	return 0;
}

bool operator==(const TT& o, const TT& t)
{
	return o.a == t.a && o.b == t.b && o.c == t.c;
}

template <>
size_t hash(const TT& k, const size_t seed)
{
	union Conv
	{
		Conv(const TT& t) :
			_1(t.b), _2(t.c), _3(t.a) {}

		uint64_t v;
		struct
		{
			uint8_t _1;
			uint8_t _2;
			uint16_t _3;
		};
	};
	return hash(Conv(k).v, 0);
	return hash(size_t(size_t(k.a << 16) ^ (size_t(k.b) << 24) ^ (size_t(k.c) << 8)), 0);
	return hash(size_t((k.b << (k.c^k.b)) ^ k.a), seed);
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

template<typename Hash>
void TestHash(Hash& a)
{
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

template<typename Hash>
int Chrono(Hash& test)
{
	{
		auto start = std::chrono::steady_clock::now();
		test.Add(181, 1);
		test.Add(191, 1);
		test.Add(201, 1);
		test.Add(211, 1);
		test.Add(221, 1);
		auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
		std::cout << duration.count() << std::endl;
		return test.Take(181) + test.Take(191) + test.Take(201) + test.Take(211) + test.Take(221);
	}
}

void TestStatic()
{
	auto start = std::chrono::steady_clock::now();
	static Hash<int, int, 32, 300000> test;
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
	std::cout << duration.count() << std::endl;
	constexpr auto size = sizeof(test) / 1024.0;
	Chrono(test);
}

void TestHeap()
{
	auto start = std::chrono::steady_clock::now();
	Hash<int, int> test(1000000);
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
	std::cout << duration.count() << std::endl;
	constexpr auto size = sizeof(test) / 1024.0;
	Chrono(test);
}

typedef BucketT<int, int, 32> Test;
static Test __t[ComputeHashKeyCount(4000000)];

/*int main()
{
	Container<Test> hash(ComputeHashKeyCount(4000000), __t);

	TestStatic();
	TestHeap();
	{
		Hash<int, int> test(912);
		test.Add(1, 1);
		test.Add(1, 2);
		test.Add(1, 3);
		test.Add(1, 1);
		test.Add(2, 2);
		int _1 = test.Take(1);
		int _11 = test.Take(1);
		int _12 = test.Take(1);
		int _13 = test.Take(1);
		int _14 = test.Take(1);
		int _2 = test.Take(2);
		_2 = _2;
	}

	{
		Hash<int, int> test(912);
		constexpr auto size = sizeof(test);
		constexpr auto heap = Hash<int, int>::NeededHeap(912) / 1024.0;
		Chrono(test);
	}
	{
		Container<int> heap(1001);
		Container<bool, 10> _static;
		std::cout << heap[9] << std::endl;
		std::cout << _static[9] << std::endl;
	}
	{
		MultiHash_S<int, int, 912> test;
		auto start = std::chrono::steady_clock::now();
		test.Add(181, 1);
		test.Add(191, 1);
		test.Add(201, 1);
		test.Add(211, 1);
		test.Add(221, 1);
		auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start);
		std::cout << duration.count() << std::endl;
		//return test.Count(181) + test.Count(191) + test.Count(201) + test.Count(211) + test.Count(221);
		//return 0;
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
		MultiHash_S<int, int, 912> i;
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
		MultiHash_H<int, int> i(917);
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
		Hash<int, int, 8, 100> t;
		constexpr auto t_ = sizeof(t);
		Hash<int, std::string, 8, 1000> v;
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
		//Hash<TT, int>::Bucket bucket[ComputeHashKeyCount(100)]{};
		Container< Hash<TT, int>::Bucket> bucket(ComputeHashKeyCount(100));
		Hash<TT, int>::KeyValue keys[100]{};
		std::atomic<Hash<TT, int>::KeyValue*> keyRecycle[100];
		Hash<TT, int> a(size_t(100), &bucket[0], &keys[0], &keyRecycle[0]);
		TestHash(a);
	}
	{
		Hash<TT, int> a(100);
		TestHash(a);
	}

	{
		Hash<TT, int, 8, 100> a;
		TestHash(a);
	}
	std::cout << "Hello World!\n";
}*/

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started:
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
