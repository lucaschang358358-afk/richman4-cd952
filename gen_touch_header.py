#!/usr/bin/env python3
"""
Auto-detects touchscreen/pen event APIs from installed Muteki SDK headers
and emits muteki_touch_adapt.h so compilation succeeds across SDK versions.
"""
import os
import re
import sys

out_path = sys.argv[1] if len(sys.argv) > 1 else "muteki_touch_adapt.h"

search_roots = ["/usr/lib/sdk/muteki", "/usr/include", "/tmp/mock_muteki"]
header_files = []
for root in search_roots:
    if os.path.isdir(root):
        for dirpath, _, filenames in os.walk(root):
            if "muteki" in dirpath.replace("\\", "/"):
                for fn in filenames:
                    if fn.endswith(".h") and any(k in fn.lower() for k in ("event", "common", "ui", "input", "touch", "pen")):
                        header_files.append(os.path.join(dirpath, fn))

all_text = ""
for hf in sorted(set(header_files)):
    try:
        with open(hf, "r", encoding="utf-8", errors="ignore") as f:
            all_text += f"\n/* FILE: {hf} */\n" + f.read()
    except OSError:
        pass

clean = re.sub(r"/\*.*?\*/", " ", all_text, flags=re.S)
clean = re.sub(r"//[^\n]*", " ", clean)

def extract_braced_body(text, start_pattern):
    bodies = []
    for m in re.finditer(start_pattern, text):
        i = m.end() - 1
        if i < 0 or text[i] != "{":
            continue
        depth = 0
        start = i + 1
        for j in range(i, len(text)):
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
                if depth == 0:
                    bodies.append(text[start:j])
                    break
    return "\n".join(bodies)

has_test_pend = bool(re.search(r"\bTestPendEvent\s*\(", clean))
has_test_touch = bool(re.search(r"\bTestTouchEvent\s*\(", clean))
has_test_pen = bool(re.search(r"\bTestPenEvent\s*\(", clean))

enum_names = set(re.findall(r"\bUI_EVENT_TYPE_[A-Z0-9_]+\b", clean))
down_candidates = [
    "UI_EVENT_TYPE_TOUCH_DOWN", "UI_EVENT_TYPE_PEN_DOWN", "UI_EVENT_TYPE_PENDOWN",
    "UI_EVENT_TYPE_TOUCH_PRESS", "UI_EVENT_TYPE_PEN_PRESS",
    "UI_EVENT_TYPE_TOUCH", "UI_EVENT_TYPE_PEN", "UI_EVENT_TYPE_TAP",
    "UI_EVENT_TYPE_STYLUS_DOWN", "UI_EVENT_TYPE_POINTER_DOWN"
]
up_candidates = [
    "UI_EVENT_TYPE_TOUCH_UP", "UI_EVENT_TYPE_PEN_UP", "UI_EVENT_TYPE_PENUP",
    "UI_EVENT_TYPE_TOUCH_RELEASE", "UI_EVENT_TYPE_PEN_RELEASE",
    "UI_EVENT_TYPE_STYLUS_UP", "UI_EVENT_TYPE_POINTER_UP"
]
down_types = [e for e in down_candidates if e in enum_names]
up_types = [e for e in up_candidates if e in enum_names]
if not down_types:
    for e in sorted(enum_names):
        if ("TOUCH" in e or "PEN" in e) and not any(x in e for x in ("UP", "MOVE", "DRAG", "RELEASE", "LONG")):
            down_types.append(e)

struct_body = ""
for pat in [r"struct\s+ui_event_base_s\s*\{", r"struct\s+ui_event_s\s*\{", r"union\s+ui_event_u\s*\{"]:
    b = extract_braced_body(clean, pat)
    if b:
        struct_body += "\n" + b

has_body_member = bool(re.search(r"\bbody\s*;", struct_body))
prefix = "(ev).body." if has_body_member else "(ev)."

x_expr = None
y_expr = None

nested_m = re.search(r"struct\s*[a-zA-Z0-9_]*\s*\{[^}]*\bx\b[^}]*\by\b[^}]*\}\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*;", struct_body)
if nested_m:
    nm = nested_m.group(1)
    x_expr = f"{prefix}{nm}.x"
    y_expr = f"{prefix}{nm}.y"

if not x_expr:
    flat_pairs = [
        ("touch_x", "touch_y"),
        ("pen_x", "pen_y"),
        ("x", "y"),
        ("pos_x", "pos_y"),
        ("coord_x", "coord_y"),
        ("touch_pos_x", "touch_pos_y"),
        ("key_code0", "key_code1"),
    ]
    for fx, fy in flat_pairs:
        if re.search(r"\b" + fx + r"\b", struct_body) and re.search(r"\b" + fy + r"\b", struct_body):
            if fx == "key_code0" and not down_types:
                continue
            x_expr = f"{prefix}{fx}"
            y_expr = f"{prefix}{fy}"
            break

test_parts = []
if has_test_pend:
    test_parts.append("TestPendEvent(evp)")
if has_test_touch:
    test_parts.append("TestTouchEvent(evp)")
if has_test_pen:
    test_parts.append("TestPenEvent(evp)")

can_touch = bool(down_types and x_expr and y_expr and test_parts)
print(f"[gen_touch_header] headers={len(header_files)} test={test_parts} down={down_types} up={up_types} xy=({x_expr},{y_expr}) enabled={can_touch}")

with open(out_path, "w", encoding="utf-8") as out:
    out.write("#ifndef MUTEKI_TOUCH_ADAPT_H\n#define MUTEKI_TOUCH_ADAPT_H\n\n")
    if can_touch:
        test_expr = " || ".join(test_parts)
        down_expr = " || ".join(f"({prefix}event_type == {t})" for t in down_types)
        up_expr = " || ".join(f"({prefix}event_type == {t})" for t in up_types) if up_types else "0"
        out.write("#define MUTEKI_HAS_TOUCH 1\n")
        out.write(f"#define MUTEKI_TEST_TOUCH(evp) ({test_expr})\n")
        out.write(f"#define MUTEKI_IS_TOUCH_DOWN(ev) ({down_expr})\n")
        out.write(f"#define MUTEKI_IS_TOUCH_UP(ev) ({up_expr})\n")
        out.write(f"#define MUTEKI_TOUCH_X(ev) ((int)({x_expr}))\n")
        out.write(f"#define MUTEKI_TOUCH_Y(ev) ((int)({y_expr}))\n")
    else:
        out.write("#define MUTEKI_HAS_TOUCH 0\n")
        out.write("#define MUTEKI_TEST_TOUCH(evp) (0)\n")
        out.write("#define MUTEKI_IS_TOUCH_DOWN(ev) (0)\n")
        out.write("#define MUTEKI_IS_TOUCH_UP(ev) (0)\n")
        out.write("#define MUTEKI_TOUCH_X(ev) (0)\n")
        out.write("#define MUTEKI_TOUCH_Y(ev) (0)\n")
    out.write("\n#endif\n")
