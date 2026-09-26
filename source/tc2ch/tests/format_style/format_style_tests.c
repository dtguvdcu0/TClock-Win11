#include <stdio.h>
#include <string.h>

#include "../../dll/format_style.h"
#include "../../common/color_value.h"

static int g_failures = 0;

static void test_check(BOOL condition, const char* name)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", name);
		g_failures++;
	}
}

static TC_FS_STYLE test_base(void)
{
	TC_FS_STYLE style;
	ZeroMemory(&style, sizeof(style));
	style.foreColor = RGB(1, 2, 3);
	style.shadowColor = RGB(4, 5, 6);
	style.fontSize = 12;
	style.shadowRange = 1;
	return style;
}

static void test_newline_lengths(void)
{
	static const WCHAR lf[] = L"\n";
	static const WCHAR cr[] = L"\r";
	static const WCHAR crlf[] = L"\r\n";
	static const WCHAR mixed[] = L"A\r\nB\nC\rD";
	int position;
	int lines = 1;

	test_check(TcFormatStyleGetNewlineLength(lf, 1, 0) == 1, "LF newline length");
	test_check(TcFormatStyleGetNewlineLength(cr, 1, 0) == 1, "CR newline length");
	test_check(TcFormatStyleGetNewlineLength(crlf, 2, 0) == 2, "CRLF newline length");
	test_check(TcFormatStyleGetNewlineLength(crlf, 2, 1) == 1, "LF recognized at its own position");
	test_check(TcFormatStyleGetNewlineLength(mixed, (int)_countof(mixed) - 1, 1) == 2, "mixed CRLF length");
	test_check(TcFormatStyleGetNewlineLength(mixed, (int)_countof(mixed) - 1, 4) == 1, "mixed LF length");
	test_check(TcFormatStyleGetNewlineLength(mixed, (int)_countof(mixed) - 1, 6) == 1, "mixed CR length");
	for (position = 0; position < (int)_countof(mixed) - 1;) {
		int newlineLength = TcFormatStyleGetNewlineLength(mixed, (int)_countof(mixed) - 1, position);
		if (newlineLength > 0) {
			lines++;
			position += newlineLength;
		}
		else position++;
	}
	test_check(lines == 4, "mixed newline forms produce four layout lines");
}

static void test_color_values(void)
{
    COLORREF color = 0;
    TCV_KIND kind = 0;
    static const WCHAR names[][32] = { L"aliceblue", L"darkslategrey", L"rebeccapurple", L"transparent" };
    static const COLORREF expected[] = { RGB(240,248,255), RGB(47,79,79), RGB(102,51,153), 0 };
    int i;

    for (i = 0; i < 3; i++) {
        test_check(tcv_parse(names[i], &color, &kind), "CSS named color parses");
        test_check(color == expected[i], "CSS named color value");
        test_check(kind == TCV_KIND_NAME, "CSS named color kind");
    }
    test_check(!tcv_parse(names[3], &color, &kind), "transparent is not an opaque color name");
    test_check(tcv_parse(L"ReD", &color, &kind) && color == RGB(255,0,0), "named color case insensitive");
    test_check(tcv_parse(L"#0f8", &color, &kind) && color == RGB(0,255,136), "short hex channel order");
    test_check(tcv_parse(L"#FF0088", &color, &kind) && color == RGB(255,0,136), "long hex channel order");
    test_check(tcv_parse(L"RGB(255,0,0)", &color, &kind) && color == RGB(255,0,0), "legacy RGB syntax preserved");
    test_check(tcv_parse(L"16711680", &color, &kind) && color == 16711680, "legacy decimal value preserved");
    test_check(!tcv_parse(L"#12", &color, &kind), "short invalid hex rejected");
    test_check(!tcv_parse(L"#GG0000", &color, &kind), "invalid hex digit rejected");
    test_check(!tcv_parse(L"red trailing", &color, &kind), "trailing color text rejected");
    test_check(!tcv_parse(L"16777216", &color, &kind), "decimal outside COLORREF range rejected");
    test_check(!tcv_parse_ascii("caf\xc3\xa9", &color, &kind), "non-ASCII color input rejected");
}

static void test_format_style_named_color(void)
{
    TC_FS_RULE rule;
    TC_FS_REPORT report;
    TC_FS_STYLE base = test_base();
    TC_FS_STYLE resolved;

    test_check(TcFormatStyleParseRule(L"CU if (value >= 70) { ForeColor=red; ShadowColor=#40FF40; }", 1, &rule, &report), "FormatStyle CSS color parse");
    test_check(rule.branchCount == 1, "FormatStyle single branch count");
    test_check(rule.branches[0].declaration.foreColor == RGB(255,0,0), "FormatStyle named color value");
    test_check(rule.branches[0].declaration.shadowColor == RGB(64,255,64), "FormatStyle hex color value");
    {
        TC_FS_RULESET ruleset;
        TcFormatStyleInit(&ruleset);
        ruleset.enabled = TRUE;
        ruleset.rules[0] = rule;
        ruleset.ruleCount = 1;
        test_check(TcFormatStyleApply(&ruleset, L"CU", L"70", 2, &base, &resolved, NULL) == 1, "FormatStyle CSS color apply");
        test_check(resolved.foreColor == RGB(255,0,0), "FormatStyle CSS color applied");
        test_check(resolved.shadowColor == RGB(64,255,64), "FormatStyle hex color applied");
    }
}

static void test_numeric_rule(void)
{
	TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FS_STYLE base = test_base();
	TC_FS_STYLE resolved;
	WCHAR section[] =
		L"Enabled=1\0"
		L"Rule10=CU if (value >= 70) { ForeColor=RGB(255,176,0); }\0"
		L"\0";

	test_check(TcFormatStyleLoadMulti(section, (int)_countof(section), &ruleset, &report), "numeric load");
	test_check(ruleset.ruleCount == 1, "numeric rule count");
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"69", 2, &base, &resolved, NULL) == 0, "numeric below");
	test_check(resolved.foreColor == base.foreColor, "numeric below color");
	test_check(TcFormatStyleApply(&ruleset, L"CU", L" 70 ", 4, &base, &resolved, NULL) == 1, "numeric trim match");
	test_check(resolved.foreColor == RGB(255, 176, 0), "numeric color");
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"70%", 3, &base, &resolved, NULL) == 0, "numeric unit error");
}

static void test_string_rule(void)
{
	TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FS_STYLE base = test_base();
	TC_FS_STYLE resolved;
	WCHAR section[] =
		L"Enabled=1\0"
		L"SyntaxVersion=1\0"
		L"Rule1=CUSTOM1 if (value == \"ERROR\") { Italic=1; }\0"
		L"\0";

	test_check(TcFormatStyleLoadMulti(section, (int)_countof(section), &ruleset, &report), "string load with legacy SyntaxVersion");
	test_check(TcFormatStyleApply(&ruleset, L"CUSTOM1", L"ERROR", 5, &base, &resolved, NULL) == 1, "string exact");
	test_check(resolved.italic == 1, "string style");
	test_check(TcFormatStyleApply(&ruleset, L"CUSTOM1", L"error", 5, &base, &resolved, NULL) == 0, "string case sensitive");
}

static void test_conditional_branches(void)
{
	TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FS_STYLE base = test_base();
	TC_FS_STYLE resolved;
	WCHAR section[] =
		L"Enabled=1\0"
		L"SyntaxVersion=not-a-version\0"
		L"Rule1=ddd if (value == \"土\") { ForeColor=blue; Bold=1; } else if (value == \"日\") { ForeColor=red; }\0"
		L"Rule2=CU if (value >= 90) { ForeColor=red; } else if (value >= 70) { ForeColor=orange; } else { ForeColor=blue; }\0"
		L"Rule3=TEMP if (value == \"ready\") { Italic=1; } else if (value >= 5) { Bold=1; } else { ForeColor=green; }\0"
		L"\0";

	test_check(TcFormatStyleLoadMulti(section, (int)_countof(section), &ruleset, &report), "branch load without syntax version semantics");
	test_check(report.warnings == 0, "legacy SyntaxVersion ignored without warning");
	test_check(ruleset.ruleCount == 3, "branch rule count");

	test_check(TcFormatStyleApply(&ruleset, L"ddd", L"土", 1, &base, &resolved, NULL) == 1, "first string branch matches");
	test_check(resolved.foreColor == RGB(0,0,255) && resolved.bold == 1, "first string branch properties");
	test_check(TcFormatStyleApply(&ruleset, L"ddd", L"日", 1, &base, &resolved, NULL) == 1, "else-if string branch matches");
	test_check(resolved.foreColor == RGB(255,0,0) && resolved.bold == base.bold, "else-if is exclusive and partial");
	test_check(TcFormatStyleApply(&ruleset, L"ddd", L"月", 1, &base, &resolved, NULL) == 0, "string chain without else leaves base style");
	test_check(resolved.foreColor == base.foreColor, "unmatched string chain keeps base color");

	test_check(TcFormatStyleApply(&ruleset, L"CU", L"95", 2, &base, &resolved, NULL) == 1, "numeric first branch matches");
	test_check(resolved.foreColor == RGB(255,0,0), "numeric first branch wins");
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"80", 2, &base, &resolved, NULL) == 1, "numeric else-if branch matches");
	test_check(resolved.foreColor == RGB(255,165,0), "numeric else-if color");
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"69", 2, &base, &resolved, NULL) == 1, "numeric else branch matches");
	test_check(resolved.foreColor == RGB(0,0,255), "numeric else color");
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"N/A", 3, &base, &resolved, NULL) == 0, "numeric conversion error does not fall through to else");
	test_check(resolved.foreColor == base.foreColor, "numeric conversion error leaves base style");

	test_check(TcFormatStyleApply(&ruleset, L"TEMP", L"6", 1, &base, &resolved, NULL) == 1, "numeric else-if after false text condition matches");
	test_check(resolved.bold == 1 && resolved.foreColor == base.foreColor, "numeric else-if keeps omitted properties");
	test_check(TcFormatStyleApply(&ruleset, L"TEMP", L"N/A", 3, &base, &resolved, NULL) == 0, "later conversion error aborts chain before else");
	test_check(resolved.foreColor == base.foreColor, "error in else-if does not apply else style");
}

static void test_cascade(void)
{
	TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FS_STYLE base = test_base();
	TC_FS_STYLE resolved;
	WCHAR section[] =
		L"Enabled=1\0"
		L"SyntaxVersion=1\0"
		L"Rule30=CU if (value >= 90) { ForeColor=RGB(9,9,9); }\0"
		L"Rule10=CU if (value >= 70) { ForeColor=RGB(7,7,7); Bold=1; }\0"
		L"Rule20=CU if (value >= 80) { Italic=1; }\0"
		L"\0";

	test_check(TcFormatStyleLoadMulti(section, (int)_countof(section), &ruleset, &report), "cascade load");
	test_check(ruleset.rules[0].order == 10 && ruleset.rules[2].order == 30, "cascade numeric order");
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"95", 2, &base, &resolved, NULL) == 3, "cascade matches");
	test_check(resolved.foreColor == RGB(9, 9, 9), "cascade later color");
	test_check(resolved.bold == 1 && resolved.italic == 1, "cascade partial properties");
}

static void test_error_logic(void)
{
	TC_FS_RULE rule;
	TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FS_STYLE base = test_base();
	TC_FS_STYLE resolved;

	TcFormatStyleInit(&ruleset);
	ruleset.enabled = TRUE;
	test_check(TcFormatStyleParseRule(L"CU if (!(value > 70)) { Bold=1; }", 1, &rule, &report), "error-not parse");
	ruleset.rules[0] = rule;
	ruleset.ruleCount = 1;
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"NA", 2, &base, &resolved, NULL) == 0, "not error does not match");

	test_check(TcFormatStyleParseRule(L"CU if (false && value > 70) { Bold=1; }", 1, &rule, &report), "and short parse");
	ruleset.rules[0] = rule;
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"NA", 2, &base, &resolved, NULL) == 0, "and short circuit");

	test_check(TcFormatStyleParseRule(L"CU if (true || value > 70) { Bold=1; }", 1, &rule, &report), "or short parse");
	ruleset.rules[0] = rule;
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"NA", 2, &base, &resolved, NULL) == 1, "or short circuit");
}

static void test_validation(void)
{
	TC_FS_RULE rule;
	TC_FS_REPORT report;
	ZeroMemory(&report, sizeof(report));
	test_check(!TcFormatStyleParseRule(L"CU if (value >= 70) { FontSize=4; }", 1, &rule, &report), "font size low rejected");
	test_check(!TcFormatStyleParseRule(L"CU if (value >= 70) { BackColor=RGB(1,2,3); }", 1, &rule, &report), "global property rejected");
	test_check(!TcFormatStyleParseRule(L"CU if (value >= 70) { Bold=1; Bold=0; }", 1, &rule, &report), "duplicate property rejected");
	test_check(TcFormatStyleParseRule(L"CU if (true) { Bold=1; } else if (false) { Bold=0; }", 1, &rule, &report), "same property allowed in alternative branches");
	test_check(!TcFormatStyleParseRule(L"CU if (true) { Bold=1; } else { Italic=1; } else if (true) { Bold=0; }", 1, &rule, &report), "else must be final");
	test_check(!TcFormatStyleParseRule(L"CU if (true) { Bold=1; Bold=0; } else { Italic=1; }", 1, &rule, &report), "duplicate property rejected within one branch");
	test_check(TcFormatStyleParseRule(L"CU if (false) { Bold=1; } else if (false) { Bold=0; } else if (false) { Italic=1; } else if (false) { Italic=0; } else if (false) { ForeColor=red; } else if (false) { ShadowColor=blue; } else if (false) { FontSize=12; } else { ForeColorShadow=1; }", 1, &rule, &report), "maximum branch capacity accepted");
	test_check(!TcFormatStyleParseRule(L"CU if (true) { Bold=1; } else if (true) { Italic=1; } else if (true) { ForeColor=red; } else if (true) { ShadowColor=blue; } else if (true) { FontSize=12; } else if (true) { ForeColorShadow=1; } else if (true) { ForeColorBorder=1; } else if (true) { ClockShadowRange=2; } else if (true) { Bold=0; }", 1, &rule, &report), "branch capacity exceeded rejected");
}

static void test_rule_order_and_duplicates(void)
{
	static const WCHAR sortedSection[] =
		L"Enabled=1\0SyntaxVersion=1\0"
		L"Rule20=CU if (value >= 20) { Bold=1; }\0"
		L"Rule10=CU if (value >= 10) { Italic=1; }\0\0";
	static const WCHAR duplicateSection[] =
		L"Enabled=1\0SyntaxVersion=1\0"
		L"Rule10=CU if (value >= 10) { Bold=1; }\0"
		L"Rule10=CU if (value >= 20) { Italic=1; }\0"
		L"Rule10=CU if (value >= 30) { FontSize=18; }\0\0";
	TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FS_STYLE baseStyle;
	TC_FS_STYLE resolvedStyle;

	test_check(TcFormatStyleLoadMulti(sortedSection, ARRAYSIZE(sortedSection), &ruleset, &report), "sorted load");
	test_check(ruleset.ruleCount == 2, "sorted rule count");
	test_check(ruleset.rules[0].order == 10, "sorted first order");
	test_check(ruleset.rules[1].order == 20, "sorted second order");
	ZeroMemory(&baseStyle, sizeof(baseStyle));
	TcFormatStyleApply(&ruleset, L"CU", L"25", 2, &baseStyle, &resolvedStyle, NULL);
	test_check(resolvedStyle.italic == 1, "sorted earlier style");
	test_check(resolvedStyle.bold == 1, "sorted later style");

	test_check(TcFormatStyleLoadMulti(duplicateSection, ARRAYSIZE(duplicateSection), &ruleset, &report), "duplicate load");
	test_check(ruleset.ruleCount == 0, "all duplicate rules disabled");
	test_check(ruleset.invalidCount == 3, "all duplicate rules counted");
}

static void test_selector_and_precedence(void)
{
	TC_FS_RULE rule;
	TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FS_STYLE base = test_base();
	TC_FS_STYLE resolved;

	test_check(TcFormatStyleSelectorSupported(L"CU"), "aggregate CPU selector");
	test_check(TcFormatStyleSelectorSupported(L"CUe31"), "extended CPU selector");
	test_check(TcFormatStyleSelectorSupported(L"CUSTOM32"), "custom selector upper bound");
	test_check(!TcFormatStyleSelectorSupported(L"CUSTOM33"), "custom selector overflow rejected");
	test_check(!TcFormatStyleSelectorSupported(L"UNKNOWN"), "unknown selector rejected");
	test_check(!TcFormatStyleParseRule(L"UNKNOWN if (true) { Bold=1; }", 1, &rule, &report), "unknown rule rejected");

	TcFormatStyleInit(&ruleset);
	ruleset.enabled = TRUE;
	test_check(TcFormatStyleParseRule(
		L"CU if (false || true && value == 70) { Bold=1; }", 1, &rule, &report), "precedence parse");
	ruleset.rules[0] = rule;
	ruleset.ruleCount = 1;
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"70", 2, &base, &resolved, NULL) == 1, "and before or");
	test_check(resolved.bold == 1, "precedence style");
}

int main(void)
{
	test_newline_lengths();
	test_color_values();
	test_format_style_named_color();
	test_numeric_rule();
	test_string_rule();
	test_cascade();
	test_error_logic();
	test_validation();
	test_rule_order_and_duplicates();
	test_selector_and_precedence();
	test_conditional_branches();
	if (g_failures != 0) {
		fprintf(stderr, "%d FormatStyle test(s) failed.\n", g_failures);
		return 1;
	}
	printf("All FormatStyle tests passed.\n");
	return 0;
}
