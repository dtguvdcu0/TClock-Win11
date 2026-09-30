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

    test_check(TcFormatStyleParseRule(L"if (CU.value >= 70) { CU.ForeColor=red; CU.ShadowColor=#40FF40; }", 1, &rule, &report), "FormatStyle CSS color parse");
    test_check(rule.branchCount == 1, "FormatStyle single branch count");
    test_check(rule.targets[0].declaration.foreColor == RGB(255,0,0), "FormatStyle named color value");
    test_check(rule.targets[0].declaration.shadowColor == RGB(64,255,64), "FormatStyle hex color value");
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
		L"Rule10=if (CU.value >= 70) { CU.ForeColor=RGB(255,176,0); }\0"
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
		L"Rule1=if (CUSTOM1.value == \"ERROR\") { CUSTOM1.Italic=1; }\0"
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
		L"Rule1=if (ddd.value == \"土\") { ddd.ForeColor=blue; ddd.Bold=1; } else if (ddd.value == \"日\") { ddd.ForeColor=red; }\0"
		L"Rule2=if (CU.value >= 90) { CU.ForeColor=red; } else if (CU.value >= 70) { CU.ForeColor=orange; } else { CU.ForeColor=blue; }\0"
		L"Rule3=if (TEMP.value == \"ready\") { TEMP.Italic=1; } else if (TEMP.value >= 5) { TEMP.Bold=1; } else { TEMP.ForeColor=green; }\0"
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
		L"Rule30=if (CU.value >= 90) { CU.ForeColor=RGB(9,9,9); }\0"
		L"Rule10=if (CU.value >= 70) { CU.ForeColor=RGB(7,7,7); CU.Bold=1; }\0"
		L"Rule20=if (CU.value >= 80) { CU.Italic=1; }\0"
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
	test_check(TcFormatStyleParseRule(L"if (!(CU.value > 70)) { CU.Bold=1; }", 1, &rule, &report), "error-not parse");
	ruleset.rules[0] = rule;
	ruleset.ruleCount = 1;
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"NA", 2, &base, &resolved, NULL) == 0, "not error does not match");

	test_check(TcFormatStyleParseRule(L"if (false && CU.value > 70) { CU.Bold=1; }", 1, &rule, &report), "and short parse");
	ruleset.rules[0] = rule;
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"NA", 2, &base, &resolved, NULL) == 0, "and short circuit");

	test_check(TcFormatStyleParseRule(L"if (true || CU.value > 70) { CU.Bold=1; }", 1, &rule, &report), "or short parse");
	ruleset.rules[0] = rule;
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"NA", 2, &base, &resolved, NULL) == 1, "or short circuit");
}

static void test_validation(void)
{
	TC_FS_RULE rule;
	TC_FS_REPORT report;
	ZeroMemory(&report, sizeof(report));
	test_check(!TcFormatStyleParseRule(L"if (CU.value >= 70) { CU.FontSize=4; }", 1, &rule, &report), "font size low rejected");
	test_check(!TcFormatStyleParseRule(L"if (CU.value >= 70) { BackColor=RGB(1,2,3); }", 1, &rule, &report), "global property rejected");
	test_check(!TcFormatStyleParseRule(L"if (CU.value >= 70) { CU.Bold=1; CU.Bold=0; }", 1, &rule, &report), "duplicate property rejected");
	test_check(TcFormatStyleParseRule(L"if (true) { CU.Bold=1; } else if (false) { CU.Bold=0; }", 1, &rule, &report), "same property allowed in alternative branches");
	test_check(!TcFormatStyleParseRule(L"if (true) { CU.Bold=1; } else { CU.Italic=1; } else if (true) { CU.Bold=0; }", 1, &rule, &report), "else must be final");
	test_check(!TcFormatStyleParseRule(L"if (true) { CU.Bold=1; CU.Bold=0; } else { CU.Italic=1; }", 1, &rule, &report), "duplicate property rejected within one branch");
	test_check(TcFormatStyleParseRule(L"if (false) { CU.Bold=1; } else if (false) { CU.Bold=0; } else if (false) { CU.Italic=1; } else if (false) { CU.Italic=0; } else if (false) { CU.ForeColor=red; } else if (false) { CU.ShadowColor=blue; } else if (false) { CU.FontSize=12; } else { CU.ForeColorShadow=1; }", 1, &rule, &report), "maximum branch capacity accepted");
	test_check(!TcFormatStyleParseRule(L"if (true) { CU.Bold=1; } else if (true) { CU.Italic=1; } else if (true) { CU.ForeColor=red; } else if (true) { CU.ShadowColor=blue; } else if (true) { CU.FontSize=12; } else if (true) { CU.ForeColorShadow=1; } else if (true) { CU.ForeColorBorder=1; } else if (true) { CU.ClockShadowRange=2; } else if (true) { CU.Bold=0; }", 1, &rule, &report), "branch capacity exceeded rejected");
}

static void test_rule_order_and_duplicates(void)
{
	static const WCHAR sortedSection[] =
		L"Enabled=1\0SyntaxVersion=1\0"
		L"Rule20=if (CU.value >= 20) { CU.Bold=1; }\0"
		L"Rule10=if (CU.value >= 10) { CU.Italic=1; }\0\0";
	static const WCHAR duplicateSection[] =
		L"Enabled=1\0SyntaxVersion=1\0"
		L"Rule10=if (CU.value >= 10) { CU.Bold=1; }\0"
		L"Rule10=if (CU.value >= 20) { CU.Italic=1; }\0"
		L"Rule10=if (CU.value >= 30) { CU.FontSize=18; }\0\0";
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
	test_check(TcFormatStyleParseRule(L"if (true) { UNKNOWN.Bold=1; }", 1, &rule, &report), "named rule accepted");

	TcFormatStyleInit(&ruleset);
	ruleset.enabled = TRUE;
	test_check(TcFormatStyleParseRule(
		L"if (false || true && CU.value == 70) { CU.Bold=1; }", 1, &rule, &report), "precedence parse");
	ruleset.rules[0] = rule;
	ruleset.ruleCount = 1;
	test_check(TcFormatStyleApply(&ruleset, L"CU", L"70", 2, &base, &resolved, NULL) == 1, "and before or");
	test_check(resolved.bold == 1, "precedence style");
}

static void test_cross_items(void)
{
	static TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FORMAT_SPANS spans;
	TC_FS_STYLE styles[4];
	TC_FS_STYLE base = test_base();
	TC_FS_STYLE merged;
	int i;
	const WCHAR* selectors[] = { L"CUSTOM3", L"CUSTOM1", L"CUSTOM2", L"CUSTOM3" };
	const WCHAR* text = L"10 80 90 20";
	ZeroMemory(&spans, sizeof(spans));
	spans.count = 4;
	for (i = 0; i < 4; i++) {
		spans.items[i].start = i * 3;
		spans.items[i].length = 2;
		lstrcpynW(spans.items[i].selector, selectors[i], TC_FS_SELECTOR_CCH);
	}
	TcFormatStyleInit(&ruleset);
	ruleset.enabled = TRUE;
	ruleset.ruleCount = 1;
	test_check(TcFormatStyleParseRule(L"if (CUSTOM1.value >= 80 && CUSTOM2.value >= 90) { CUSTOM1.ForeColor=red; CUSTOM2.ForeColor=red; CUSTOM3.Bold=1; }", 1, &ruleset.rules[0], &report), "cross-item parse");
	test_check(TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4), "cross-item frame");
	test_check(styles[1].foreColor == RGB(255,0,0) && styles[2].foreColor == RGB(255,0,0), "compound condition colors both items");
	test_check(styles[0].bold && styles[3].bold, "cross-target before and after anchor and all duplicates");
	merged = base;
	TcFormatStyleMerge(&merged, &styles[0]);
	test_check(merged.bold && merged.foreColor == base.foreColor && merged.fontSize == base.fontSize, "partial overlay preserves base");
	test_check(TcFormatStyleResolveFrame(&ruleset, L"10 79 90 20", 11, &spans, styles, 4), "next frame");
	test_check(!styles[0].setMask && !styles[1].setMask && !styles[2].setMask && !styles[3].setMask, "false condition clears previous frame");

	test_check(TcFormatStyleParseRule(L"if (CUSTOM1.value == 80 && CUSTOM2.value == \"90\") { CUSTOM1.Bold=1; }", 1, &ruleset.rules[0], &report), "explicit anchor and cross text parse");
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(styles[1].bold, "qualified anchor equals local value");
	test_check(!TcFormatStyleParseRule(L"if (true) { CUSTOM1.Bold=1; CUSTOM1.Bold=0; }", 1, &ruleset.rules[0], &report), "duplicate explicit property rejected");
	test_check(!TcFormatStyleParseRule(L"CUSTOM1 if (CUSTOM1.value == 80) { CUSTOM1.Bold=1; }", 1, &ruleset.rules[0], &report), "leading selector rejected");
	test_check(!TcFormatStyleParseRule(L"if (value == 80) { CUSTOM1.Bold=1; }", 1, &ruleset.rules[0], &report), "unqualified condition rejected");
	test_check(!TcFormatStyleParseRule(L"if (true) { Bold=1; }", 1, &ruleset.rules[0], &report), "unqualified destination rejected");
	test_check(!TcFormatStyleParseRule(L"if (true) { CUSTOM2.Bold=1; CUSTOM2.bold=0; }", 1, &ruleset.rules[0], &report), "qualified duplicate rejected");
	test_check(TcFormatStyleParseRule(L"if (UNKNOWN.value == 1) { CUSTOM1.Bold=1; }", 1, &ruleset.rules[0], &report), "named reference accepted");
	test_check(!TcFormatStyleParseRule(L"if (true) { CUSTOM2.value=1; }", 1, &ruleset.rules[0], &report), "value assignment requires a string literal");

	TcFormatStyleParseRule(L"if (CUSTOM3.value >= 0) { CUSTOM1.Bold=1; } else { CUSTOM1.Italic=1; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(!styles[1].setMask, "ambiguous reference aborts without else");
	TcFormatStyleParseRule(L"if (CUSTOM4.value >= 0) { CUSTOM1.Bold=1; } else { CUSTOM1.Italic=1; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(!styles[1].setMask, "missing reference aborts without else");
	TcFormatStyleParseRule(L"if (true || CUSTOM4.value >= 0) { CUSTOM1.Bold=1; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(styles[1].bold, "short circuit skips missing reference");
	TcFormatStyleParseRule(L"if (true) { CUSTOM1.Bold=1; CUSTOM2.Bold=1; CUSTOM4.Bold=1; } else { CUSTOM1.Italic=1; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(!styles[1].setMask && !styles[2].setMask, "missing destination prevents partial application");
	test_check(TcFormatStyleApply(&ruleset, L"CUSTOM1", L"80", 2, &base, &merged, NULL) == 0 &&
		memcmp(&merged, &base, sizeof(base)) == 0, "single-item apply respects missing destination");

	TcFormatStyleParseRule(L"if (CUSTOM2.value >= 90) { CUSTOM1.Bold=1; } else { CUSTOM1.Italic=1; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleResolveFrame(&ruleset, L"10 80 XX 20", 11, &spans, styles, 4);
	test_check(!styles[1].setMask, "cross numeric error aborts chain");
	TcFormatStyleParseRule(L"if (CUSTOM3.value == 10) { CUSTOM3.Bold=1; CUSTOM2.ForeColor=red; } else { CUSTOM2.ForeColor=blue; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(!styles[0].setMask && !styles[3].setMask && !styles[2].setMask, "duplicate condition reference aborts the rule");
	TcFormatStyleParseRule(L"if (true) { CUSTOM3.Bold=1; CUSTOM2.ForeColor=blue; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(styles[0].bold && styles[3].bold && styles[2].foreColor == RGB(0,0,255), "explicit destination updates every duplicate");
	ruleset.ruleCount = 2;
	TcFormatStyleParseRule(L"if (true) { CUSTOM2.ForeColor=green; CUSTOM2.Italic=1; }", 2, &ruleset.rules[1], &report);
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(styles[2].foreColor == RGB(0,128,0) && styles[2].italic, "later rule overlays target");
	ruleset.ruleCount = 1;
	TcFormatStyleParseRule(L"if (true) { CUSTOM2.Font=\"Arial\"; CUSTOM2.FontSize=24; CUSTOM2.Bold=1; CUSTOM2.Italic=1; CUSTOM2.ForeColorShadow=1; CUSTOM2.ForeColorBorder=1; CUSTOM2.ShadowColor=blue; CUSTOM2.ClockShadowRange=3; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4);
	test_check(styles[2].fontSize == 24 && wcscmp(styles[2].fontFace,L"Arial") == 0 && styles[2].shadow && styles[2].border && styles[2].shadowRange == 3 && styles[2].shadowColor == RGB(0,0,255), "all qualified style properties");
	spans.overflow = TRUE;
	test_check(!TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4) && !styles[2].setMask, "overflow clears and falls back");
	spans.overflow = FALSE;
	spans.items[0].length = 0;
	test_check(TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 4), "empty span retained");
	spans.items[0].length = 2;
	test_check(!TcFormatStyleResolveFrame(&ruleset, text, 11, &spans, styles, 3), "insufficient output capacity rejected");
}

static void test_frame_compatibility(void)
{
	static TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FORMAT_SPANS spans;
	TC_FS_STYLE overlays[1], expected, actual, base = test_base();
	WCHAR ruleText[TC_FS_RULE_CCH];
	WCHAR property[64];
	const WCHAR* values[] = { L"95", L"80", L"69", L"N/A" };
	WCHAR section[] = L"Enabled=1\0Rule10=if (CU.value >= 90) { CU.ForeColor=red; } else if (CU.value >= 70) { CU.ForeColor=orange; } else { CU.ForeColor=blue; }\0Rule2=if (true) { CU.Bold=1; }\0\0";
	int i;
	ZeroMemory(&spans, sizeof(spans));
	spans.count = 1;
	lstrcpyW(spans.items[0].selector, L"CU");
	test_check(TcFormatStyleLoadMulti(section, (int)ARRAYSIZE(section), &ruleset, &report), "legacy frame section");
	for (i = 0; i < (int)ARRAYSIZE(values); i++) {
		int length = lstrlenW(values[i]);
		spans.items[0].length = length;
		TcFormatStyleApply(&ruleset, L"CU", values[i], length, &base, &expected, NULL);
		test_check(TcFormatStyleResolveFrame(&ruleset, values[i], length, &spans, overlays, 1), "legacy frame resolution");
		actual = base;
		TcFormatStyleMerge(&actual, &overlays[0]);
		test_check(memcmp(&actual, &expected, sizeof(actual)) == 0, "legacy frame equals single-item evaluation");
	}
	lstrcpyW(ruleText, L"if (true) { ");
	for (i = 2; i <= TC_FS_MAX_TARGETS + 1; i++) {
		swprintf_s(property, ARRAYSIZE(property), L"CUSTOM%d.Bold=1; ", i);
		wcscat_s(ruleText, ARRAYSIZE(ruleText), property);
	}
	wcscat_s(ruleText, ARRAYSIZE(ruleText), L"}");
	test_check(TcFormatStyleParseRule(ruleText, 1, &ruleset.rules[0], &report), "maximum destination groups accepted");
	ruleText[lstrlenW(ruleText)-1] = 0;
	wcscat_s(ruleText, ARRAYSIZE(ruleText), L"CUSTOM18.Bold=1; }");
	test_check(!TcFormatStyleParseRule(ruleText, 1, &ruleset.rules[0], &report), "destination overflow rejected");
}

static void test_value_input(WCHAR* text, char* info, TC_FORMAT_SPANS* spans)
{
	lstrcpyW(text, L"100|");
	memset(info, 1, 4);
	info[4] = 0;
	ZeroMemory(spans, sizeof(*spans));
	spans->count = 2;
	spans->items[0].length = 3;
	spans->items[0].zone = 1;
	lstrcpyW(spans->items[0].selector, L"CU");
	spans->items[1].start = 4;
	spans->items[1].zone = 8;
	lstrcpyW(spans->items[1].selector, L"STATUS");
}

static void test_value_assignments(void)
{
	static TC_FS_RULESET ruleset;
	TC_FS_REPORT report;
	TC_FORMAT_SPANS spans, savedSpans;
	WCHAR text[128], savedText[128];
	char info[128], savedInfo[128];
	WCHAR section[] =
		L"Enabled=1\0"
		L"Rule1=if (CU.value == 100) { CU.value=\"MAX\"; CU.ForeColor=red; }\0"
		L"Rule2=if (CU.value == \"MAX\") { STATUS.value=\"High\"; STATUS.ForeColor=red; }\0"
		L"Rule3=if (STATUS.value == \"High\") { STATUS.value=\"Busy\"; } else { STATUS.value=\"Repeated\"; }\0\0";
	test_check(TcFormatStyleLoadMulti(section, (int)ARRAYSIZE(section), &ruleset, &report), "value rules load");
	test_value_input(text, info, &spans);
	test_check(TcFormatStyleTransform(&ruleset, text, 128, info, &spans), "value transform");
	test_check(wcscmp(text, L"MAX|Busy") == 0 && spans.items[1].length == 4, "self write, later read and empty placement");
	test_check(spans.items[0].style.foreColor == RGB(255,0,0) && spans.items[1].style.foreColor == RGB(255,0,0), "styles retained through replacement");
	test_check(info[3] == 1 && info[4] == 8 && info[7] == 8 && info[8] == 0, "replacement zones and literal separator");
	test_check(TcFormatStyleTransform(&ruleset, text, 128, info, &spans) && wcscmp(text, L"MAX|Busy") == 0, "cached frame never reruns assignments");
	test_value_input(text, info, &spans);
	text[0] = L'0';
	TcFormatStyleTransform(&ruleset, text, 128, info, &spans);
	test_check(wcscmp(text, L"000|Repeated") == 0, "next frame starts with ordinary values");

	TcFormatStyleInit(&ruleset); ruleset.enabled = TRUE; ruleset.ruleCount = 3;
	TcFormatStyleParseRule(L"if (CU.value == 100) { MID.ForeColor=blue; MID.value=\"1\"; CU.value=\"50\"; }", 1, &ruleset.rules[0], &report);
	TcFormatStyleParseRule(L"if (MID.value == 1 && CU.value == 50) { MID.value=\"2\"; STATUS.value=\"virtual\"; }", 2, &ruleset.rules[1], &report);
	TcFormatStyleParseRule(L"if (CU.value == 50 && MID.value == 2) { CU.value=\"100\"; }", 3, &ruleset.rules[2], &report);
	test_value_input(text, info, &spans);
	test_check(TcFormatStyleTransform(&ruleset, text, 128, info, &spans) && wcscmp(text, L"100|virtual") == 0, "virtual anchor, cache invalidation and mutual writes execute once");
	test_check(spans.count == 2, "unplaced virtual item creates no screen location");

	ruleset.ruleCount = 1;
	TcFormatStyleParseRule(L"if (true) { CU.value=\"\"; STATUS.value=\"<%CU%>\"; }", 1, &ruleset.rules[0], &report);
	test_value_input(text, info, &spans);
	TcFormatStyleTransform(&ruleset, text, 128, info, &spans);
	test_check(wcscmp(text, L"|<%CU%>") == 0 && spans.items[0].length == 0 && spans.items[1].start == 1, "clear and literal nonrecursive replacement");

	TcFormatStyleParseRule(L"if (true) { CU.value=\"\"; STATUS.value=\"\"; }", 1, &ruleset.rules[0], &report);
	test_value_input(text, info, &spans);
	text[3] = 0; spans.items[1].start = 3;
	TcFormatStyleTransform(&ruleset, text, 128, info, &spans);
	test_check(!text[0] && !info[0] && spans.items[0].length == 0 && spans.items[1].length == 0, "all-empty frame");

	TcFormatStyleParseRule(L"if (true) { STATUS.value=\"A\\n\xD83D\xDE00\"; }", 1, &ruleset.rules[0], &report);
	test_value_input(text, info, &spans);
	TcFormatStyleTransform(&ruleset, text, 128, info, &spans);
	test_check(wcscmp(text, L"100|A\n\xD83D\xDE00") == 0 && spans.items[1].length == 4, "newline and surrogate pair preserved");

	TcFormatStyleParseRule(L"if (true) { CU.value=\"expanded\"; STATUS.value=\"overflow\"; }", 1, &ruleset.rules[0], &report);
	test_value_input(text, info, &spans);
	CopyMemory(savedText, text, sizeof(text)); CopyMemory(savedInfo, info, sizeof(info)); savedSpans = spans;
	test_check(!TcFormatStyleTransform(&ruleset, text, 8, info, &spans), "output overflow rejected");
	test_check(memcmp(text, savedText, sizeof(text)) == 0 && memcmp(info, savedInfo, sizeof(info)) == 0 && memcmp(&spans, &savedSpans, sizeof(spans)) == 0, "output overflow leaves entire original frame intact");

	TcFormatStyleParseRule(L"if (true) { CUSTOM1.value=\"override\"; }", 1, &ruleset.rules[0], &report);
	test_value_input(text, info, &spans); lstrcpyW(spans.items[1].selector, L"CUSTOM1");
	lstrcpyW(text, L"100|configured"); spans.items[1].length = 10;
	TcFormatStyleTransform(&ruleset, text, 128, info, &spans);
	test_check(wcscmp(text, L"100|override") == 0, "configured CUSTOM is overridden");
	test_value_input(text, info, &spans); lstrcpyW(spans.items[1].selector, L"CUSTOM1");
	TcFormatStyleTransform(&ruleset, text, 128, info, &spans);
	test_check(wcscmp(text, L"100|override") == 0, "unconfigured CUSTOM is populated");

	test_check(!TcFormatStyleParseRule(L"if (true) { CU.value=\"a\"; CU.value=\"b\"; }", 1, &ruleset.rules[0], &report), "duplicate self value rejected");
	ruleset.ruleCount = 2;
	TcFormatStyleParseRule(L"if (true) { true.value=\"ok\"; }", 1, &ruleset.rules[0], &report);
	test_check(TcFormatStyleParseRule(L"if (true.value == \"ok\") { STATUS.value=\"named\"; }", 2, &ruleset.rules[1], &report), "qualified boolean-word name accepted");
	test_value_input(text, info, &spans);
	TcFormatStyleTransform(&ruleset, text, 128, info, &spans);
	test_check(wcscmp(text, L"100|named") == 0, "boolean literal and qualified name are distinct");
	test_check(TcFormatStyleIdentifier(L"STATUS_1") && !TcFormatStyleIdentifier(L"1STATUS") && !TcFormatStyleIdentifier(L"Bad-Name"), "named identifier grammar");
}

int main(void)
{
	test_value_assignments();
	test_frame_compatibility();
	test_cross_items();
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
