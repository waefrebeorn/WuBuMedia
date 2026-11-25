/*
 * wubu_core.c — the ~ WuBu ~ companion core.
 *
 * What WuBuMedia promises (persona spec, wubu_bonzi.c): an emotion state
 * machine, an empathy engine, and a mood->prosody mapper. This is that, written
 * from scratch in strict C11 with no dependencies, small enough to audit in one
 * sitting and honest enough to print its own state.
 *
 *   build:  gcc -std=c11 -O2 -Wall -Wextra -o wubu_core wubu_core.c
 *   use:    ./wubu_core state
 *           ./wubu_core hear "<what someone said>"
 *           ./wubu_core say  "<what wubu says>"
 *           ./wubu_core tick <seconds>
 *           ./wubu_core ledger
 *
 * State lives in wubu_state.bin next to the binary (fixed-size struct, no
 * parser, no database). Every utterance is appended to wubu_ledger.jsonl so the
 * page can show what it actually said and when.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAGIC 0x57554255u /* "WUBU" */
#define VERSION 1
#define RING 32
#define LINELEN 512

enum mood { M_IDLE = 0, M_WORKING, M_PLEASED, M_TIRED, M_SHARP, M_QUIET, M_COUNT };
static const char *MOOD_NAME[M_COUNT] = {"idle", "working", "pleased", "tired", "sharp", "quiet"};

typedef struct {
    char text[LINELEN];
    char mood[16];
    uint64_t at;
    int valence;
} Turn;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t mood;
    int8_t energy;      /* 0..100 : how much is left in the box today     */
    int8_t warmth;      /* 0..100 : how much it is giving a stranger      */
    int8_t patience;    /* 0..100 : how much repetition it will absorb    */
    uint32_t turns;     /* total turns since birth                        */
    uint32_t head;      /* ring write index                               */
    uint64_t born;
    uint64_t last;
    Turn ring[RING];
} State;

static State S;

static uint64_t now_s(void) { return (uint64_t)time(NULL); }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void save(void) {
    FILE *f = fopen("wubu_state.bin", "wb");
    if (!f) { perror("wubu_state.bin"); return; }
    fwrite(&S, sizeof S, 1, f);
    fclose(f);
}

static void load(void) {
    FILE *f = fopen("wubu_state.bin", "rb");
    if (f) {
        if (fread(&S, sizeof S, 1, f) == 1 && S.magic == MAGIC && S.version == VERSION) {
            fclose(f);
            return;
        }
        fclose(f);
    }
    memset(&S, 0, sizeof S);
    S.magic = MAGIC;
    S.version = VERSION;
    S.mood = M_IDLE;
    S.energy = 72;
    S.warmth = 60;
    S.patience = 80;
    S.born = now_s();
    S.last = S.born;
}

/* ---------------------------------------------------------------- empathy */
/* A deliberately small sentiment read: praise, hostility, repetition, need.
   This is a keyword grader, not a model — and it says so. */
typedef struct { int valence; int praised; int hostile; int needy; int tired; } Read;

static int has(const char *h, const char *n) { return strstr(h, n) != NULL; }

static Read read_input(const char *s) {
    Read r = {0, 0, 0, 0, 0};
    char low[LINELEN];
    size_t n = strlen(s), i;
    for (i = 0; i < n && i < LINELEN - 1; i++) {
        char c = s[i];
        low[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    low[i] = 0;

    if (has(low, "thank") || has(low, "good") || has(low, "nice") || has(low, "love") ||
        has(low, "great") || has(low, "beautiful") || has(low, "works")) r.praised = 1;
    if (has(low, "stupid") || has(low, "fake") || has(low, "lie") || has(low, "shit") ||
        has(low, "useless") || has(low, "bot") || has(low, "hate")) r.hostile = 1;
    if (has(low, "?") || has(low, "help") || has(low, "how do") || has(low, "where"))
        r.needy = 1;
    if (has(low, "tired") || has(low, "sleep") || has(low, "again") || has(low, "repeat"))
        r.tired = 1;

    r.valence = (r.praised ? 2 : 0) - (r.hostile ? 2 : 0);
    return r;
}

/* ---------------------------------------------------------------- state machine */
static void transition(Read r, int heard) {
    /* energy drains with use and recovers on the tick; patience drains on
       repetition and on hostility; warmth follows praise and courtesy. */
    if (heard) {
        S.energy = (int8_t)clampi(S.energy - 1, 0, 100);
        if (r.tired) S.patience = (int8_t)clampi(S.patience - 6, 0, 100);
        if (r.hostile) { S.patience = (int8_t)clampi(S.patience - 10, 0, 100);
                         S.warmth = (int8_t)clampi(S.warmth - 4, 0, 100);
                         S.mood = M_SHARP; }
        if (r.praised) { S.warmth = (int8_t)clampi(S.warmth + 8, 0, 100);
                         S.mood = M_PLEASED; }
        if (r.needy && !r.hostile) S.mood = (S.energy > 35) ? M_WORKING : M_TIRED;
        if (S.patience < 25) S.mood = M_QUIET;
    }
}

static void tick(int secs) {
    int gain = secs / 60;
    if (gain < 1) gain = 1;
    S.energy = (int8_t)clampi(S.energy + gain, 0, 100);
    S.patience = (int8_t)clampi(S.patience + gain / 2, 0, 100);
    if (S.mood == M_TIRED && S.energy > 55) S.mood = M_IDLE;
    if (S.mood == M_SHARP && S.patience > 60) S.mood = M_IDLE;
    if (S.mood == M_QUIET && S.patience > 45) S.mood = M_IDLE;
    if (!S.mood) S.mood = M_IDLE;
    S.last = now_s();
}

/* mood -> prosody: how the same sentence would be said */
static void prosody(char *out, size_t n) {
    int rate = 1, pitch = 0, energy = 50;
    switch (S.mood) {
        case M_PLEASED: rate = 4; pitch = 2; energy = 70; break;
        case M_WORKING: rate = 3; pitch = 0; energy = 62; break;
        case M_TIRED:   rate = -1; pitch = -2; energy = 28; break;
        case M_SHARP:   rate = 2; pitch = 1; energy = 66; break;
        case M_QUIET:   rate = -2; pitch = -1; energy = 20; break;
        default:        rate = 0; pitch = 0; energy = 45; break;
    }
    /* +-3 steps on a 5-step scale, so the numbers mean something */
    snprintf(out, n, "{\"rate\":%d,\"pitch\":%d,\"energy\":%d,\"warmth\":%d,\"patience\":%d}",
             rate, pitch, energy, S.warmth, S.patience);
}

/* ---------------------------------------------------------------- ledger */
static void ledger(const char *kind, const char *text, int valence) {
    FILE *f = fopen("wubu_ledger.jsonl", "a");
    if (!f) return;
    char esc[LINELEN * 2];
    size_t j = 0;
    for (size_t i = 0; text[i] && j < sizeof esc - 2; i++) {
        char c = text[i];
        if (c == '"' || c == '\\') esc[j++] = '\\';
        if (c == '\n') { esc[j++] = '\\'; c = 'n'; }
        esc[j++] = c;
    }
    esc[j] = 0;
    fprintf(f, "{\"t\":%llu,\"kind\":\"%s\",\"mood\":\"%s\",\"valence\":%d,\"text\":\"%s\"}\n",
            (unsigned long long)now_s(), kind, MOOD_NAME[S.mood], valence, esc);
    fclose(f);
}

static void push_turn(const char *text, int valence) {
    Turn *t = &S.ring[S.head % RING];
    snprintf(t->text, sizeof t->text, "%s", text);
    snprintf(t->mood, sizeof t->mood, "%s", MOOD_NAME[S.mood]);
    t->at = now_s();
    t->valence = valence;
    S.head++;
    S.turns++;
}

int main(int argc, char **argv) {
    load();
    const char *cmd = argc > 1 ? argv[1] : "state";
    const char *arg = argc > 2 ? argv[2] : "";

    if (!strcmp(cmd, "hear")) {
        Read r = read_input(arg);
        transition(r, 1);
        push_turn(arg, r.valence);
        S.last = now_s();
        char p[256];
        prosody(p, sizeof p);
        printf("{\"heard\":true,\"mood\":\"%s\",\"valence\":%d,\"prosody\":%s,\"turns\":%u}\n",
               MOOD_NAME[S.mood], r.valence, p, S.turns);
        ledger("hear", arg, r.valence);
        save();
        return 0;
    }
    if (!strcmp(cmd, "say")) {
        push_turn(arg, 0);
        S.energy = (int8_t)clampi(S.energy - 1, 0, 100);
        printf("{\"said\":true,\"mood\":\"%s\",\"turns\":%u}\n", MOOD_NAME[S.mood], S.turns);
        ledger("say", arg, 0);
        save();
        return 0;
    }
    if (!strcmp(cmd, "tick")) {
        tick(atoi(arg[0] ? arg : "60"));
        save();
        printf("{\"ticked\":true,\"mood\":\"%s\",\"energy\":%d}\n", MOOD_NAME[S.mood], S.energy);
        return 0;
    }
    if (!strcmp(cmd, "ledger")) {
        FILE *f = fopen("wubu_ledger.jsonl", "r");
        if (!f) { printf("{\"ledger\":\"empty\"}\n"); return 0; }
        char line[LINELEN * 2];
        while (fgets(line, sizeof line, f)) fputs(line, stdout);
        fclose(f);
        return 0;
    }
    /* state */
    char p[256];
    prosody(p, sizeof p);
    printf("{\"name\":\"wubu\",\"version\":%d,\"mood\":\"%s\",\"energy\":%d,\"warmth\":%d,"
           "\"patience\":%d,\"turns\":%u,\"born\":%llu,\"last\":%llu,\"prosody\":%s}\n",
           VERSION, MOOD_NAME[S.mood], S.energy, S.warmth, S.patience, S.turns,
           (unsigned long long)S.born, (unsigned long long)S.last, p);
    return 0;
}
