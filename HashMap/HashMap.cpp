// HashMap.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include "Hash.h"
#include "HashIterator.h"
#include <chrono>
#include <map>
#include <unordered_map>
#include <mutex>
#include <future>

template <typename Hash>
void TestHash(Hash& a);
void someTests();

struct TT
{
	uint16_t a;
	uint8_t b;
	uint8_t c;

	std::string toString() const
	{
		return "TT{" + std::to_string(a) + ", " + std::to_string(b) + ", " + std::to_string(c) + "}";
	}
};

template <typename K, typename V>
struct TT_WriteItem
{
	K key;
	V v;
};

enum Components
{
	SUT_STD_UNORDERED_MULTIMAP,
	SUT_HASHMAP_INSERT_TAKE,
	SUT_HASHMAP_INSERT_READ,
	SUT_HASHMAP_INSERT_READ_HEAP_BUCKET,
	SUT_SIZE = SUT_HASHMAP_INSERT_READ_HEAP_BUCKET + 1
};

enum HashMemAllocator
{
	HEAP,
	STATIC,
	EXT
};

constexpr static const Components SUT = Components::SUT_HASHMAP_INSERT_TAKE;
constexpr static const HashMemAllocator HashAllocator = HashMemAllocator::STATIC;
constexpr static const bool validateWithIterators = 0;
constexpr static const bool validateForExtraItems = false;
constexpr static const uint8_t THREADS = 1;

constexpr static const char* TESTED[SUT_SIZE] = {"std::unordered_multimap",
                                                 "Hash(insert take)",
                                                 "Hash(insert read)",
                                                 "Hash(insert read HEAP)"};

static std::mutex testlock;

template <uint32_t SIZE>
struct Rand
{
	void Fill(std::mt19937& engine)
	{
		for (auto i = 0; i < SIZE; ++i)
			data[i] = engine();
	}
	uint32_t data[SIZE];
};

const uint8_t OUTER_ARR_SIZE = 24;
const uint8_t ITEMS_PER_THREAD = OUTER_ARR_SIZE / THREADS;
static_assert((OUTER_ARR_SIZE % THREADS) == 0);
const uint32_t TEST_ARRAY_SIZE = 85000;
constexpr const uint32_t ITEMS = OUTER_ARR_SIZE * TEST_ARRAY_SIZE;

TT_WriteItem<int, Rand<16>> TEST_ARRAY[OUTER_ARR_SIZE][TEST_ARRAY_SIZE];
std::map<uint32_t /*Hash*/, uint32_t /*number of conflicts*/> HASHES;
std::map<uint32_t /*hash index*/, uint32_t /*hash index*/> HASHES_INDEX;

constexpr const uint32_t HASH_SIZE = ComputeHashKeyCount(ITEMS);

static const bool INIT_ARRAY = []() {
	std::random_device rd{};
	std::mt19937 engine{rd()};
	const uint32_t seed = engine();
	uint32_t maxHashCollision = 1;
	uint32_t maxKeyCollision = 1;

	for (uint8_t thread = 0; thread < OUTER_ARR_SIZE; ++thread)
	{
		for (uint32_t item = 0; item < TEST_ARRAY_SIZE; ++item)
		{
			const int key = ((uint32_t(thread) << 24) | item) ^ thread;
			TEST_ARRAY[thread][item].key = key;
			TEST_ARRAY[thread][item].v.Fill(engine);
			// TEST_ARRAY[thread][item].v = (int)engine();
			/*const uint32_t h = hash(key, seed);
			if (HASHES.find(h) == HASHES.end())
			    HASHES.insert({ h, 1 });
			else
			{
			    uint32_t& ref = HASHES.at(h);
			    ref++;
			    maxHashCollision = std::max(ref, maxHashCollision);
			}

			const uint32_t index = h & (HASH_SIZE - 1);

			if (HASHES_INDEX.find(index) == HASHES_INDEX.end())
			    HASHES_INDEX.insert({ index, 1 });
			else
			{
			    uint32_t& ref = HASHES_INDEX.at(index);
			    ref++;
			    maxKeyCollision = std::max(ref, maxKeyCollision);
			}*/
		}
	}

	return true;
}();

template <typename Map>
static auto ProcessData(const unsigned int from, const unsigned int to, Map& map)
{
	auto start = std::chrono::steady_clock::now();
	for (auto index = from; index < to; ++index)
	{
		for (uint32_t item = 0; item < TEST_ARRAY_SIZE; ++item)
		{
			if constexpr (SUT != Components::SUT_STD_UNORDERED_MULTIMAP)
			{
				if (!map.Add(TEST_ARRAY[index][item].key, TEST_ARRAY[index][item].v))
				{
					assert(0);
					throw "ERROR: Unable to insert data to map";
				}
			}
			else if constexpr (SUT == Components::SUT_STD_UNORDERED_MULTIMAP)
			{
				if constexpr (THREADS > 1)
				{
					testlock.lock();
					map.insert({TEST_ARRAY[index][item].key, TEST_ARRAY[index][item].v});
					testlock.unlock();
				}
				else
				{
					map.insert({TEST_ARRAY[index][item].key, TEST_ARRAY[index][item].v});
				}
			}
		}
	}

	auto end = std::chrono::steady_clock::now() - start;
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);
	return duration;
}

template <typename Map>
static void ProcessDatas(Map& map)
{
	auto start = std::chrono::steady_clock::now();

	if constexpr (THREADS > 1)
	{
		std::vector<std::future<std::chrono::milliseconds>> vec;
		for (auto i = 0; i < THREADS; ++i)
			vec.push_back(std::async(
			    std::launch::async,
			    [&map](const unsigned int from, const unsigned int to) { return ProcessData(from, to, map); },
			    i * ITEMS_PER_THREAD,
			    i * ITEMS_PER_THREAD + ITEMS_PER_THREAD));
		for (auto& v : vec)
			v.wait();

		for (uint32_t i = 0; i < vec.size(); ++i)
		{
			auto res = vec[i].get().count();
			std::cout << i << " - Execution time: " << res << std::endl;
		}
	}
	else
	{
		ProcessData(0, OUTER_ARR_SIZE, map);
	}
	auto end = std::chrono::steady_clock::now() - start;
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);
	std::cout << "Total execution time: " << duration.count() << std::endl;
}

template <typename Map>
static bool ValidateData(const unsigned int from, const unsigned int to, Map& map)
{
	Rand<16> res[2]{};
	Rand<16>* pRes = &res[0];

	auto start = std::chrono::steady_clock::now();
	bool OK = true;

	for (uint32_t thread = from; thread < to; ++thread)
	{
		for (uint32_t item = 0; item < TEST_ARRAY_SIZE; ++item)
		{
			int vals = 0;
			auto receiver = [pRes, &vals](const Rand<16>& val) {
				Rand<16>* p = pRes + vals;
				memcpy(p, &val.data, sizeof(Rand<16>));
				++vals;
				return vals < 2;
			};

			const auto tt = TEST_ARRAY[thread][item];
			if constexpr ((SUT != Components::SUT_STD_UNORDERED_MULTIMAP))
			{
				if constexpr (validateWithIterators)
				{
					HashIterator iter(map);
					iter.SetKey(tt.key);
					bool ok = iter.Next() && (memcmp(iter.Value().data, tt.v.data, sizeof(tt.v.data)) == 0);
					OK &= ok;
					assert(ok);

					if constexpr (validateForExtraItems)
					{
						const bool noExtraItems = (iter.Next() == false);
						OK &= noExtraItems;
						assert(noExtraItems);
					}
				}
				else if constexpr (SUT == Components::SUT_HASHMAP_INSERT_TAKE)
				{
					if constexpr (validateForExtraItems)
					{
						map.Take(tt.key, receiver);
						bool ok = (vals == 1) && (memcmp(res[0].data, tt.v.data, sizeof(tt.v.data)) == 0);
					}
					else
					{
						const auto& val = map.Take(tt.key);
						bool ok = (memcmp(val.data, tt.v.data, sizeof(tt.v.data)) == 0);
						OK &= ok;
						assert(ok);
					}
				}
				else if constexpr (SUT == Components::SUT_HASHMAP_INSERT_READ)
				{
					if constexpr (validateForExtraItems)
					{
						map.Read(tt.key, receiver);
						bool ok = (vals == 1) && (memcmp(res[0].data, tt.v.data, sizeof(tt.v.data)) == 0);
					}
					else
					{
						const auto& val = map.Read(tt.key);
						bool ok = (memcmp(val.data, tt.v.data, sizeof(tt.v.data)) == 0);
						OK &= ok;
						assert(ok);
					}
				}
			}
			else
			{
				vals = map.count(tt.key);
				res[0] = map.find(tt.key)->second;
				bool ok = (vals == 1) && (memcmp(res[0].data, tt.v.data, sizeof(tt.v.data)) == 0);
				OK &= ok;
				assert(ok);
			}
		}
	}

	if constexpr (THREADS > 1)
	{
		auto end = std::chrono::steady_clock::now() - start;
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);

		static std::mutex std_cout_lock;
		{
			std::lock_guard locker(std_cout_lock);
			std::cout << int(from / ITEMS_PER_THREAD) << " - Validation execution time: " << duration.count()
			          << std::endl;
		}
	}

	return OK;
}

template <typename Map>
static bool ValidateDatas(Map& map)
{
	auto start = std::chrono::steady_clock::now();

	bool ret = true;

	if constexpr (THREADS > 1)
	{
		std::vector<std::future<bool>> vec;
		for (auto i = 0; i < THREADS; ++i)
			vec.push_back(std::async(
			    std::launch::async,
			    [&map](const unsigned int from, const unsigned int to) { return ValidateData(from, to, map); },
			    i * ITEMS_PER_THREAD,
			    i * ITEMS_PER_THREAD + ITEMS_PER_THREAD));

		for (auto& v : vec)
			v.wait();

		for (uint32_t i = 0; i < vec.size(); ++i)
		{
			bool res = vec[i].get();
			ret &= res;
		}
	}
	else
	{
		ret &= ValidateData(0, OUTER_ARR_SIZE, map);
	}

	std::cout << "Validation result " << (ret ? "OK" : "ERROR") << std::endl;
	auto end = std::chrono::steady_clock::now() - start;
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);
	std::cout << "Validation for " << TESTED[SUT] << " took " << duration.count() << std::endl;

	return ret;
}

uint32_t hash(const std::integral_constant<uint32_t, 1>& k, const uint32_t seed)
{
	return hash(k.value, seed);
}

template <typename K, MapMode mode, bool assert = false>
void TestKey()
{
	HashKeyProperties<K, mode> a;
	constexpr bool _a1 = a.VALID_KEY_TYPE;
	int i = 0;
	i = 0;

	if constexpr (assert)
	{
		KeyPropertyValidator<K, mode> a;
	}
}

constexpr static const MapMode GetMapMode()
{
	if constexpr (SUT == Components::SUT_HASHMAP_INSERT_READ)
	{
		return MapMode::PARALLEL_INSERT_READ;
	}
	else if constexpr (SUT == Components::SUT_HASHMAP_INSERT_TAKE)
	{
		return MapMode::PARALLEL_INSERT_TAKE;
	}
	else
	{
		return MapMode::PARALLEL_INSERT_READ_GROW_FROM_HEAP;
	}
}

int main()
{
	try
	{
		auto iters = 0;
		for (auto i = 1;; ++i)
		{
			std::cout << "*************************************************" << std::endl;
			std::cout << "************************************** iteration: " << std::to_string(i) << std::endl;

			if constexpr (SUT != Components::SUT_STD_UNORDERED_MULTIMAP)
			{
				if constexpr (HashAllocator == HashMemAllocator::HEAP)
				{
					Hash<int, Rand<16>, HeapAllocator<32>, GetMapMode()> map(ITEMS);
					ProcessDatas(map);
					if (!ValidateDatas(map))
						return -1;
				}
				else if constexpr (HashAllocator == HashMemAllocator::STATIC)
				{
					static Hash<int, Rand<16>, StaticAllocator<ITEMS, 32>, GetMapMode()> map;
					ProcessDatas(map);
					if (!ValidateDatas(map))
						return -1;
				}
			}
			else
			{
				std::unordered_multimap<int, Rand<16>> map;
				map.reserve(ITEMS);
				ProcessDatas(map);
				if (!ValidateDatas(map))
					return -1;
			}

			std::cout << "###################################### iteration: " << std::to_string(i) << std::endl;
			std::cout << "#################################################" << std::endl;

			// return 0;

			if constexpr (GetMapMode() != MapMode::PARALLEL_INSERT_TAKE && HashAllocator == HashMemAllocator::STATIC)
				// static map can be tested once only when not INSERT_TAKE
				break;
#ifdef _DEBUG
				// break;
#endif
		}
	}
	catch (std::bad_alloc& alloc)
	{
		std::cerr << alloc.what() << std::endl;
		return -1;
	}
	catch (...)
	{
		std::cerr << "Unknown exception" << std::endl;
		return -1;
	}

	someTests();
	{
		KeyValueLinkedList<std::string, int> test;
		KeyValueLinkedList<std::string, int> test1;
		test.pNext = &test1;
		test.k.hash = 1;
		test.k.key = "test";
		test.v = 1;
	}
	Hash<std::string, int> str_(100);
	std::string s("1");
	HashIterator iterRead(str_);
	iterRead.SetKey(s);
	iterRead.Next();

	int* p = nullptr;
	int& ref = *(int*)nullptr;

	TT tt_{1, 2, 3};
	Hash<TT, int> TT_(100);
	TT_.Add(tt_, 1);
	HashIterator iterTake(TT_);
	iterTake.SetKey(tt_);
	iterTake.Next();

	str_.Add("1", 1);
	str_.Add("1", 12);
	str_.Add("1", 123);
	str_.Add("1", 1234);
	str_.Add("1", 12345);
	str_.Add("1", 123456);
	auto _v = str_.Read("");
	auto __v = str_.Read("1");
	while (iterRead.Next())
	{
		std::cout << iterRead.Value() << std::endl;
	}
	iterRead.Reset();
	while (iterRead.Next())
	{
		std::cout << iterRead.Value() << std::endl;
	}
	struct Big
	{
		// std::string _c;
		int a : 30;
		int b : 2;
		int _val : 1;
	};

	TestKey<int, MapMode::PARALLEL_INSERT_TAKE, true>();
	TestKey<int, MapMode::PARALLEL_INSERT_READ, true>();

	TestKey<std::string, MapMode::PARALLEL_INSERT_TAKE, false>(); // Fails verification
	TestKey<std::string, MapMode::PARALLEL_INSERT_READ, true>();
	TestKey<std::string, MapMode::PARALLEL_INSERT_READ_GROW_FROM_HEAP, true>();

	TestKey<Big, MapMode::PARALLEL_INSERT_TAKE, false>(); // Fails verification
	TestKey<Big, MapMode::PARALLEL_INSERT_READ, true>();

	TestKey<int[2], MapMode::PARALLEL_INSERT_TAKE, false>(); // Fails verification
	TestKey<int[2], MapMode::PARALLEL_INSERT_READ, false>(); // Fails verification

	{ // Heap allocate, with max of 111 elements, default bucket size
		Hash<TT, int> map(111);
		constexpr auto isAlwaysLockFree = Hash<TT, int>::IsAlwaysLockFree();
		const bool isLockFree = map.IsLockFree();

		TestHash(map);
	}

	Hash<TT, int> map(111);
	map.Add({1, 2, 3}, 1);

	someTests();
	return 0;
}

bool operator==(const TT& o, const TT& t)
{
	return o.a == t.a && o.b == t.b && o.c == t.c;
}

template <>
uint32_t hash(const TT& k, const uint32_t seed)
{
	const TT* k_ = &k;
	const uint64_t* val = (uint64_t*)k_;
	std::cout << " hashing " << k.toString() << " hash: " << hash(*val, seed) << " seed:" << seed << std::endl;
	//_Test<1, std::integral_constant, int> a;
	return hash(*val, seed);
}

template <>
uint32_t hash(const std::string& s, const uint32_t seed)
{
#define FNV_PRIME_32 16777619
#define FNV_OFFSET_BASIS_32 2166136261

	uint32_t fnv = FNV_OFFSET_BASIS_32;
	for (uint32_t i = 0; i < s.size(); ++i)
	{
		fnv = fnv ^ (s[i]);
		fnv = fnv * FNV_PRIME_32;
	}
	return hash(fnv, seed);
}

template <typename Hash>
void TestHash(Hash& a)
{
	TT t1{1, 2, 3};
	TT t2{3, 1, 2};
	TT t3{1, 3, 2};
	TT t4{2, 1, 3};
	a.Add(t1, 1);
	a.Add(t2, 2);
	a.Add(t3, 3);
	a.Add(t3, 777);
	a.Add(t3, 4);
	a.Add(t4, 5);

	HashIterator iter(a);
	// TRACE << typeid(KeyIterator).name() << std::endl;
	iter.SetKey(t3);
	while (iter.Next())
	{
		std::cout << "Hello, " << iter.Value() << std::endl;
	}

	int t3_ = a.Take(t3);
	auto f = [](const int& obj) {
		std::cout << "Hello, " << obj << std::endl;
		return true;
	};
	a.Take(t3, f);
	t3_ = t3_;
}

template <typename Hash>
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
	static Hash<int, int, StaticAllocator<300000, 32>> test;
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

void someTests()
{
	Hash<int, int> a(Hash<int, int>(1));
	{ // Heap allocate, with max of 111 elements, default bucket size
		Hash<TT, int> map(111);
		constexpr auto isAlwaysLockFree = Hash<TT, int>::IsAlwaysLockFree();
		const bool isLockFree = map.IsLockFree();

		TestHash(map);
	}
	{ // Use externally provided memory, with max of 12 elements, bucket size of 11
		constexpr auto elems = 12;
		typedef Hash<TT, int, ExternalAllocator<11>> SHash;
		Container<SHash::Bucket, ALLOCATION_TYPE_STATIC::value, ComputeHashKeyCount(elems)> bucket;
		SHash::KeyValue keys[elems]{};

		typedef typename std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_TAKE> MODE;

		typedef typename std::conditional<std::is_same<MODE, MODE_INSERT_TAKE>::value, // Check the operation mode of
		                                                                               // the map
		                                  KeyValueInsertTake<TT, int>, // If requirements are met
		                                  KeyValueLinkedList<TT, int> // If requirements are not met
		                                  >::type // Extract type selected by std::conditional (i.e. MODE_INSERT_TAKE or
		                                          // MODE_INSERT_TAKE>
		    KeyValueTest; // Extract actual type from selected mode

		KeyValueTest keys_[elems]{};
		constexpr auto same = std::is_same<SHash::KeyValue, KeyValueTest>::value;
		std::atomic<SHash::KeyValue*> keyRecycle[elems];
		{
			SHash map;
			map.Init(uint32_t(elems), &bucket[0], &keys[0], &keyRecycle[0]);
			TestHash(map);
		}
		{
			SHash map;
			map.Init(uint32_t(elems), &bucket[0], &keys[0], &keyRecycle[0]);
			TestHash(map);
		}
	}
	{ // Allocate statically, 111 max elements, default bucket size
		Hash<TT, int, StaticAllocator<111>> map;
		TestHash(map);
	}

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
		// constexpr auto heap = Hash<int, int>::NeededHeap(912) / 1024.0;
		Chrono(test);
	}
	{
		Hash<int, int, StaticAllocator<100, 8>> t;
		constexpr auto t_ = sizeof(t);
		Hash<int, std::string, StaticAllocator<1000, 8>> v;
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
		Hash<TT, int> a(100);
		TestHash(a);
	}

	{
		Hash<TT, int, StaticAllocator<100, 8>> a;
		TestHash(a);
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
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files
//   to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
