#ifndef TC_INPUT_BINDING_H
#define TC_INPUT_BINDING_H

#include <string.h>
#include <limits.h>

/* Host-side serialized input contract; existing plugin /1 layouts are unchanged. */
#define IB_KEY_CHARS 68
#define IB_RECORD_CHARS 71
#define IB_BUILTIN 0
#define IB_DLL 1
#define IB_OPAQUE 2

typedef struct {
    int kind;
    int number;
    char key[IB_KEY_CHARS];
} IB_ACTION;

static __inline int ib_check_id(const char* text, size_t length)
{
    size_t i;
    if (!length || length > 31) return 0;
    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static __inline void ib_parse_action(const char* text, IB_ACTION* action)
{
    size_t length;
    const char *p, *slash;
    unsigned long magnitude = 0, limit;
    int negative = 0;
    memset(action, 0, sizeof(*action));
    action->kind = IB_OPAQUE;
    if (!text || !*text) return;
    length = strlen(text);
    if (length >= 7 && !memcmp(text, "-1|dll:", 7)) {
        if (length >= IB_RECORD_CHARS) return;
        slash = strchr(text + 7, '/');
        if (!slash || !ib_check_id(text + 7, (size_t)(slash - text - 7)) || !ib_check_id(slash + 1, strlen(slash + 1))) return;
        memcpy(action->key, text + 3, length - 3 + 1);
        action->kind = IB_DLL;
        return;
    }
    p = text;
    if (*p == '-' || *p == '+') negative = *p++ == '-';
    if (!*p) return;
    limit = negative ? (unsigned long)INT_MAX + 1UL : (unsigned long)INT_MAX;
    for (; *p; ++p) {
        unsigned digit;
        if (*p < '0' || *p > '9') return;
        digit = (unsigned)(*p - '0');
        if (magnitude > (limit - digit) / 10) return;
        magnitude = magnitude * 10 + digit;
    }
    action->kind = IB_BUILTIN;
    action->number = negative ? (magnitude == (unsigned long)INT_MAX + 1UL ? INT_MIN : -(int)magnitude) : (int)magnitude;
}

#endif
