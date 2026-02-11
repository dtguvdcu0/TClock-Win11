#include <stddef.h>

#if defined(_MSC_VER)
#pragma function(memcpy)
#pragma function(memset)
#endif

void* memcpy(void* dst, const void* src, size_t count)
{
	size_t i;
	unsigned char* d = (unsigned char*)dst;
	const unsigned char* s = (const unsigned char*)src;

	for (i = 0; i < count; ++i) {
		d[i] = s[i];
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
