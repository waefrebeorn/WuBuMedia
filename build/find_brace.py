#!/usr/bin/env python3
"""Find unmatched braces in C source."""
import sys, re

with open(sys.argv[1]) as f:
    code = f.read()

# Use preprocessor to expand, but we work on raw file
with open(sys.argv[1]) as f:
    lines = f.readlines()

in_string = False
in_char = False
in_line_comment = False
in_block_comment = False
depth = 0
prev = ''
brace_lines = []  # track all { and } line numbers

for line_num, line_text in enumerate(lines, 1):
    for c in line_text:
        if c == '\n':
            in_line_comment = False
            prev = ''
            continue
        if in_line_comment:
            continue
        if in_block_comment:
            if prev == '*' and c == '/':
                in_block_comment = False
            prev = c
            continue
        if in_string:
            if c == '"' and (prev != '\\' or (prev == '\\' and c == '"' and len(brace_lines) > 0)):
                # Check for escaped quote
                pass
            if c == '"' and prev != '\\':
                in_string = False
            prev = c
            continue
        if in_char:
            if c == "'" and prev != '\\':
                in_char = False
            prev = c
            continue
        
        if prev == '/' and c == '/':
            in_line_comment = True
            prev = ''
            continue
        if prev == '/' and c == '*':
            in_block_comment = True
            prev = c
            continue
        
        if c == '"':
            in_string = True
            prev = ''
            continue
        if c == "'":
            in_char = True
            prev = ''
            continue
        if c == '{':
            depth += 1
            brace_lines.append((line_num, '{', depth))
        elif c == '}':
            depth -= 1
            brace_lines.append((line_num, '}', depth))
        prev = c

print(f"Final depth: {depth}")
if depth > 0:
    print(f"Missing {depth} closing brace(s)")
    # Find where depth never returns to 0
    for ln, ch, d in reversed(brace_lines):
        if d > 0:
            print(f"  Last open brace at line {ln} (depth={d})")
            break
