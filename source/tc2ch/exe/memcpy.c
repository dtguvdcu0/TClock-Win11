#include <stddef.h>

#if defined(_MSC_VER)
#pragma function(memcpy)
#pragma function(memmove)
#pragma function(memset)
#pragma optimize("", off)
#endif

void* memmove(void* dst, const void* src, size_t count)
{
	unsigned char* d = (unsigned char*)dst;
	const unsigned char* s = (const unsigned char*)src;

	if (d == s || count == 0) {
		return dst;
	}

	if (d < s || d >= (s + count)) {
		while (count--) {
			*d++ = *s++;
		}
	}
	else {
		d += count;
		s += count;
		while (count--) {
			*--d = *--s;
		}
	}

	return dst;
}

void* memcpy(void* dst, const void* src, size_t count)
{
	unsigned char* d = (unsigned char*)dst;
	const unsigned char* s = (const unsigned char*)src;
	while (count--) {
		*d++ = *s++;
	}
	return dst;
}

void* memset(void* dst, int c, size_t count)
{
	size_t i;
	unsigned char* d = (unsigned char*)dst;
	unsigned char b = (unsigned char)c;

	for (i = 0; i < count; ++i) {
		d[i] = b;
	}
	return dst;
}

#if defined(_MSC_VER)
#pragma optimize("", on)
#endif
