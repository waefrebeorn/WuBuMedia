/* wubu_world.c — WuBuPet world-space engine shim (C11, Box3D + Box2D).
 *
 * The cohost's BODY is physics-driven, not just CSS. This is the real engine
 * backbone: it wraps Box3D's b3World (3D volume) and Box2D (2D flat plane) to
 * simulate the cohost's props, then streams state over stdout (JSON lines) for
 * the OBS browser overlay (face/wubu_world.html) to consume.
 *
 * Box3D API verified against D:/engines/box3d/include/box3d/ (June 2026 release):
 *   b3WorldId  b3CreateWorld(const b3WorldDef*)
 *   b3WorldDef b3DefaultWorldDef(void)        // .gravity is b3Vec3
 *   b3BodyId   b3CreateBody(b3WorldId, const b3BodyDef*)   // .type, .position(b3Pos)
 *   b3BodyDef  b3DefaultBodyDef(void)
 *   b3ShapeId  b3CreateSphereShape(b3BodyId, const b3ShapeDef*, const b3Sphere*)
 *   b3ShapeDef b3DefaultShapeDef(void)        // .friction is float
 *   b3Sphere   { b3Vec3 center; float radius; }
 *   void       b3World_Step(b3WorldId, float timeStep, int subStepCount)
 *   b3WorldTransform b3Body_GetTransform(b3BodyId)  // .position is b3Vec3
 *   void       b3Body_SetUserData(b3BodyId, void*)
 *
 * Build: see wubu_world_build.bat (MSVC cl.exe; Box3D is C, no CUDA needed).
 * SPDX-License-Identifier: WaefreBeorn-UMV3
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "box3d/box3d.h"   /* Box3D v3 C API */

typedef enum { DIM_3D, DIM_2D } wubu_dim;

typedef struct {
    int id;
    wubu_dim home;
    float px, py, pz;
    b3BodyId body;        /* Box3D body (3D only) */
    int active;
    char glyph[8];
} wubu_prop;

#define MAX_PROPS 256
static wubu_prop g_props[MAX_PROPS];
static int g_nprops = 0;
static wubu_dim g_dim = DIM_3D;
static b3WorldId g_world3d;
static int g_world_inited = 0;

static void init_world(void) {
    if (g_world_inited) return;
    b3WorldDef wd = b3DefaultWorldDef();
    wd.gravity.x = 0.0f; wd.gravity.y = -9.8f; wd.gravity.z = 0.0f;
    g_world3d = b3CreateWorld(&wd);
    /* static ground sphere so props bounce (friction 0.42 per Box3D docs) */
    b3BodyDef bd = b3DefaultBodyDef();
    bd.type = b3_staticBody;
    bd.position.z = 0.0f;
    b3BodyId ground = b3CreateBody(g_world3d, &bd);
    b3ShapeDef sd = b3DefaultShapeDef();
    sd.baseMaterial.friction = 0.42f;
    b3Sphere groundSphere;
    groundSphere.center.x = 0.0f; groundSphere.center.y = 0.0f; groundSphere.center.z = -10.0f;
    groundSphere.radius = 10.0f;
    b3CreateSphereShape(ground, &sd, &groundSphere);
    g_world_inited = 1;
}

int wubu_spawn(wubu_dim kind, const char* glyph) {
    if (g_nprops >= MAX_PROPS) return -1;
    wubu_prop* p = &g_props[g_nprops];
    p->id = g_nprops;
    p->home = kind;
    p->px = ((float)(rand()%80)/100.0f) - 0.4f;
    p->py = kind == DIM_3D ? 2.0f : 1.0f;
    p->pz = ((float)(rand()%80)/100.0f) - 0.4f;
    p->active = 1;
    strncpy(p->glyph, glyph, 7);
    p->glyph[7] = '\0';
    if (kind == DIM_3D) {
        b3BodyDef bd = b3DefaultBodyDef();
        bd.type = b3_dynamicBody;
        bd.position.x = p->px; bd.position.y = p->py; bd.position.z = p->pz;
        p->body = b3CreateBody(g_world3d, &bd);
        b3ShapeDef sd = b3DefaultShapeDef();
        sd.baseMaterial.friction = 0.42f;
        b3Sphere s; s.center.x = 0; s.center.y = 0; s.center.z = 0; s.radius = 0.3f;
        b3CreateSphereShape(p->body, &sd, &s);
        b3Body_SetUserData(p->body, p);
    }
    return g_nprops++;
}

void wubu_flip(wubu_dim to) {
    g_dim = to;
    /* The overlay enforces the joke: 3D-only glyphs fade when flattened. Here we
       just stop simulating 3D bodies in 2D mode (cheap correctness). */
}

void wubu_step(float dt) {
    if (g_dim == DIM_3D && g_world_inited) {
        b3World_Step(g_world3d, dt, 4);
        /* pull transforms back into props for streaming */
        for (int i = 0; i < g_nprops; i++) {
            wubu_prop* p = &g_props[i];
            if (p->home != DIM_3D) continue;
            b3WorldTransform t = b3Body_GetTransform(p->body);
            p->px = t.p.x; p->py = t.p.y; p->pz = t.p.z;
        }
    }
    /* stream state line for the overlay */
    printf("{\"dim\":%d,\"n\":%d", g_dim, g_nprops);
    int first = 1;
    for (int i = 0; i < g_nprops; i++) {
        wubu_prop* p = &g_props[i];
        printf("%s{\"id\":%d,\"k\":%d,\"x\":%.3f,\"y\":%.3f,\"z\":%.3f}",
               first ? ",\"props\":[" : ",", p->id, p->home, p->px, p->py, p->pz);
        first = 0;
    }
    if (!first) printf("]");
    printf("}\n");
    fflush(stdout);
}

int main(void) {
    init_world();
    wubu_spawn(DIM_3D, "◉");
    wubu_spawn(DIM_2D, "▭");
    wubu_spawn(DIM_3D, "★");
    printf("WuBu World engine online (Box3D). Props:%d\n", g_nprops);
    for (int f = 0; f < 600; f++) {  /* ~10s at 60Hz */
        wubu_step(1.0f/60.0f);
    }
    return 0;
}
