#ifndef TC_FORMAT_STYLE_H
#define TC_FORMAT_STYLE_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TC_FS_MAX_RULES 32
#define TC_FS_MAX_NODES 64
#define TC_FS_MAX_BRANCHES 8
#define TC_FS_MAX_TARGETS 16
#define TC_FS_MAX_STRING_CCH 512
#define TC_FS_SELECTOR_CCH 32
#define TC_FS_RULE_CCH 1024
#define TC_FS_DIAG_CCH 256
#define TC_FORMAT_MAX_SPANS 256
#define TC_FS_MAX_ITEMS 512
#define TC_FS_TEXT_CCH 4096

#define TC_FS_PROP_VALUE            0x00000200u

#define TC_FS_PROP_FORE_COLOR       0x00000001u
#define TC_FS_PROP_FONT             0x00000002u
#define TC_FS_PROP_FONT_SIZE        0x00000004u
#define TC_FS_PROP_BOLD             0x00000008u
#define TC_FS_PROP_ITALIC           0x00000010u
#define TC_FS_PROP_SHADOW           0x00000020u
#define TC_FS_PROP_BORDER           0x00000040u
#define TC_FS_PROP_SHADOW_COLOR     0x00000080u
#define TC_FS_PROP_SHADOW_RANGE     0x00000100u

typedef enum TC_FS_TRUTH {
	TC_FS_FALSE = 0,
	TC_FS_TRUE = 1,
	TC_FS_ERROR = 2
} TC_FS_TRUTH;

typedef enum TC_FS_COMPARE {
	TC_FS_CMP_EQ = 0,
	TC_FS_CMP_NE,
	TC_FS_CMP_LT,
	TC_FS_CMP_LE,
	TC_FS_CMP_GT,
	TC_FS_CMP_GE
} TC_FS_COMPARE;

typedef enum TC_FS_NODE_TYPE {
	TC_FS_NODE_FALSE = 0,
	TC_FS_NODE_TRUE,
	TC_FS_NODE_NOT,
	TC_FS_NODE_AND,
	TC_FS_NODE_OR,
	TC_FS_NODE_NUMERIC,
	TC_FS_NODE_TEXT
} TC_FS_NODE_TYPE;

typedef struct TC_FS_STYLE {
	DWORD setMask;
	WORD valueStart;
	WORD valueLength;
	COLORREF foreColor;
	COLORREF shadowColor;
	WCHAR fontFace[LF_FACESIZE];
	int fontSize;
	int shadowRange;
	BYTE bold;
	BYTE italic;
	BYTE shadow;
	BYTE border;
} TC_FS_STYLE;

typedef struct TC_FS_NODE {
	BYTE type;
	BYTE compare;
	short left;
	short right;
	double number;
	WORD selectorRef; /* String-pool offset plus one; zero means the anchor. */
	WORD textStart;
	WORD textLength;
} TC_FS_NODE;

typedef struct TC_FS_TARGET {
	WCHAR selector[TC_FS_SELECTOR_CCH];
	TC_FS_STYLE declaration;
} TC_FS_TARGET;

typedef struct TC_FS_BRANCH {
	int rootNode;
	int targetStart;
	int targetCount;
	TC_FS_STYLE declaration;
} TC_FS_BRANCH;

typedef struct TC_FS_RULE {
	int order;
	WCHAR selector[TC_FS_SELECTOR_CCH];
	TC_FS_NODE nodes[TC_FS_MAX_NODES];
	int nodeCount;
	TC_FS_BRANCH branches[TC_FS_MAX_BRANCHES];
	int branchCount;
	TC_FS_TARGET targets[TC_FS_MAX_TARGETS];
	int targetCount;
	WCHAR stringPool[TC_FS_MAX_STRING_CCH];
	int stringUsed;
} TC_FS_RULE;

typedef struct TC_FS_RULESET {
	BOOL enabled;
	int ruleCount;
	int invalidCount;
	TC_FS_RULE rules[TC_FS_MAX_RULES];
} TC_FS_RULESET;

typedef struct TC_FS_VALUE_CACHE {
	int numericState;
	double numericValue;
} TC_FS_VALUE_CACHE;

typedef struct TC_FS_REPORT {
	int errors;
	int warnings;
	int ruleOrder;
	int charOffset;
	WCHAR message[TC_FS_DIAG_CCH];
} TC_FS_REPORT;

typedef struct TC_FORMAT_SPAN {
	int start;
	int length;
	int occurrence;
	BYTE zone;
	TC_FS_STYLE style;
	WCHAR selector[TC_FS_SELECTOR_CCH];
} TC_FORMAT_SPAN;

typedef struct TC_FORMAT_SPANS {
	int count;
	BOOL overflow;
	BOOL resolved;
	TC_FORMAT_SPAN items[TC_FORMAT_MAX_SPANS];
} TC_FORMAT_SPANS;

void TcFormatStyleInit(TC_FS_RULESET* ruleset);
BOOL TcFormatStyleIdentifier(const WCHAR* name);
BOOL TcFormatStyleSelectorSupported(const WCHAR* selector);
BOOL TcFormatStyleParseRule(const WCHAR* text, int order, TC_FS_RULE* rule, TC_FS_REPORT* report);
BOOL TcFormatStyleLoadMulti(const WCHAR* sectionMulti, int sectionCch,
	TC_FS_RULESET* ruleset, TC_FS_REPORT* report);
int TcFormatStyleApply(const TC_FS_RULESET* ruleset, const WCHAR* selector,
	const WCHAR* value, int valueLength, const TC_FS_STYLE* baseStyle,
	TC_FS_STYLE* resolvedStyle, TC_FS_VALUE_CACHE* cache);
/* Frame overlays are partial styles; callers retain per-zone base properties. */
BOOL TcFormatStyleResolveFrame(const TC_FS_RULESET* ruleset, const WCHAR* text,
	int textLength, const TC_FORMAT_SPANS* spans, TC_FS_STYLE* overlays, int capacity);
BOOL TcFormatStyleTransform(const TC_FS_RULESET* ruleset, WCHAR* text, int capacity,
	char* info, TC_FORMAT_SPANS* spans);
void TcFormatStyleMerge(TC_FS_STYLE* target, const TC_FS_STYLE* overlay);
int TcFormatStyleGetNewlineLength(const WCHAR* text, int textLength, int position);

#ifdef __cplusplus
}
#endif

#endif
