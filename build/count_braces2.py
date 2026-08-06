#!/usr/bin/env python3
import sys

with open(sys.argv[1]) as f:
    lines = f.readlines()

in_string = False
in_char = False
in_line_comment = False
in_block_comment = False
depth = 0
prev = ''

for line_num, line_text in enumerate(lines, 1):
    line = line_text
    for c in line:
        if c == '\n':
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
        prev = c
    
    if depth > 0 and line_num >= 195:
        print(f"Line {line_num}: depth={depth} ({line_text.rstrip()[:80]})")
