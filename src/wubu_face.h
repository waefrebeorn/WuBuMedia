#ifndef WUBU_FACE_H
#define WUBU_FACE_H

/* wubu_face.h — C11 face state writer for the cohost avatar.
 *
 * Writes face_state.json for the OBS browser source overlay.
 * License: WaefreBeorn-UMV3
 */
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FaceImpl Face;

/* Create a face state writer. face_dir is the path to the face/ directory. */
Face *wubu_face_create(const char *face_dir, void *cohost);

/* Destroy. Safe with NULL. */
void wubu_face_destroy(Face *f);

/* Update face state. text drives visemes, mood sets emotion,
 * speaking toggles the speaking flag. */
int wubu_face_update(Face *f, const char *text, const char *mood, int speaking);

/* Simulate a poke interaction. */
int wubu_face_poke(Face *f, int power);

/* Simulate a fling interaction. */
int wubu_face_fling(Face *f, int power);

/* Get current mood/energy. */
double wubu_face_get_mood(Face *f);
double wubu_face_get_energy(Face *f);

#ifdef __cplusplus
}
#endif

#endif /* WUBU_FACE_H */
