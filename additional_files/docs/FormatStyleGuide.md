# Conditional format styles and display values

Conditional rules run after ordinary value generation and before final layout/drawing. They can change both text and appearance. Add rules under `[FormatStyle]`; keep a backup of your settings before editing.

## Conditional messages without Custom Var setup

In the main display format, place the output name where the message should appear:

```text
CU"% "<%STATUS%>
```

Then add rules:

```ini
[FormatStyle]
Enabled=1
Rule1=if (CU.value >= 90) { STATUS.value="High"; STATUS.ForeColor=red; } else { STATUS.value="Normal"; STATUS.ForeColor=green; }
```

`STATUS` needs no acquisition configuration. Names use ASCII letters, digits, and underscore, start with a letter or underscore, and have at most 31 characters. Names are case-sensitive. Existing built-in tokens and recognized compound formats retain their meanings; use a distinct name for a new message field. An unassigned named placeholder is empty. Repeated placements receive assignments together.

The main format continues to interpret ordinary clock tokens. Quote literal labels and separators, as in `"CPU: "CU" "<%STATUS%>`. Explicit `<%NAME%>` boundaries are retained for placeholders; recognized legacy compound formats such as `<%hhnnss%>` still display the time.

## Reading and assigning values

Every value reference and assignment names its item explicitly. A rule starts with `if`; the old leading selector syntax and unqualified properties are invalid.

- `CU.value` reads the current CU text. `CU.value="MAX"` replaces it.
- `CU.ForeColor=red` styles CU. `STATUS.ForeColor=red` styles STATUS.
- Qualification works with `ForeColor`, `Font`, `FontSize`, `Bold`, `Italic`, `ForeColorShadow`, `ForeColorBorder`, `ShadowColor`, and `ClockShadowRange`. Existing value ranges and color notation are unchanged.

Value assignments accept quoted string literals, including `""` to hide an item's text. They support escaped quotes/backslashes and `\n`, `\r`, `\t`. Assigned content is literal display text: an assigned `<%CU%>` is not expanded again. Surrounding literal separators are not removed when an item becomes empty.

```ini
Rule1=if (CU.value == 100) { CU.value="MAX"; CU.ForeColor=red; }
Rule2=if (CU.value == "MAX") { STATUS.value="Full load"; STATUS.Bold=1; }
```

Rule2 sees `MAX`. Self-assignment is allowed. Rules run once in numeric RuleN order, with no backward jump or repeated evaluation. On the next update, ordinary values are generated again, and the same one-pass rules run anew. Later assignments override earlier ones for that update only.

CUSTOM values can be overwritten whether or not Custom Var is configured:

```ini
Rule3=if (CU.value == "MAX") { CUSTOM1.value="Busy"; } else { CUSTOM1.value=""; }
```

Configured values supply only the initial text. Conditional assignments have final display priority and do not change saved settings, external files, or the source measurement.

## Virtual intermediate values

Assignments can create names with no screen placement. Later rules may read those values; no text is appended at an arbitrary screen position.

```ini
Rule1=if (CU.value >= 90) { LEVEL.value="high"; } else { LEVEL.value="normal"; }
Rule2=if (LEVEL.value == "high") { STATUS.value="Check CPU"; STATUS.ForeColor=red; }
```

Only place `<%STATUS%>` in the display format if LEVEL is intended as an intermediate value. A created unplaced value has one occurrence and lasts only for the current update.

## Conditions, missing items, and ordering

Numeric comparisons support `==`, `!=`, `<`, `<=`, `>`, and `>=`; string comparisons support `==` and `!=`. Combine conditions with `&&`, `||`, `!`, and parentheses. Comparison right operands remain numeric or quoted string literals. Arithmetic, interpolation, value-copy expressions, and direct item-to-item comparisons are not supported.

Each if/else-if/else chain applies only its first matching branch. Each rule is evaluated once per display frame. Conditions read the current working values, including earlier assignments. Numeric conversion accepts surrounding whitespace but not unit suffixes. Empty or nonnumeric values fail numeric conversion; string comparison is exact.

A missing reference, a reference to a name with several occurrences, or a numeric conversion error aborts that rule without selecting `else`. Short-circuited operands are not evaluated. A placed empty item remains present and can be compared to `""` or populated later.

A branch can create and style a name together, regardless of the order of those declarations. A style-only write to a missing, unplaced, uncreated name prevents that branch from applying; other rules continue. Duplicate assignments to the same destination/property within a branch are rejected.

Writes to a name affect all its displayed occurrences. Later rules win for conflicting properties. New virtual items are available to later rules; they never cause the current rule to restart.

Text and styles are resolved before layout, so a destination may appear before a condition item. Only properties explicitly assigned are overridden. GDI and WinUI receive the final text and its matching style ranges; a cached final frame does not run assignments a second time.

## Limits and fallback

Limits are 32 rules, 8 branches and 64 condition nodes per rule, and 16 destination groups across a rule's branches. A destination reused in one branch shares a group; reuse in another branch consumes another. Rules have a 1024-character limit and a shared 512-WCHAR pool for string values/references. Individual assigned strings must fit that pool along with the rule's other strings.

Frames support 256 placed items and up to 512 working items including virtual values. Final text must fit the caller's buffer (4096 UTF-16 code units for the main clock, including the terminator). Empty placements retain their order. If frame capacity/allocation fails, the original generated text is retained and ordinary rendering is used; partially reconstructed output is never published. Invalid rules are rejected.
