// WineBridge: what MSVC's static runtime gives a DLL and ucrtbase doesn't (operator new and delete on its heap), and the one libc++
// function Wine's libc++.a lacks (the hash tables' sizes)
#include <stdlib.h>
#include <stddef.h>
#include <new>

void *operator new (size_t n) { return malloc (n ? n : 1); }
void *operator new[] (size_t n) { return malloc (n ? n : 1); }
void operator delete (void *p) noexcept { free (p); }
void operator delete[] (void *p) noexcept { free (p); }
void operator delete (void *p, size_t) noexcept { free (p); }
void operator delete[] (void *p, size_t) noexcept { free (p); }

namespace std { inline namespace __1 {
	// the smallest prime not below n, as libc++'s hash.cpp gives it
	size_t __next_prime (size_t n)
	{
		if (n <= 2) return 2;
		if (n > 4294967291u) abort (); // above the largest 32-bit prime: libc++'s overflow_error, an abort without exceptions
		for (size_t p = n | 1;; p += 2) {
			bool prime = true;
			for (size_t d = 3; d <= p / d; d += 2) if (p % d == 0) { prime = false; break; }
			if (prime) return p;
		}
	}
} }
