/* wubu_face.c — C11 face state writer for the cohost avatar.
 *
 * Replaces the Python wubu_cohost._publish_status() that writes
 * face_state.json for the OBS browser source.
 *
 * License: WaefreBeorn-UMV3
 * Build: cc -c wubu_face.c -o wubu_face.o -lsqlite3 -Wall -Wextra -std=c11
 */
#include "wubu_face.h"
#include "wubu_cohost.h"
#include "wubu_emotion.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

struct FaceImpl {
    char  *face_dir;
    double mood;
    double energy;
    int    speaking;
    int    viseme_idx;
    time_t last_update;
};

Face *wubu_face_create(const char *face_dir, void *cohost) {
    (void)cohost;  /* not used in C11 port */
    Face *f = (Face *)calloc(1, sizeof(Face));
    if (!f) return NULL;
    f->face_dir = face_dir ? strdup(face_dir) : strdup("face");
    f->mood = 0.5;
    f->energy = 0.3;
    f->speaking = 0;
    f->viseme_idx = 0;
    f->last_update = time(NULL);
    return f;
}

void wubu_face_destroy(Face *f) {
    if (!f) return;
    free(f->face_dir);
    free(f);
}

/* Write face_state.json atomically (write tmp, then rename) */
static int face_write_state(Face *f) {
    if (!f) return 0;
    char path[512];
    char tmp[512];
    snprintf(path, sizeof(path), "%s/face_state.json", f->face_dir);
    snprintf(tmp, sizeof(tmp), "%s/.face_state.tmp", f->face_dir);

    const char *mood_str;
    switch ((int)(f->mood * 6)) {
        case 0: mood_str = "sad"; break;
        case 1: mood_str = "neutral"; break;
        case 2: mood_str = "happy"; break;
        case 3: mood_str = "excited"; break;
        case 4: mood_str = "very_happy"; break;
        default: mood_str = "ecstatic"; break;
    }

    FILE *out = fopen(tmp, "w");
    if (!out) return 0;

    fprintf(out, "{\"mood\":\"%s\",\"text\":\"\",\"speaking\":%s,"
              "\"ts\":%.0f,\"mode\":\"live\",\"visemes\":\"%d\","
              "\"energy\":%.2f,\"mood_val\":%.2f}",
        mood_str,
        f->speaking ? "true" : "false",
        (double)f->last_update,
        f->viseme_idx,
        f->energy,
        f->mood);
    fclose(out);

#ifdef _WIN32
    _unlink(path);
    MoveFileA(tmp, path);
#else
    unlink(path);
    rename(tmp, path);
#endif
    return 1;
}

int wubu_face_update(Face *f, const char *text, const char *mood, int speaking) {
    if (!f) return 0;
    if (text) f->viseme_idx = (int)(strlen(text) % 5);
    f->speaking = speaking;
    f->last_update = time(NULL);
    if (mood) {
        if (strcmp(mood, "happy") == 0) f->mood = 0.8;
        else if (strcmp(mood, "excited") == 0) f->mood = 1.0;
        else if (strcmp(mood, "sad") == 0) f->mood = 0.2;
        else if (strcmp(mood, "angry") == 0) f->mood = 0.1;
        else if (strcmp(mood, "thinking") == 0) f->mood = 0.5;
        else f->mood = 0.5;
    }
    return face_write_state(f);
}

int wubu_face_poke(Face *f, int power) {
    if (!f) return 0;
    (void)power;
    f->mood = 0.1;  /* angry */
    f->energy = 1.0;
    f->last_update = time(NULL);
    return face_write_state(f);
}

int wubu_face_fling(Face *f, int power) {
    if (!f) return 0;
    (void)power;
    f->mood = 0.3;  /* dizzy/confused */
    f->energy = 0.8;
    f->last_update = time(NULL);
    return face_write_state(f);
}

double wubu_face_get_mood(Face *f) {
    return f ? f->mood : 0.5;
}

double wubu_face_get_energy(Face *f) {
    return f ? f->energy : 0.3;
}
