#!/usr/bin/env bash
# run_tests.sh -- canonical test entrypoint for WuBuMedia.
#   ./run_tests.sh
# Pure stdlib, no pytest. Non-zero exit on any failure.
set -u
PY="./.venv_win/Scripts/python.exe"
[ -x "$PY" ] || PY="python"
rc=0
for t in tests/test_*.py; do
    [ -e "$t" ] || continue
    echo "=== $t ==="
    "$PY" "$t" || rc=1
done
exit $rc
