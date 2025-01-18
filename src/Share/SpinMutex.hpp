#pragma once
#include <atomic>
#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

class SpinMutex
{
private:
	std::atomic<bool> flag = { false };

public:
	void lock()
	{
		for (;;)
		{
			if (!flag.exchange(true, std::memory_order_acquire))
				break;

			while (flag.load(std::memory_order_relaxed))
			{
#ifdef _MSC_VER
				_mm_pause();
#else
#if defined(__APPLE__) || defined(__MACH__)
                __asm__ __volatile__("yield" : : : "memory");
#else
				__builtin_ia32_pause();
#endif
#endif
			}
		}
	}

	void unlock()
	{
		flag.store(false, std::memory_order_release);
	}
};

class SpinLock
{
public:
	SpinLock(SpinMutex& mtx) :_mutex(mtx) { _mutex.lock(); }
	SpinLock(const SpinLock&) = delete;
	SpinLock& operator=(const SpinLock&) = delete;
	~SpinLock() { _mutex.unlock(); }

private:
	SpinMutex&	_mutex;
};
