#pragma once
#include <mutex>
#include <atomic>
#include <iostream>

#define ENABLE_DEBUG_PRINTS 1

#if ENABLE_DEBUG_PRINTS

enum class DebugLevel
{
	TRACE,
	DEBUG,
	ERROR,
	LEVELS = ERROR + 1
};

const char* __LEVELS[(unsigned int)DebugLevel::LEVELS]{"TRACE: ", "DEBUG: ", "ERROR: "};
constexpr static bool ___ENABLED[(unsigned int)DebugLevel::LEVELS]{false, false, false};

constexpr static bool IsDebugEnabled(const DebugLevel lvl)
{
	return ___ENABLED[(unsigned int)lvl];
}

static std::mutex __debug_lock;

template <DebugLevel level>
class Debug
{
public:
	inline Debug() noexcept
	{
	}

	template <typename... Args>
	inline void Print(Args&&... args) noexcept
	{
		std::lock_guard guard(__debug_lock);
		std::cout << __LEVELS[(unsigned int)level];
		_Print(std::forward<Args>(args)...);
		std::cout << std::endl;
	}

private:
	template <typename Arg>
	inline void _Print(Arg&& arg) noexcept
	{
		std::cout << std::forward<Arg&&>(arg);
	}

	template <typename Arg, typename... Args>
	inline void _Print(Arg&& arg, Args&&... args) noexcept
	{
		_Print(std::forward<Arg>(arg));
		_Print(std::forward<Args>(args)...);
	}

	DISABLE_COPY_MOVE(Debug)
};

static Debug<DebugLevel::TRACE> __trace;
static Debug<DebugLevel::DEBUG> __debug;
static Debug<DebugLevel::ERROR> __error;

#define TRACE(...) \
	if constexpr (IsDebugEnabled(DebugLevel::TRACE)) \
	{ \
		__trace.Print(__VA_ARGS__); \
	}
#define DEBUG(...) \
	if constexpr (IsDebugEnabled(DebugLevel::DEBUG)) \
	{ \
		__debug.Print(__VA_ARGS__); \
	}
#define ERROR(...) \
	if constexpr (IsDebugEnabled(DebugLevel::ERROR)) \
	{ \
		__error.Print(__VA_ARGS__); \
	}

#else
#define TRACE(...)
#define DEBUG(...)
#define ERROR(...)
#endif //  DISABLE_DEBUG_PRINTS

#if defined(_DEBUG) || defined(VALIDATE_ITERATOR_NON_CONCURRENT_ACCESS)

struct ConcurrencyChecker
{
	inline ConcurrencyChecker(std::atomic<size_t>& counter, const char* file, const int line) noexcept
	    : _counter(counter)
	    , _file(file)
	    , _line(line)
	{
		if (++_counter != 1)
		{
			// Concurrent access detected, where it's not allowed
			ERROR("Concurrent access detected where it is not allowed: ", _file, ":", _line);
			abort();
		}
	}

	inline ~ConcurrencyChecker() noexcept
	{
		if (--_counter != 0)
		{
			// Concurrent access detected, where it's not allowed
			ERROR("Concurrent access detected where it is not allowed: ", _file, ":", _line);
			abort();
		}
	}

private:
	std::atomic<size_t>& _counter;
	const char* _file;
	const int _line;

	DISABLE_COPY_MOVE(ConcurrencyChecker)
};

#define CHECK_CONCURRENT_ACCESS(atomic_counter) ConcurrencyChecker ____checker(atomic_counter, __FILE__, __LINE__);
#else
#define CHECK_CONCURRENT_ACCESS(atomic_counter)
#endif //  _DEBUG || VALIDATE_ITERATOR_NON_CONCURRENT_ACCESS
