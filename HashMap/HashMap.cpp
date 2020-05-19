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


template<typename Hash>
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

const uint8_t OUTER_ARR_SIZE = 24;
const uint8_t THREADS = 12;
const uint8_t ITEMS_PER_THREAD = OUTER_ARR_SIZE / THREADS;
static_assert((OUTER_ARR_SIZE % THREADS) == 0);
const size_t TEST_ARRAY_SIZE = 10000;
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
//Hash<int, Rand<16>, HeapAllocator<20>> test2(ITEMS);
//static Hash<int, Rand<16>, StaticAllocator<ITEMS, 20>> test2;
#define TESTED "Lockless hashmap"
#else
#define TESTED "std::unordered_multimap"
std::mutex testlock;
std::unordered_multimap<int, Rand<16>> test;
#endif


static auto ProcessData(const unsigned int from, const unsigned int to, Hash<int, Rand<16>, HeapAllocator<32>>& map)
{
	auto start = std::chrono::steady_clock::now();
	for (auto index = from; index < to; ++index)
	{
		for (size_t item = 0; item < TEST_ARRAY_SIZE; ++item)
		{
#ifdef TEST_HASHMAP
			if (!map.Add(TEST_ARRAY[index][item].key, TEST_ARRAY[index][item].v))
				assert(0);
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

static void ProcessDatas(Hash<int, Rand<16>, HeapAllocator<32>>& map)
{
	auto start = std::chrono::steady_clock::now();

#ifdef RUN_IN_THREADS
	std::vector<std::future<std::chrono::milliseconds>> vec;
	for (auto i = 0; i < THREADS; ++i)
		vec.push_back(std::async(std::launch::async, ProcessData, i * ITEMS_PER_THREAD, i * ITEMS_PER_THREAD + ITEMS_PER_THREAD, std::ref(map)));
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

static bool ValidateData(const unsigned int from, const unsigned int to, Hash<int, Rand<16>, HeapAllocator<32>>& map)
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
			map.Take(tt.key, receiver);
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

static bool ValidateDatas(Hash<int, Rand<16>, HeapAllocator<32>>& map)
{
	bool ret = true;

#if defined(RUN_IN_THREADS) //&& defined(TEST_HASHMAP)
	std::vector<std::future<bool>> vec;
	for (auto i = 0; i < THREADS; ++i)
		vec.push_back(std::async(std::launch::async, ValidateData, i * ITEMS_PER_THREAD, i * ITEMS_PER_THREAD + ITEMS_PER_THREAD, std::ref(map)));

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

template<
	size_t SIZE,
	template<typename T3, T3> class T1
>
struct _Test
{
	//T1<T2, T2> member;
};


template<
	template<typename T3, T3> class T1, typename T, T val = T()
>
struct __Test
{
	typedef typename std::integral_constant<T, val> A;

	typedef std::is_same<A, typename T1<T, val>> IS_SAME;
	constexpr static const bool isSame = IS_SAME::value;
	T1<T, val> a;
};

size_t hash(const std::integral_constant<size_t, 1>& k, const size_t seed)
{
	return hash(k.value, seed);
}

template<typename K, MapMode mode, bool assert = false>
void TestKey()
{
	HashKeyProperties<K, mode> a;
	constexpr bool _a1 = a.VALID_KEY_TYPE;
	//constexpr bool _a3 = a.STD_ATOMIC_ALWAYS_LOCK_FREE;
	//constexpr bool _a4 = a.STD_ATOMIC_AVAILABLE;
	int i = 0;
	i = 0;

	if constexpr (assert)
	{
		KeyPropertyValidator<K, mode> a;
	}
}

template<typename K, typename V, typename _Alloc = HeapAllocator<>, MapMode OP_MODE = DefaultModeSelector<K, _Alloc>::MODE>
//MapMode OP_MODE = MapMode::PARALLEL_INSERT_TAKE>
class MAP :
	public _Alloc,
	public std::conditional<
	IS_INSERT_READ_FROM_HEAP(OP_MODE), // Check the operation mode of the map
	BaseAllocateItemsFromHeap<K, V, _Alloc>, // Inherit if requirements are met
	HashBaseNormal<K, V, _Alloc, IS_INSERT_TAKE(OP_MODE)>  // Inherit if requirements are not met
	>::type
{
public:
	typedef typename std::conditional<
		IS_INSERT_READ_FROM_HEAP(OP_MODE), // Check the operation mode of the map
		BaseAllocateItemsFromHeap<K, V, _Alloc>, // Inherit if requirements are met
		HashBaseNormal<K, V, _Alloc, IS_INSERT_TAKE(OP_MODE)>  // Inherit if requirements are not met
	>::type Base;

	MAP() : _Alloc(0), Base(3), m_hash(1){}

	constexpr static const KeyPropertyValidator<K, OP_MODE> VALIDATOR{};
	typedef typename Base::Bucket Bucket;
	Container<Bucket, _Alloc::ALLOCATOR, _Alloc::KEY_COUNT> m_hash;
};

int main()
{
	someTests();
	{
		KeyValueLinkedList<std::string, int> test;
		KeyValueLinkedList<std::string, int> test1;
		test.pNext = &test1;
		test.k.hash = 1;
		test.k.key = "test";
		test.v = 1;
	}
	{
		MAP<std::string, int> _A;
		MAP<std::string, int, HeapAllocator<0>, MapMode::PARALLEL_INSERT_READ_GROW_FROM_HEAP> _B;
		constexpr auto is_same = IS_INSERT_READ_FROM_HEAP(MODE_INSERT_TAKE::value);
		constexpr auto is_same2 = IS_INSERT_READ_FROM_HEAP(MODE_INSERT_READ::value);
		constexpr auto is_same3 = IS_INSERT_READ_FROM_HEAP(MODE_INSERT_READ_HEAP_BUCKET::value);
		TEST<int, HeapAllocator<0>> a;
		a.MODE;
		constexpr auto test = TEST<std::string, HeapAllocator<>>::ZERO_SIZE_BUCKET::value;
		constexpr auto test_ = TEST<std::string, HeapAllocator<>>::MODE;
		constexpr auto test2 = TEST<std::string, HeapAllocator<0>>::ZERO_SIZE_BUCKET::value;
		constexpr auto test2_ = TEST<std::string, HeapAllocator<0>>::MODE;

		constexpr auto test3 = TEST<std::string, StaticAllocator<13>>::ZERO_SIZE_BUCKET::value;
		constexpr auto test3_ = TEST<std::string, StaticAllocator<13>>::MODE;
		constexpr auto test4 = TEST<std::string, StaticAllocator<13, 0>>::ZERO_SIZE_BUCKET::value;
		constexpr auto test4_ = TEST<std::string, StaticAllocator<13, 0>>::MODE;

		constexpr auto test5 = TEST<std::string, ExternalAllocator<>>::ZERO_SIZE_BUCKET::value;
		constexpr auto test5_ = TEST<std::string, ExternalAllocator<>>::MODE;
		constexpr auto test6 = TEST<std::string, ExternalAllocator<0>>::ZERO_SIZE_BUCKET::value;
		constexpr auto test6_ = TEST<std::string, ExternalAllocator<0>>::MODE;
	}
	/*auto iters = 1000;
	for (auto i = 0; i < iters; ++i)
	{
		Hash<int, Rand<16>, HeapAllocator<32>> map(ITEMS);
		ProcessDatas(map);

		auto start = std::chrono::steady_clock::now();
		bool ret = ValidateDatas(map);
		std::cout << "Validation result " << (ret ? "OK" : "ERROR") << std::endl;
		auto end = std::chrono::steady_clock::now() - start;
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);
		std::cout << "Validation for " << TESTED << " took " << duration.count() << std::endl;
	}*/
	Hash<std::string, int> str_(100);
	std::string s("1");
	KeyIterator iterRead(str_);
	iterRead.SetKey(s);
	iterRead.Next();

	int* p = nullptr;
	int& ref = *(int*)nullptr;

	TT tt_{ 1,2,3 };
	Hash<TT, int> TT_(100);
	TT_.Add(tt_, 1);
	KeyIterator iterTake(TT_);
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
		//std::string _c;
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

	//TestKey<std::bool_constant<false>, MapMode::PARALLEL_INSERT_TAKE, true>();
	//TestKey<std::bool_constant<false>, MapMode::PARALLEL_INSERT_READ, true>();

	{
		//Hash<std::bool_constant<false>, int, HeapAllocator<0>, MapMode::PARALLEL_INSERT_READ> __test(1);
		MultiHash_H<int[2], int> _test_(1);
		//Hash<Big, int, HeapAllocator<>, MapMode::PARALLEL_INSERT_TAKE_ALLOW_LOCKING> _test(1);
		int a[2]{};
		//_test_.Add(a, 1);

		//__test.Add(std::integral_constant<size_t, 1>(), 3);
		//auto val = __test.Take(std::integral_constant<size_t, 1>());
		//val = val;
	}
	/*HashTraits<std::integral_constant<size_t, 1>, int, HeapAllocator<>>::USE_STD_ATOMIC;


	HashTraits<int, std::string, HeapAllocator<>, true> __a;
	__a.ATOMICS_IN_USE;
	__a.USE_STD_ATOMIC;

	KeyProperties<std::string>::STD_ATOMIC_USABLE::value;
	KeyProperties<int>::USE_STD_ATOMIC;*/

	__Test<std::integral_constant, int, 1> a;
	a.a;
	a.isSame;
	//_Test<1, std::integral_constant, int> a;
#ifndef TEST_HASHMAP
	test.reserve(ITEMS);
#else
	//const bool isLockFree = test2.IsLockFree();
#endif // !TEST_HASHMAP

#ifndef _DEBUG
	/*ProcessDatas();

	auto start = std::chrono::steady_clock::now();
	bool ret = ValidateDatas();
	std::cout << "Validation result " << (ret ? "OK" : "ERROR") << std::endl;
	auto end = std::chrono::steady_clock::now() - start;
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end);
	std::cout << "Validation for " << TESTED << " took " << duration.count() << std::endl;*/
#endif

	{// Heap allocate, with max of 111 elements, default bucket size
		Hash<TT, int> map(111);
		constexpr auto isAlwaysLockFree = Hash<TT, int>::IsAlwaysLockFree();
		const bool isLockFree = map.IsLockFree();

		// Function enabled only if not EXTERNAL
		map.Test();

		TestHash(map);
	}

	Hash<TT, int> map(111);
	map.Add({ 1,2,3 }, 1);

	someTests();
	return 0;
}

bool operator==(const TT& o, const TT& t)
{
	return o.a == t.a && o.b == t.b && o.c == t.c;
}

template <>
size_t hash(const TT& k, const size_t seed)
{
	const TT* k_ = &k;
	const uint64_t* val = (uint64_t*)k_;
	//std::cout << " hashing " << k.toString() << " hash: " << hash(*val, seed) << " seed:" << seed << std::endl;
	hash(*val, seed);
	/*union Conv
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
	std::cout << " hashing " << k.toString() << " hash: "<< hash(Conv(k).v, seed) << " seed:" << seed << std::endl;
	return hash(Conv(k).v, seed);
	return hash(size_t(size_t(k.a << 16) ^ (size_t(k.b) << 24) ^ (size_t(k.c) << 8)), 0);
	return hash(size_t((k.b << (k.c^k.b)) ^ k.a), seed);*/
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

	KeyIterator iter(a);
	iter.SetKey(t3);
	while (iter.Next())
	{
		std::cout << "Hello, " << iter.Value() << std::endl;
	}

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
	{// Heap allocate, with max of 111 elements, default bucket size
		Hash<TT, int> map(111);
		constexpr auto isAlwaysLockFree = Hash<TT, int>::IsAlwaysLockFree();
		const bool isLockFree = map.IsLockFree();

		// Function enabled only if not EXTERNAL
		map.Test();

		TestHash(map);
	}
	{//Use externally provided memory, with max of 12 elements, bucket size of 11
		constexpr auto elems = 12;
		typedef Hash<TT, int, ExternalAllocator<11>> SHash;
		Container<SHash::Bucket, ALLOCATION_TYPE_STATIC::value, ComputeHashKeyCount(elems)> bucket;
		SHash::KeyValue keys[elems]{};

		typedef typename std::integral_constant<MapMode, MapMode::PARALLEL_INSERT_TAKE> MODE;

		typedef typename std::conditional<
			std::is_same<MODE, MODE_INSERT_TAKE>::value, // Check the operation mode of the map
			KeyValueInsertTake<TT, int>, // If requirements are met
			KeyValueLinkedList<TT, int>  // If requirements are not met
		>::type // Extract type selected by std::conditional (i.e. MODE_INSERT_TAKE or MODE_INSERT_TAKE>
			KeyValueTest; // Extract actual type from selected mode

		KeyValueTest keys_[elems]{};
		constexpr auto same = std::is_same< SHash::KeyValue, KeyValueTest>::value;
		std::atomic<SHash::KeyValue*> keyRecycle[elems];
		{
			SHash map;
			map.Init(size_t(elems), &bucket[0], &keys[0], &keyRecycle[0]);
			TestHash(map);

			// Function enabled only if not EXTERNAL
			// Doesn't compile
			//
			// map.Test();
		}
		{
			SHash map;
			map.Init(size_t(elems), &bucket[0], &keys[0], &keyRecycle[0]);
			TestHash(map);

			// Function enabled only if not EXTERNAL
			// Doesn't compile
			//
			// map.Test();
		}
	}
	{ // Allocate statically, 111 max elements, default bucket size
		Hash<TT, int, StaticAllocator<111>> map;
		TestHash(map);

		// Function enabled only if not EXTERNAL
		map.Test();
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
		//constexpr auto heap = Hash<int, int>::NeededHeap(912) / 1024.0;
		Chrono(test);
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
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
