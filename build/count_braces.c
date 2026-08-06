#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "r");
    if (!f) return 1;
    
    int c, prev = 0;
    int depth = 0;
    int line = 1;
    int in_string = 0, in_char = 0, in_line_comment = 0, in_block_comment = 0;
    
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            line++;
            in_line_comment = 0;
            prev = c;
            continue;
        }
        if (in_line_comment) { prev = c; continue; }
        if (in_block_comment) {
            if (prev == '*' && c == '/') in_block_comment = 0;
            prev = c;
            continue;
        }
        if (in_string) {
            if (c == '"' && prev != '\\') in_string = 0;
            prev = c;
            continue;
        }
        if (in_char) {
            if ((char)c == 0x27 && prev != '\\') in_char = 0;  /* 0x27 = single quote */
            prev = c;
            continue;
        }
        
        if (prev == '/' && c == '/') {
            in_line_comment = 1;
            prev = c;
            continue;
        }
        if (prev == '/' && c == '*') {
            in_block_comment = 1;
            prev = c;
            continue;
        }
        
        if (c == '"') { in_string = 1; prev = c; continue; }
        if ((char)c == 0x27) { in_char = 1; prev = c; continue; }
        
        if (c == '{') {
            depth++;
        }
        if (c == '}') {
            depth--;
        }
        printf("  %c at line %d, depth=%d\n", c, line, depth);
        
        prev = c;
    }
    
    printf("\nFinal depth: %d\n", depth);
    fclose(f);
    return 0;
}
