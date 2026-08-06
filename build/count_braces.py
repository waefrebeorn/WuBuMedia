#!/usr/bin/env python3
import sys

with open(sys.argv[1]) as f:
    code = f.read()

in_string = False
in_char = False
in_line_comment = False
in_block_comment = False
depth = 0
line = 1
prev = ''

for c in code:
    if c == '\n':
        line += 1
        in_line_comment = False
        prev = ''
        continue
    if in_line_comment:
        prev = c
        continue
    if in_block_comment:
        if prev == '*' and c == '/':
            in_block_comment = False
        prev = c
        continue
    if in_string:
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
    elif c == "'":
        in_char = True
    elif c == '{':
        depth += 1
    elif c == '}':
        depth -= 1
        if depth == 0:
            # Found function-level close
            pass
    prev = c

print(f"Final brace depth: {depth}")
