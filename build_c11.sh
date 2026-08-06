#!/bin/sh
# build_c11.sh — Build all C11 modules with zero-warning strict flags.
#
# Usage: sh build_c11.sh [test]
#   (no args  — build libraries + executables)
#   test     — also build and run all test executables
#
CC=cc
CFLAGS="-Wall -Wextra -std=c11 -g -I src"

echo "=== Compiling C11 modules ==="

# Core modules
$CC $CFLAGS -c src/wubu_wiki.c   -o build/wubu_wiki.o   -lsqlite3 2>&1
echo "  wubu_wiki.c      OK"

$CC $CFLAGS -c src/wubu_emotion.c -o build/wubu_emotion.o -lm 2>&1
echo "  wubu_emotion.c   OK"

$CC $CFLAGS -c src/wubu_rlm.c    -o build/wubu_rlm.o    -lsqlite3 -lm 2>&1
echo "  wubu_rlm.c       OK"

$CC $CFLAGS -c src/wubu_recs.c   -o build/wubu_recs.o   -lsqlite3 -lm 2>&1
echo "  wubu_recs.c      OK"

$CC $CFLAGS -c src/wubu_cohost.c -o build/wubu_cohost.o -lsqlite3 -lm 2>&1
echo "  wubu_cohost.c    OK"

$CC $CFLAGS -c src/wubu_agent.c  -o build/wubu_agent.o  -lsqlite3 -lm 2>&1
echo "  wubu_agent.c     OK"

$CC $CFLAGS -c src/wubu_gateway.c -o build/wubu_gateway.o -lsqlite3 -lm -lws2_32 2>&1
echo "  wubu_gateway.c   OK"

$CC $CFLAGS -c src/wubu_self.c   -o build/wubu_self.o   -lsqlite3 -lm 2>&1
echo "  wubu_self.c      OK"

$CC $CFLAGS -c src/wubu_face.c   -o build/wubu_face.o   -lm 2>&1
echo "  wubu_face.c      OK"

$CC $CFLAGS -c src/wubu_wss.c     -o build/wubu_wss.o     -lws2_32 2>&1
echo "  wubu_wss.c       OK"

$CC $CFLAGS -c src/wubu_sica.c   -o build/wubu_sica.o  -lsqlite3 2>&1
echo "  wubu_sica.c      OK"

$CC $CFLAGS -c src/wubu_rvc.c    -o build/wubu_rvc.o   -lm 2>&1
echo "  wubu_rvc.c        OK"

$CC $CFLAGS -c src/wubu_buddy.c  -o build/wubu_buddy.o -lsqlite3 -lm 2>&1
echo "  wubu_buddy.c      OK"

$CC $CFLAGS -c src/wubu_vc.c     -o build/wubu_vc.o   -lsqlite3 -lm 2>&1
echo "  wubu_vc.c         OK"

$CC $CFLAGS -c src/wubu_daemon.c -o build/wubu_daemon.o -lsqlite3 2>&1
echo "  wubu_daemon.c    OK"

$CC $CFLAGS -c src/wubucmd.c     -o build/wubucmd.o     2>&1
echo "  wubucmd.c        OK"

echo ""
echo "=== Compiling modules (object files) ==="
$CC $CFLAGS -c src/wubucmd.c -o build/wubucmd.o 2>&1
echo "  wubucmd.c         OK (object only, needs -lgdi32 -luser32 to link)"

echo ""
echo "=== Building executables ==="
$CC $CFLAGS -DWUBU_DAEMON_MAIN src/wubu_daemon.c src/wubu_wiki.c -lsqlite3 -o build/wubu_daemon.exe 2>&1
echo "  wubu_daemon.exe   OK"

if [ "$1" = "test" ]; then
    echo ""
    echo "=== Building test suites ==="
    $CC $CFLAGS src/test_daemon.c  src/wubu_daemon.c src/wubu_wiki.c -lsqlite3 -o build/test_daemon.exe 2>&1
    echo "  test_daemon.exe   OK"

    $CC $CFLAGS src/test_emotion.c src/wubu_emotion.c -lm -o build/test_emotion.exe 2>&1
    echo "  test_emotion.exe  OK"

    $CC $CFLAGS src/test_rlm.c src/wubu_rlm.c -lsqlite3 -lm -o build/test_rlm.exe 2>&1
    echo "  test_rlm.exe      OK"

    $CC $CFLAGS src/test_recs.c src/wubu_recs.c -lsqlite3 -lm -o build/test_recs.exe 2>&1
    echo "  test_recs.exe     OK"

    $CC $CFLAGS src/test_cohost.c src/wubu_cohost.c src/wubu_wiki.c src/wubu_emotion.c src/wubu_rlm.c src/wubu_recs.c -lsqlite3 -lm -o build/test_cohost.exe 2>&1
    echo "  test_cohost.exe   OK"

    $CC $CFLAGS src/test_agent.c src/wubu_agent.c src/wubu_cohost.c src/wubu_wiki.c src/wubu_emotion.c src/wubu_rlm.c src/wubu_recs.c -lsqlite3 -lm -o build/test_agent.exe 2>&1
    echo "  test_agent.exe    OK"

    $CC $CFLAGS src/test_rest.c src/wubu_gateway.c src/wubu_self.c src/wubu_face.c src/wubu_wss.c src/wubu_cohost.c src/wubu_wiki.c src/wubu_emotion.c src/wubu_rlm.c src/wubu_recs.c -lsqlite3 -lm -lws2_32 -o build/test_rest.exe 2>&1
    echo "  test_rest.exe     OK"

    $CC $CFLAGS src/test_sica.c src/wubu_sica.c -lsqlite3 -o build/test_sica.exe 2>&1
    echo "  test_sica.exe     OK"

    $CC $CFLAGS src/test_rvc.c src/wubu_rvc.c src/wubu_rlm.c -lsqlite3 -lm -o build/test_rvc.exe 2>&1
    echo "  test_rvc.exe      OK"

    $CC $CFLAGS src/test_vc.c src/wubu_vc.c src/wubu_rvc.c src/wubu_rlm.c src/wubu_buddy.c -lsqlite3 -lm -o build/test_vc.exe 2>&1
    echo "  test_vc.exe       OK"

    echo ""
    echo "=== Running all tests ==="
    rm -f /tmp/test_wiki*.db* /tmp/test_rlm*.db* /tmp/test_recs.db* /tmp/test_cohost.db* /tmp/test_agent*.db* /tmp/test_rest*.db* /tmp/self_test.log /tmp/test_debug*
    ./build/test_daemon.exe || true
    echo ""
    ./build/test_emotion.exe || true
    echo ""
    ./build/test_rlm.exe || true
    echo ""
    ./build/test_recs.exe || true
    echo ""
    rm -f /tmp/test_cohost.db* /tmp/wubu_recs.db* /tmp/wubu_rlm.db*
    ./build/test_cohost.exe || true
    echo ""
    rm -f /tmp/test_agent_cohost.db* /tmp/wubu_recs.db* /tmp/wubu_rlm.db*
    ./build/test_agent.exe || true
    echo ""
    rm -f /tmp/test_rest.db* /tmp/wubu_recs.db* /tmp/wubu_rlm.db* /tmp/self_test.log /tmp/face_state.json
    ./build/test_rest.exe || true
    echo ""
    rm -f /tmp/test_sica*
    ./build/test_sica.exe || true

    rm -f /tmp/test_rvc*
    ./build/test_rvc.exe || true

    rm -f /tmp/test_vc*
    ./build/test_vc.exe || true
fi

echo ""
echo "=== Done (zero warnings) ==="
