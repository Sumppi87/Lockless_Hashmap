#pragma once
#include <mutex>
#include <atomic>
#include <iostream>

enum class DebugLevel
{
	TRACE,
	DEBUG,
	ERROR,
	LEVELS = ERROR + 1
};

const char* __LEVELS[(unsigned int)DebugLevel::LEVELS]{ "TRACE: ", "DEBUG: ", "ERROR: " };
constexpr static bool ___ENABLED[(unsigned int)DebugLevel::LEVELS]
{
	false,
	false,
	false
};

static std::mutex __debug_lock;

class Debug
{
public:
	Debug(const DebugLevel level)
		: m_level(level) {}

	template<typename T>
	inline Debug& operator<<(const T& t)
	{
		if (___ENABLED[(unsigned int)m_level])
		{
			std::lock_guard guard(__debug_lock);
			std::cout << __LEVELS[(unsigned int)m_level] << t;
		}
		return *this;
	}

	// this is the type of std::cout
	typedef std::basic_ostream<char, std::char_traits<char> > CoutType;

	// this is the function signature of std::endl
	typedef CoutType& (*StandardEndLine)(CoutType&);

	// define an operator<< to take in std::endl
	inline Debug& operator<<(const StandardEndLine manip)
	{
		if (___ENABLED[(unsigned int)m_level])
		{
			std::lock_guard guard(__debug_lock);
			// call the function, but we cannot return it's value
			manip(std::cout);
		}
		return *this;
	}

private:
	const DebugLevel m_level;
};
static Debug __debug[(unsigned int)DebugLevel::LEVELS]{ DebugLevel::TRACE, DebugLevel::DEBUG, DebugLevel::ERROR };

#define TRACE(x) __debug[(unsigned int)DebugLevel::TRACE] << x;
#define TRACE __debug[(unsigned int)DebugLevel::TRACE]
#define DEBUG(x) __debug[(unsigned int)DebugLevel::DEBUG] << x;
#define DEBUG __debug[(unsigned int)DebugLevel::DEBUG]
#define ERROR(x) __debug[(unsigned int)DebugLevel::ERROR] << x;
#define ERROR __debug[(unsigned int)DebugLevel::ERROR]

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
			ERROR << "Concurrent access detected where it is not allowed: " << _file << ":" << _line << std::endl;
			abort();
		}
	}

	inline ~ConcurrencyChecker() noexcept
	{
		if (--_counter != 0)
		{
			// Concurrent access detected, where it's not allowed
			ERROR << "Concurrent access detected where it is not allowed: " << _file << ":" << _line << std::endl;
			abort();
		}
	}

private:
	std::atomic<size_t>& _counter;
	const char* _file;
	const int _line;
};

#define CHECK_CONCURRENT_ACCESS(atomic_counter) ConcurrencyChecker ____checker(atomic_counter, __FILE__, __LINE__);
#else
#define CHECK_CONCURRENT_ACCESS(atomic_counter)
#endif //  _DEBUG
