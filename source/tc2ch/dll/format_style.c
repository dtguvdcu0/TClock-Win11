#include "format_style.h"
#include "../common/color_value.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

int TcFormatStyleGetNewlineLength(const WCHAR* text, int textLength, int position)
{
	if (!text || position < 0 || position >= textLength) return 0;
	if (text[position] == L'\r') {
		return position + 1 < textLength && text[position + 1] == L'\n' ? 2 : 1;
	}
	return text[position] == L'\n' ? 1 : 0;
}

typedef struct TC_FS_PARSER {
	const WCHAR* start;
	const WCHAR* pos;
	TC_FS_RULE* rule;
	TC_FS_REPORT* report;
	int depth;
} TC_FS_PARSER;

static void fs_set_report(TC_FS_REPORT* report, int order, int offset, const WCHAR* message)
{
	if (!report) return;
	if (report->errors == 0 && report->warnings == 0) {
		report->ruleOrder = order;
		report->charOffset = offset;
		lstrcpynW(report->message, message ? message : L"FormatStyle error", TC_FS_DIAG_CCH);
	}
	report->errors++;
}

static void fs_skip_space(TC_FS_PARSER* parser)
{
	while (*parser->pos && iswspace(*parser->pos)) parser->pos++;
}

static BOOL fs_is_ident_start(WCHAR ch)
{
	return ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || ch == L'_');
}

static BOOL fs_is_ident_char(WCHAR ch)
{
	return fs_is_ident_start(ch) || (ch >= L'0' && ch <= L'9');
}

BOOL TcFormatStyleIdentifier(const WCHAR* name)
{
	int i;
	if (!name || !fs_is_ident_start(name[0])) return FALSE;
	for (i = 1; name[i]; i++) {
		if (i >= TC_FS_SELECTOR_CCH - 1 || !fs_is_ident_char(name[i])) return FALSE;
	}
	return TRUE;
}

static BOOL fs_match_char(TC_FS_PARSER* parser, WCHAR ch)
{
	fs_skip_space(parser);
	if (*parser->pos != ch) return FALSE;
	parser->pos++;
	return TRUE;
}

static BOOL fs_match_text(TC_FS_PARSER* parser, const WCHAR* text)
{
	int len;
	fs_skip_space(parser);
	len = lstrlenW(text);
	if (wcsncmp(parser->pos, text, (size_t)len) != 0) return FALSE;
	parser->pos += len;
	return TRUE;
}

static BOOL fs_match_word(TC_FS_PARSER* parser, const WCHAR* word)
{
	const WCHAR* save;
	int len;
	fs_skip_space(parser);
	save = parser->pos;
	len = lstrlenW(word);
	if (wcsncmp(save, word, (size_t)len) != 0 || fs_is_ident_char(save[len])) return FALSE;
	parser->pos += len;
	return TRUE;
}

static BOOL fs_read_ident(TC_FS_PARSER* parser, WCHAR* out, int outCch)
{
	const WCHAR* start;
	int len;
	fs_skip_space(parser);
	if (!fs_is_ident_start(*parser->pos)) return FALSE;
	start = parser->pos++;
	while (fs_is_ident_char(*parser->pos)) parser->pos++;
	len = (int)(parser->pos - start);
	if (len >= outCch) return FALSE;
	CopyMemory(out, start, (SIZE_T)len * sizeof(WCHAR));
	out[len] = L'\0';
	return TRUE;
}

static BOOL fs_parse_decimal(const WCHAR* text, int length, double* outValue, int* outUsed, BOOL trimAll)
{
	int i = 0;
	int sign = 1;
	int digits = 0;
	int exponent = 0;
	int exponentSign = 1;
	int exponentDigits = 0;
	double value = 0.0;
	double scale = 1.0;

	if (!text || length < 0 || !outValue) return FALSE;
	if (trimAll) {
		while (i < length && iswspace(text[i])) i++;
		while (length > i && iswspace(text[length - 1])) length--;
	}
	if (i < length && (text[i] == L'+' || text[i] == L'-')) {
		if (text[i++] == L'-') sign = -1;
	}
	while (i < length && text[i] >= L'0' && text[i] <= L'9') {
		value = (value * 10.0) + (double)(text[i++] - L'0');
		digits++;
	}
	if (i < length && text[i] == L'.') {
		i++;
		while (i < length && text[i] >= L'0' && text[i] <= L'9') {
			scale *= 0.1;
			value += (double)(text[i++] - L'0') * scale;
			digits++;
		}
	}
	if (digits == 0) return FALSE;
	if (i < length && (text[i] == L'e' || text[i] == L'E')) {
		i++;
		if (i < length && (text[i] == L'+' || text[i] == L'-')) {
			if (text[i++] == L'-') exponentSign = -1;
		}
		while (i < length && text[i] >= L'0' && text[i] <= L'9') {
			if (exponent < 10000) exponent = (exponent * 10) + (int)(text[i] - L'0');
			i++;
			exponentDigits++;
		}
		if (exponentDigits == 0) return FALSE;
		value *= pow(10.0, (double)(exponentSign * exponent));
	}
	if (trimAll) {
		while (i < length && iswspace(text[i])) i++;
	}
	if (trimAll && i != length) return FALSE;
	if (!_finite(value)) return FALSE;
	*outValue = value * (double)sign;
	if (outUsed) *outUsed = i;
	return TRUE;
}

static BOOL fs_read_number(TC_FS_PARSER* parser, double* outValue)
{
	int used = 0;
	int available;
	fs_skip_space(parser);
	available = lstrlenW(parser->pos);
	if (!fs_parse_decimal(parser->pos, available, outValue, &used, FALSE)) return FALSE;
	parser->pos += used;
	return TRUE;
}

static BOOL fs_read_int(TC_FS_PARSER* parser, int minimum, int maximum, int* outValue)
{
	double number;
	int value;
	if (!fs_read_number(parser, &number)) return FALSE;
	value = (int)number;
	if ((double)value != number || value < minimum || value > maximum) return FALSE;
	*outValue = value;
	return TRUE;
}

static BOOL fs_read_string(TC_FS_PARSER* parser, WCHAR* out, int outCch, int* outLength)
{
	int used = 0;
	fs_skip_space(parser);
	if (*parser->pos != L'\"') return FALSE;
	parser->pos++;
	while (*parser->pos && *parser->pos != L'\"') {
		WCHAR ch = *parser->pos++;
		if (ch == L'\\') {
			ch = *parser->pos++;
			if (ch == L'\\' || ch == L'\"') {
				/* Keep ch. */
			}
			else if (ch == L'n') ch = L'\n';
			else if (ch == L'r') ch = L'\r';
			else if (ch == L't') ch = L'\t';
			else return FALSE;
		}
		if (used + 1 >= outCch) return FALSE;
		out[used++] = ch;
	}
	if (*parser->pos != L'\"') return FALSE;
	parser->pos++;
	out[used] = L'\0';
	if (outLength) *outLength = used;
	return TRUE;
}

static int fs_add_node(TC_FS_PARSER* parser, BYTE type, int left, int right)
{
	TC_FS_NODE* node;
	int index;
	if (parser->rule->nodeCount >= TC_FS_MAX_NODES) return -1;
	index = parser->rule->nodeCount++;
	node = &parser->rule->nodes[index];
	ZeroMemory(node, sizeof(*node));
	node->type = type;
	node->left = (short)left;
	node->right = (short)right;
	return index;
}

static int fs_parse_or(TC_FS_PARSER* parser);

static BOOL fs_parse_compare(TC_FS_PARSER* parser, TC_FS_COMPARE* compare)
{
	fs_skip_space(parser);
	if (wcsncmp(parser->pos, L"==", 2) == 0) *compare = TC_FS_CMP_EQ;
	else if (wcsncmp(parser->pos, L"!=", 2) == 0) *compare = TC_FS_CMP_NE;
	else if (wcsncmp(parser->pos, L"<=", 2) == 0) *compare = TC_FS_CMP_LE;
	else if (wcsncmp(parser->pos, L">=", 2) == 0) *compare = TC_FS_CMP_GE;
	else if (*parser->pos == L'<') { *compare = TC_FS_CMP_LT; parser->pos++; return TRUE; }
	else if (*parser->pos == L'>') { *compare = TC_FS_CMP_GT; parser->pos++; return TRUE; }
	else return FALSE;
	parser->pos += 2;
	return TRUE;
}

static int fs_parse_primary(TC_FS_PARSER* parser)
{
	TC_FS_COMPARE compare;
	int nodeIndex;
	WORD selectorRef = 0;
	WCHAR operand[TC_FS_SELECTOR_CCH];
	fs_skip_space(parser);
	if (fs_match_char(parser, L'(')) {
		int inner;
		if (++parser->depth > 16) return -1;
		inner = fs_parse_or(parser);
		parser->depth--;
		if (inner < 0 || !fs_match_char(parser, L')')) return -1;
		return inner;
	}
	if (!fs_read_ident(parser, operand, TC_FS_SELECTOR_CCH)) return -1;
	fs_skip_space(parser);
	if (wcscmp(operand, L"true") == 0 && *parser->pos != L'.')
		return fs_add_node(parser, TC_FS_NODE_TRUE, -1, -1);
	if (wcscmp(operand, L"false") == 0 && *parser->pos != L'.')
		return fs_add_node(parser, TC_FS_NODE_FALSE, -1, -1);
	if (!TcFormatStyleIdentifier(operand) || !fs_match_char(parser, L'.') ||
		!fs_match_word(parser, L"value")) return -1;
	{
		int length = lstrlenW(operand) + 1;
		if (parser->rule->stringUsed + length > TC_FS_MAX_STRING_CCH) return -1;
		selectorRef = (WORD)(parser->rule->stringUsed + 1);
		CopyMemory(parser->rule->stringPool + parser->rule->stringUsed, operand, (SIZE_T)length * sizeof(WCHAR));
		parser->rule->stringUsed += length;
	}
	if (!fs_parse_compare(parser, &compare)) return -1;
	fs_skip_space(parser);
	if (*parser->pos == L'\"') {
		WCHAR text[TC_FS_MAX_STRING_CCH];
		int length;
		int start;
		if (compare != TC_FS_CMP_EQ && compare != TC_FS_CMP_NE) return -1;
		if (!fs_read_string(parser, text, TC_FS_MAX_STRING_CCH, &length)) return -1;
		if (parser->rule->stringUsed + length + 1 > TC_FS_MAX_STRING_CCH) return -1;
		start = parser->rule->stringUsed;
		CopyMemory(parser->rule->stringPool + start, text, (SIZE_T)(length + 1) * sizeof(WCHAR));
		parser->rule->stringUsed += length + 1;
		nodeIndex = fs_add_node(parser, TC_FS_NODE_TEXT, -1, -1);
		if (nodeIndex < 0) return -1;
		parser->rule->nodes[nodeIndex].compare = (BYTE)compare;
		parser->rule->nodes[nodeIndex].selectorRef = selectorRef;
		parser->rule->nodes[nodeIndex].textStart = (WORD)start;
		parser->rule->nodes[nodeIndex].textLength = (WORD)length;
		return nodeIndex;
	}
	else {
		double number;
		if (!fs_read_number(parser, &number)) return -1;
		nodeIndex = fs_add_node(parser, TC_FS_NODE_NUMERIC, -1, -1);
		if (nodeIndex < 0) return -1;
		parser->rule->nodes[nodeIndex].compare = (BYTE)compare;
		parser->rule->nodes[nodeIndex].selectorRef = selectorRef;
		parser->rule->nodes[nodeIndex].number = number;
		return nodeIndex;
	}
}

static int fs_parse_unary(TC_FS_PARSER* parser)
{
	if (fs_match_char(parser, L'!')) {
		int child = fs_parse_unary(parser);
		if (child < 0) return -1;
		return fs_add_node(parser, TC_FS_NODE_NOT, child, -1);
	}
	return fs_parse_primary(parser);
}

static int fs_parse_and(TC_FS_PARSER* parser)
{
	int left = fs_parse_unary(parser);
	if (left < 0) return -1;
	for (;;) {
		const WCHAR* save = parser->pos;
		int right;
		int parent;
		if (!fs_match_text(parser, L"&&")) {
			parser->pos = save;
			return left;
		}
		right = fs_parse_unary(parser);
		if (right < 0) return -1;
		parent = fs_add_node(parser, TC_FS_NODE_AND, left, right);
		if (parent < 0) return -1;
		left = parent;
	}
}

static int fs_parse_or(TC_FS_PARSER* parser)
{
	int left = fs_parse_and(parser);
	if (left < 0) return -1;
	for (;;) {
		const WCHAR* save = parser->pos;
		int right;
		int parent;
		if (!fs_match_text(parser, L"||")) {
			parser->pos = save;
			return left;
		}
		right = fs_parse_and(parser);
		if (right < 0) return -1;
		parent = fs_add_node(parser, TC_FS_NODE_OR, left, right);
		if (parent < 0) return -1;
		left = parent;
	}
}

static BOOL fs_parse_color(TC_FS_PARSER* parser, COLORREF* color)
{
	const WCHAR* cursor = parser->pos;
	if (!tcv_parse_next(&cursor, color, NULL)) return FALSE;
	parser->pos = cursor;
	return TRUE;
}

static BOOL fs_set_property(TC_FS_PARSER* parser, TC_FS_STYLE* style, const WCHAR* property)
{
	DWORD bit = 0;
	if (_wcsicmp(property, L"value") == 0) {
		WCHAR value[TC_FS_MAX_STRING_CCH];
		int length;
		bit = TC_FS_PROP_VALUE;
		if (!fs_read_string(parser, value, TC_FS_MAX_STRING_CCH, &length)) return FALSE;
		if (parser->rule->stringUsed + length + 1 > TC_FS_MAX_STRING_CCH) return FALSE;
		style->valueStart = (WORD)parser->rule->stringUsed;
		style->valueLength = (WORD)length;
		CopyMemory(parser->rule->stringPool + parser->rule->stringUsed, value, (SIZE_T)(length + 1) * sizeof(WCHAR));
		parser->rule->stringUsed += length + 1;
	}
	else if (_wcsicmp(property, L"ForeColor") == 0) {
		bit = TC_FS_PROP_FORE_COLOR;
		if (!fs_parse_color(parser, &style->foreColor)) return FALSE;
	}
	else if (_wcsicmp(property, L"Font") == 0) {
		int length;
		bit = TC_FS_PROP_FONT;
		if (!fs_read_string(parser, style->fontFace, LF_FACESIZE, &length) || length == 0) return FALSE;
	}
	else if (_wcsicmp(property, L"FontSize") == 0) {
		bit = TC_FS_PROP_FONT_SIZE;
		if (!fs_read_int(parser, 5, 256, &style->fontSize)) return FALSE;
	}
	else if (_wcsicmp(property, L"Bold") == 0) {
		int value;
		bit = TC_FS_PROP_BOLD;
		if (!fs_read_int(parser, 0, 1, &value)) return FALSE;
		style->bold = (BYTE)value;
	}
	else if (_wcsicmp(property, L"Italic") == 0) {
		int value;
		bit = TC_FS_PROP_ITALIC;
		if (!fs_read_int(parser, 0, 1, &value)) return FALSE;
		style->italic = (BYTE)value;
	}
	else if (_wcsicmp(property, L"ForeColorShadow") == 0) {
		int value;
		bit = TC_FS_PROP_SHADOW;
		if (!fs_read_int(parser, 0, 1, &value)) return FALSE;
		style->shadow = (BYTE)value;
	}
	else if (_wcsicmp(property, L"ForeColorBorder") == 0) {
		int value;
		bit = TC_FS_PROP_BORDER;
		if (!fs_read_int(parser, 0, 1, &value)) return FALSE;
		style->border = (BYTE)value;
	}
	else if (_wcsicmp(property, L"ShadowColor") == 0) {
		bit = TC_FS_PROP_SHADOW_COLOR;
		if (!fs_parse_color(parser, &style->shadowColor)) return FALSE;
	}
	else if (_wcsicmp(property, L"ClockShadowRange") == 0) {
		bit = TC_FS_PROP_SHADOW_RANGE;
		if (!fs_read_int(parser, 0, 10, &style->shadowRange)) return FALSE;
	}
	else {
		return FALSE;
	}
	if ((style->setMask & bit) != 0) return FALSE;
	style->setMask |= bit;
	return TRUE;
}

static BOOL fs_parse_declarations(TC_FS_PARSER* parser, TC_FS_BRANCH* branch)
{
	int count = 0;
	branch->targetStart = parser->rule->targetCount;
	if (!fs_match_char(parser, L'{')) return FALSE;
	for (;;) {
		WCHAR selector[TC_FS_SELECTOR_CCH];
		WCHAR property[64];
		TC_FS_STYLE* style;
		int i;
		fs_skip_space(parser);
		if (*parser->pos == L'}') {
			parser->pos++;
			return count > 0;
		}
		if (!fs_read_ident(parser, selector, TC_FS_SELECTOR_CCH) ||
			!TcFormatStyleIdentifier(selector) || !fs_match_char(parser, L'.') ||
			!fs_read_ident(parser, property, (int)ARRAYSIZE(property))) return FALSE;
		for (i = branch->targetStart; i < parser->rule->targetCount; i++) {
			if (wcscmp(selector, parser->rule->targets[i].selector) == 0) break;
		}
		if (i == parser->rule->targetCount) {
			if (i >= TC_FS_MAX_TARGETS) return FALSE;
			lstrcpynW(parser->rule->targets[i].selector, selector, TC_FS_SELECTOR_CCH);
			parser->rule->targetCount++;
			branch->targetCount++;
		}
		style = &parser->rule->targets[i].declaration;
		if (!fs_match_char(parser, L'=') || !fs_set_property(parser, style, property) ||
			!fs_match_char(parser, L';')) return FALSE;
		count++;
	}
}

void TcFormatStyleInit(TC_FS_RULESET* ruleset)
{
	if (ruleset) ZeroMemory(ruleset, sizeof(*ruleset));
}

BOOL TcFormatStyleSelectorSupported(const WCHAR* selector)
{
	static const WCHAR* const selectors[] = {
		L"DATESEP", L"TIMESEP", L"BEAT", L"LDATE", L"DATE", L"TIME",
		L"MK", L"MM", L"MG", L"MS",
		L"MTPK", L"MTPM", L"MTPP", L"MTPG", L"MTFK", L"MTFM", L"MTFP", L"MTFG",
		L"MTVK", L"MTVM", L"MTVP", L"MTVG", L"MAPK", L"MAPM", L"MAPP", L"MAPG",
		L"MAFK", L"MAFM", L"MAFP", L"MAFG", L"MAVK", L"MAVM", L"MAVP", L"MAVG",
		L"MUPK", L"MUPM", L"MUPP", L"MUPG", L"MUFK", L"MUFM", L"MUFP", L"MUFG",
		L"MUVK", L"MUVM", L"MUVP", L"MUVG",
		L"SSID", L"WiFi", L"EthS", L"EthL", L"EWLL", L"EWLS", L"ICP", L"LTE",
		L"VPNS", L"WANP", L"APN", L"NMX1", L"NMX2", L"NRAA", L"NSAA", L"NRSA", L"NSSA",
		L"NRAB", L"NRAK", L"NRAM", L"NRAG", L"NRSB", L"NRSK", L"NRSM",
		L"NSAB", L"NSAK", L"NSAM", L"NSAG", L"NSSB", L"NSSK", L"NSSM",
		L"GIP", L"GIPA", L"GU", L"GI", L"IPA", L"IPE", L"IPW", L"IPL", L"IPV",
		L"ST", L"Sd", L"Sa", L"Sh", L"Sn", L"Ss", L"AD", L"ad", L"BCS", L"BL",
		L"B", L"TEMP", L"VL", L"VM", L"PCORE", L"LPROC", L"StU", L"StE", L"w",
		L"aaaa", L"aaa", L"yyyy", L"yy", L"Y", L"g", L"dddd", L"dde", L"ddd",
		L"dd", L"d", L"mmmm", L"mme", L"mmm", L"mm", L"m", L"hh", L"h", L"nn",
		L"n", L"ss", L"s", L"tt", L"AMPM", L"ampm"
	};
	int i;
	if (!selector || !selector[0]) return FALSE;
	if (wcsncmp(selector, L"CUSTOM", 6) == 0) {
		const WCHAR* p = selector + 6;
		int number = 0;
		if (*p < L'0' || *p > L'9') return FALSE;
		while (*p >= L'0' && *p <= L'9') number = number * 10 + (int)(*p++ - L'0');
		return !*p && number >= 1 && number <= 32;
	}
	if (selector[0] == L'C' && (selector[1] == L'U' || selector[1] == L'C')) {
		const WCHAR* p = selector + 2;
		if (!*p) return TRUE;
		if (p[0] >= L'0' && p[0] <= L'9' && !p[1]) return TRUE;
		if (p[0] == L'e' && p[1] >= L'0' && p[1] <= L'9' &&
			p[2] >= L'0' && p[2] <= L'9' && !p[3]) return TRUE;
		return FALSE;
	}
	if (selector[0] == L'H' && selector[1] && selector[2] && selector[3] && !selector[4]) {
		if ((selector[1] == L'A' || selector[1] == L'U' || selector[1] == L'T') &&
			wcschr(L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", selector[2]) &&
			wcschr(L"MGTP", selector[3]) && !(selector[1] == L'T' && selector[3] == L'P')) return TRUE;
		if ((selector[1] == L'R' || selector[1] == L'W' || selector[1] == L'D') &&
			wcschr(L"ABCDEFGHIJKLMNOPQRSTUVWXYZ", selector[2]) && wcschr(L"BKMA", selector[3])) return TRUE;
	}
	for (i = 0; i < (int)ARRAYSIZE(selectors); i++) {
		if (wcscmp(selector, selectors[i]) == 0) return TRUE;
	}
	return FALSE;
}

BOOL TcFormatStyleParseRule(const WCHAR* text, int order, TC_FS_RULE* rule, TC_FS_REPORT* report)
{
	TC_FS_PARSER parser;
	TC_FS_BRANCH* branch;
	int root;
	if (!text || !rule) return FALSE;
	ZeroMemory(rule, sizeof(*rule));
	ZeroMemory(&parser, sizeof(parser));
	parser.start = text;
	parser.pos = text;
	parser.rule = rule;
	parser.report = report;
	rule->order = order;
	if (!fs_match_word(&parser, L"if") || !fs_match_char(&parser, L'(')) goto fail;
	root = fs_parse_or(&parser);
	if (root < 0 || !fs_match_char(&parser, L')')) goto fail;
	branch = &rule->branches[0];
	branch->rootNode = root;
	if (!fs_parse_declarations(&parser, branch)) goto fail;
	rule->branchCount = 1;
	for (;;) {
		BOOL hasCondition;
		if (!fs_match_word(&parser, L"else")) break;
		if (rule->branchCount >= TC_FS_MAX_BRANCHES) goto fail;
		branch = &rule->branches[rule->branchCount];
		hasCondition = fs_match_word(&parser, L"if");
		if (hasCondition) {
			if (!fs_match_char(&parser, L'(')) goto fail;
			root = fs_parse_or(&parser);
			if (root < 0 || !fs_match_char(&parser, L')')) goto fail;
			branch->rootNode = root;
		}
		else {
			branch->rootNode = -1;
		}
		if (!fs_parse_declarations(&parser, branch)) goto fail;
		rule->branchCount++;
		if (!hasCondition) break;
	}
	fs_skip_space(&parser);
	if (*parser.pos != L'\0') goto fail;
	return TRUE;

fail:
	fs_set_report(report, order, (int)(parser.pos - parser.start), L"Invalid FormatStyle rule syntax or value");
	ZeroMemory(rule, sizeof(*rule));
	return FALSE;
}

static int __cdecl fs_compare_rule(const void* left, const void* right)
{
	const TC_FS_RULE* a = (const TC_FS_RULE*)left;
	const TC_FS_RULE* b = (const TC_FS_RULE*)right;
	return (a->order > b->order) - (a->order < b->order);
}

static BOOL fs_parse_key_int(const WCHAR* text, int* value)
{
	int result = 0;
	if (!text || !*text) return FALSE;
	while (*text) {
		if (*text < L'0' || *text > L'9') return FALSE;
		result = (result * 10) + (int)(*text++ - L'0');
		if (result > 9999) return FALSE;
	}
	if (result < 1) return FALSE;
	*value = result;
	return TRUE;
}

BOOL TcFormatStyleLoadMulti(const WCHAR* sectionMulti, int sectionCch,
	TC_FS_RULESET* ruleset, TC_FS_REPORT* report)
{
	const WCHAR* line;
	const WCHAR* limit;
	BOOL enabledSeen = FALSE;
	int seenOrders[TC_FS_MAX_RULES];
	int seenOrderCount = 0;
	int i;
	if (!ruleset) return FALSE;
	TcFormatStyleInit(ruleset);
	if (report) ZeroMemory(report, sizeof(*report));
	if (!sectionMulti || sectionCch <= 1) return TRUE;
	line = sectionMulti;
	limit = sectionMulti + sectionCch;
	while (line < limit && *line) {
		const WCHAR* equal = wcschr(line, L'=');
		int lineLength = lstrlenW(line);
		if (!equal || equal >= line + lineLength) {
			fs_set_report(report, 0, 0, L"FormatStyle section entry has no value");
			return FALSE;
		}
		if (_wcsnicmp(line, L"Enabled=", 8) == 0 && equal == line + 7) {
			if (enabledSeen || (wcscmp(equal + 1, L"0") != 0 && wcscmp(equal + 1, L"1") != 0)) {
				fs_set_report(report, 0, 0, L"Invalid or duplicate FormatStyle Enabled value");
				return FALSE;
			}
			enabledSeen = TRUE;
			ruleset->enabled = (equal[1] == L'1');
		}
		else if (_wcsnicmp(line, L"SyntaxVersion=", 14) == 0 && equal == line + 13) {
			/* Legacy compatibility: SyntaxVersion is intentionally ignored. */
		}
		else if (_wcsnicmp(line, L"Rule", 4) == 0) {
			WCHAR orderText[16];
			int orderLength = (int)(equal - (line + 4));
			int order;
			BOOL duplicate = FALSE;
			if (orderLength <= 0 || orderLength >= (int)(sizeof(orderText) / sizeof(orderText[0])) || lstrlenW(equal + 1) > TC_FS_RULE_CCH) {
				ruleset->invalidCount++;
				line += lineLength + 1;
				continue;
			}
			CopyMemory(orderText, line + 4, (SIZE_T)orderLength * sizeof(WCHAR));
			orderText[orderLength] = L'\0';
			if (!fs_parse_key_int(orderText, &order)) {
				ruleset->invalidCount++;
				line += lineLength + 1;
				continue;
			}
			for (i = 0; i < seenOrderCount; i++) {
				if (seenOrders[i] == order) {
					duplicate = TRUE;
					break;
				}
			}
			if (duplicate) {
				for (i = 0; i < ruleset->ruleCount; i++) {
					if (ruleset->rules[i].order == order) {
						ruleset->rules[i].order = -1;
						ruleset->invalidCount++;
						break;
					}
				}
				ruleset->invalidCount++;
			}
			else if (seenOrderCount >= TC_FS_MAX_RULES || ruleset->ruleCount >= TC_FS_MAX_RULES) {
				fs_set_report(report, order, 0, L"FormatStyle rule capacity exceeded");
				return FALSE;
			}
			else {
				seenOrders[seenOrderCount++] = order;
				if (!TcFormatStyleParseRule(equal + 1, order, &ruleset->rules[ruleset->ruleCount], report)) {
					ruleset->invalidCount++;
				}
				else {
					ruleset->ruleCount++;
				}
			}
		}
		else if (report) {
			report->warnings++;
		}
		line += lineLength + 1;
	}
	for (i = ruleset->ruleCount - 1; i >= 0; i--) {
		if (ruleset->rules[i].order < 0) {
			if (i + 1 < ruleset->ruleCount) {
				MoveMemory(&ruleset->rules[i], &ruleset->rules[i + 1], (SIZE_T)(ruleset->ruleCount - i - 1) * sizeof(TC_FS_RULE));
			}
			ruleset->ruleCount--;
		}
	}
	if (!ruleset->enabled) {
		ruleset->ruleCount = 0;
		return TRUE;
	}
	if (!enabledSeen) {
		fs_set_report(report, 0, 0, L"Enabled FormatStyle requires an Enabled value");
		ruleset->enabled = FALSE;
		ruleset->ruleCount = 0;
		return FALSE;
	}
	qsort(ruleset->rules, (size_t)ruleset->ruleCount, sizeof(TC_FS_RULE), fs_compare_rule);
	return TRUE;
}

static TC_FS_TRUTH fs_compare_number(double value, BYTE compare, double literal)
{
	switch ((TC_FS_COMPARE)compare) {
	case TC_FS_CMP_EQ: return value == literal ? TC_FS_TRUE : TC_FS_FALSE;
	case TC_FS_CMP_NE: return value != literal ? TC_FS_TRUE : TC_FS_FALSE;
	case TC_FS_CMP_LT: return value < literal ? TC_FS_TRUE : TC_FS_FALSE;
	case TC_FS_CMP_LE: return value <= literal ? TC_FS_TRUE : TC_FS_FALSE;
	case TC_FS_CMP_GT: return value > literal ? TC_FS_TRUE : TC_FS_FALSE;
	case TC_FS_CMP_GE: return value >= literal ? TC_FS_TRUE : TC_FS_FALSE;
	default: return TC_FS_ERROR;
	}
}

typedef struct TC_FS_ITEM {
	const WCHAR* selector;
	const WCHAR* value;
	int length;
	BOOL changed;
	TC_FS_VALUE_CACHE cache;
	TC_FS_STYLE style;
} TC_FS_ITEM;

typedef struct TC_FS_FRAME {
	int count;
	TC_FS_ITEM items[TC_FS_MAX_ITEMS];
	WCHAR text[TC_FS_TEXT_CCH];
	char info[TC_FS_TEXT_CCH];
	TC_FORMAT_SPANS spans;
} TC_FS_FRAME;

static int fs_find_reference(const TC_FS_FRAME* frame, const WCHAR* selector)
{
	int found = -1;
	int i;
	for (i = 0; i < frame->count; i++) {
		if (wcscmp(frame->items[i].selector, selector) != 0) continue;
		if (found >= 0) return -1;
		found = i;
	}
	return found;
}

static TC_FS_TRUTH fs_eval_node(const TC_FS_RULE* rule, int nodeIndex, TC_FS_FRAME* frame)
{
	const TC_FS_NODE* node;
	TC_FS_TRUTH left;
	TC_FS_TRUTH right;
	const WCHAR* value = NULL;
	int valueLength = 0;
	TC_FS_VALUE_CACHE* cache = NULL;
	if (!rule || nodeIndex < 0 || nodeIndex >= rule->nodeCount) return TC_FS_ERROR;
	node = &rule->nodes[nodeIndex];
	if (node->selectorRef) {
		int index;
		TC_FS_ITEM* item;
		if (!frame) return TC_FS_ERROR;
		index = fs_find_reference(frame, rule->stringPool + node->selectorRef - 1);
		if (index < 0) return TC_FS_ERROR;
		item = &frame->items[index];
		value = item->value;
		valueLength = item->length;
		cache = &item->cache;
	}
	switch ((TC_FS_NODE_TYPE)node->type) {
	case TC_FS_NODE_FALSE: return TC_FS_FALSE;
	case TC_FS_NODE_TRUE: return TC_FS_TRUE;
	case TC_FS_NODE_NOT:
		left = fs_eval_node(rule, node->left, frame);
		if (left == TC_FS_ERROR) return TC_FS_ERROR;
		return left == TC_FS_TRUE ? TC_FS_FALSE : TC_FS_TRUE;
	case TC_FS_NODE_AND:
		left = fs_eval_node(rule, node->left, frame);
		if (left != TC_FS_TRUE) return left;
		return fs_eval_node(rule, node->right, frame);
	case TC_FS_NODE_OR:
		left = fs_eval_node(rule, node->left, frame);
		if (left == TC_FS_TRUE) return TC_FS_TRUE;
		if (left == TC_FS_ERROR) return TC_FS_ERROR;
		return fs_eval_node(rule, node->right, frame);
	case TC_FS_NODE_NUMERIC:
		if (!cache) return TC_FS_ERROR;
		if (cache->numericState == 0) {
			cache->numericState = fs_parse_decimal(value, valueLength, &cache->numericValue, NULL, TRUE) ? 1 : -1;
		}
		if (cache->numericState < 0) return TC_FS_ERROR;
		return fs_compare_number(cache->numericValue, node->compare, node->number);
	case TC_FS_NODE_TEXT:
		if (!value) return TC_FS_ERROR;
		if (node->compare != TC_FS_CMP_EQ && node->compare != TC_FS_CMP_NE) return TC_FS_ERROR;
		right = (valueLength == (int)node->textLength &&
			wcsncmp(value, rule->stringPool + node->textStart, (size_t)valueLength) == 0) ? TC_FS_TRUE : TC_FS_FALSE;
		return node->compare == TC_FS_CMP_NE ? (right == TC_FS_TRUE ? TC_FS_FALSE : TC_FS_TRUE) : right;
	default:
		return TC_FS_ERROR;
	}
}

void TcFormatStyleMerge(TC_FS_STYLE* target, const TC_FS_STYLE* declaration)
{
	DWORD mask = declaration->setMask & ~TC_FS_PROP_VALUE;
	if (mask & TC_FS_PROP_FORE_COLOR) target->foreColor = declaration->foreColor;
	if (mask & TC_FS_PROP_FONT) lstrcpynW(target->fontFace, declaration->fontFace, LF_FACESIZE);
	if (mask & TC_FS_PROP_FONT_SIZE) target->fontSize = declaration->fontSize;
	if (mask & TC_FS_PROP_BOLD) target->bold = declaration->bold;
	if (mask & TC_FS_PROP_ITALIC) target->italic = declaration->italic;
	if (mask & TC_FS_PROP_SHADOW) target->shadow = declaration->shadow;
	if (mask & TC_FS_PROP_BORDER) target->border = declaration->border;
	if (mask & TC_FS_PROP_SHADOW_COLOR) target->shadowColor = declaration->shadowColor;
	if (mask & TC_FS_PROP_SHADOW_RANGE) target->shadowRange = declaration->shadowRange;
	target->setMask |= mask;
}

int TcFormatStyleApply(const TC_FS_RULESET* ruleset, const WCHAR* selector,
	const WCHAR* value, int valueLength, const TC_FS_STYLE* baseStyle,
	TC_FS_STYLE* resolvedStyle, TC_FS_VALUE_CACHE* cache)
{
	TC_FS_FRAME* frame;
	int matched = 0;
	int i;
	if (!resolvedStyle || !baseStyle) return 0;
	*resolvedStyle = *baseStyle;
	if (!ruleset || !ruleset->enabled || !selector || !value || valueLength < 0) return 0;
	frame = (TC_FS_FRAME*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*frame));
	if (!frame) return 0;
	frame->count = 1;
	frame->items[0].selector = selector;
	frame->items[0].value = value;
	frame->items[0].length = valueLength;
	for (i = 0; i < ruleset->ruleCount; i++) {
		const TC_FS_RULE* rule = &ruleset->rules[i];
		int branchIndex;
		for (branchIndex = 0; branchIndex < rule->branchCount; branchIndex++) {
			const TC_FS_BRANCH* branch = &rule->branches[branchIndex];
			TC_FS_TRUTH condition = branch->rootNode < 0 ? TC_FS_TRUE :
				fs_eval_node(rule, branch->rootNode, frame);
			int targetIndex;
			if (condition == TC_FS_ERROR) break;
			if (condition != TC_FS_TRUE) continue;
			for (targetIndex = 0; targetIndex < branch->targetCount; targetIndex++) {
				const TC_FS_TARGET* target = &rule->targets[branch->targetStart + targetIndex];
				if (wcscmp(target->selector, selector) != 0 &&
					!(target->declaration.setMask & TC_FS_PROP_VALUE)) break;
			}
			if (targetIndex != branch->targetCount) break;
			for (targetIndex = 0; targetIndex < branch->targetCount; targetIndex++) {
				const TC_FS_TARGET* target = &rule->targets[branch->targetStart + targetIndex];
				if (wcscmp(target->selector, selector) != 0 ||
					(target->declaration.setMask & TC_FS_PROP_VALUE)) continue;
				TcFormatStyleMerge(resolvedStyle, &target->declaration);
				matched++;
			}
			break;
		}
	}
	if (cache) *cache = frame->items[0].cache;
	HeapFree(GetProcessHeap(), 0, frame);
	return matched;
}

static void fs_assign_item(TC_FS_ITEM* item, const TC_FS_RULE* rule, const TC_FS_STYLE* declaration)
{
	if (declaration->setMask & TC_FS_PROP_VALUE) {
		item->value = rule->stringPool + declaration->valueStart;
		item->length = declaration->valueLength;
		item->changed = TRUE;
		ZeroMemory(&item->cache, sizeof(item->cache));
	}
	TcFormatStyleMerge(&item->style, declaration);
}

static BOOL fs_resolve_items(const TC_FS_RULESET* ruleset, const WCHAR* text,
	int textLength, const TC_FORMAT_SPANS* spans, TC_FS_FRAME* frame)
{
	int r, b, t, i;
	if (!text || textLength < 0 || !spans || spans->overflow || spans->count < 0 || spans->count > TC_FORMAT_MAX_SPANS) return FALSE;
	for (i = 0; i < spans->count; i++) {
		const TC_FORMAT_SPAN* span = &spans->items[i];
		TC_FS_ITEM* item = &frame->items[i];
		if (span->start < 0 || span->length < 0 || span->start > textLength || span->length > textLength - span->start ||
			(i > 0 && span->start < spans->items[i - 1].start + spans->items[i - 1].length)) return FALSE;
		item->selector = span->selector;
		item->value = text + span->start;
		item->length = span->length;
	}
	frame->count = spans->count;
	if (!ruleset || !ruleset->enabled) return TRUE;
	for (r = 0; r < ruleset->ruleCount; r++) {
		const TC_FS_RULE* rule = &ruleset->rules[r];
		for (b = 0; b < rule->branchCount; b++) {
			const TC_FS_BRANCH* branch = &rule->branches[b];
			BOOL valid = TRUE;
			int missing = 0;
			TC_FS_TRUTH condition = branch->rootNode < 0 ? TC_FS_TRUE :
				fs_eval_node(rule, branch->rootNode, frame);
			if (condition == TC_FS_ERROR) break;
			if (condition != TC_FS_TRUE) continue;
			for (t = 0; t < branch->targetCount; t++) {
				const TC_FS_TARGET* target = &rule->targets[branch->targetStart + t];
				for (i = 0; i < frame->count; i++) {
					if (wcscmp(frame->items[i].selector, target->selector) == 0) break;
				}
				if (i == frame->count) {
					if (!(target->declaration.setMask & TC_FS_PROP_VALUE)) { valid = FALSE; break; }
					missing++;
				}
			}
			if (!valid) break;
			if (missing > TC_FS_MAX_ITEMS - frame->count) return FALSE;
			for (t = 0; t < branch->targetCount; t++) {
				const TC_FS_TARGET* target = &rule->targets[branch->targetStart + t];
				BOOL found = FALSE;
				for (i = 0; i < frame->count; i++) {
					if (wcscmp(frame->items[i].selector, target->selector) != 0) continue;
					fs_assign_item(&frame->items[i], rule, &target->declaration);
					found = TRUE;
				}
				if (!found) {
					TC_FS_ITEM* item = &frame->items[frame->count++];
					item->selector = target->selector;
					fs_assign_item(item, rule, &target->declaration);
				}
			}
			break;
		}
	}
	return TRUE;
}

BOOL TcFormatStyleResolveFrame(const TC_FS_RULESET* ruleset, const WCHAR* text,
	int textLength, const TC_FORMAT_SPANS* spans, TC_FS_STYLE* overlays, int capacity)
{
	TC_FS_FRAME* frame;
	BOOL ok;
	int i;
	if (!overlays || capacity < 0 || capacity > TC_FORMAT_MAX_SPANS) return FALSE;
	ZeroMemory(overlays, (SIZE_T)capacity * sizeof(*overlays));
	if (!spans || spans->count < 0 || spans->count > capacity) return FALSE;
	frame = (TC_FS_FRAME*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*frame));
	if (!frame) return FALSE;
	ok = fs_resolve_items(ruleset, text, textLength, spans, frame);
	if (ok) for (i = 0; i < spans->count; i++) overlays[i] = frame->items[i].style;
	HeapFree(GetProcessHeap(), 0, frame);
	return ok;
}

BOOL TcFormatStyleTransform(const TC_FS_RULESET* ruleset, WCHAR* text, int capacity,
	char* info, TC_FORMAT_SPANS* spans)
{
	TC_FS_FRAME* frame;
	BOOL ok = FALSE;
	int length, i, used = 0, source = 0;
	if (!text || !info || !spans || capacity <= 0 || capacity > TC_FS_TEXT_CCH) return FALSE;
	/* A cached frame is final: never evaluate its assignments twice. */
	if (spans->resolved) return TRUE;
	for (length = 0; length < capacity && text[length]; length++) {}
	if (length == capacity) return FALSE;
	frame = (TC_FS_FRAME*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*frame));
	if (!frame) return FALSE;
	if (!fs_resolve_items(ruleset, text, length, spans, frame)) goto done;
	frame->spans = *spans;
	for (i = 0; i <= spans->count; i++) {
		const TC_FORMAT_SPAN* original = i < spans->count ? &spans->items[i] : NULL;
		int gap = (original ? original->start : length) - source;
		if (gap >= capacity - used) goto done;
		CopyMemory(frame->text + used, text + source, (SIZE_T)gap * sizeof(WCHAR));
		CopyMemory(frame->info + used, info + source, (SIZE_T)gap);
		used += gap;
		if (original) {
			TC_FS_ITEM* item = &frame->items[i];
			TC_FORMAT_SPAN* output = &frame->spans.items[i];
			if (item->length >= capacity - used) goto done;
			output->start = used;
			output->length = item->length;
			output->style = item->style;
			CopyMemory(frame->text + used, item->value, (SIZE_T)item->length * sizeof(WCHAR));
			if (item->changed) FillMemory(frame->info + used, (SIZE_T)item->length, original->zone);
			else CopyMemory(frame->info + used, info + original->start, (SIZE_T)item->length);
			used += item->length;
			source = original->start + original->length;
		}
	}
	frame->text[used] = L'\0';
	frame->info[used] = '\0';
	frame->spans.resolved = TRUE;
	CopyMemory(text, frame->text, (SIZE_T)(used + 1) * sizeof(WCHAR));
	CopyMemory(info, frame->info, (SIZE_T)(used + 1));
	*spans = frame->spans;
	ok = TRUE;
done:
	HeapFree(GetProcessHeap(), 0, frame);
	return ok;
}
