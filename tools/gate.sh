#!/bin/sh
# Before a commit: builds the PC tests and runs every check. Exits 0 only if
# all of them passed, so it can stand before `git commit` with `&&`.
set -e
cd "$(dirname "$0")/.."
cmake -S host -B build/host >/dev/null
if ! cmake --build build/host -j4 >build/host/gate-build.log 2>&1; then
    grep -E "error" -A8 build/host/gate-build.log | head -60
    echo "gate: the build failed"
    exit 1
fi
out=$(timeout 550 ctest --test-dir build/host -j3 2>&1 || true)
echo "$out" | grep -E "Failed|tests passed" || true
echo "$out" | grep -q "100% tests passed" || { echo "gate: tests failed"; exit 1; }
for scenes in render_scenes render_net_scenes test_cactus; do
    python3 tools/check_pictures.py "build/host/$scenes" >build/host/gate-pictures.log 2>&1 ||
        { tail -5 build/host/gate-pictures.log; echo "gate: $scenes does not match"; exit 1; }
done
python3 tools/check_os_calls.py >build/host/gate-calls.log 2>&1 ||
    { tail -5 build/host/gate-calls.log; echo "gate: the OS-call check failed"; exit 1; }
# A merge left unfinished: conflict markers in a file git knows.
if git grep -n -E '^(<<<<<<< |>>>>>>> )' -- . ':!tools/gate.sh' >build/host/gate-markers.log 2>&1; then
    head -5 build/host/gate-markers.log
    echo "gate: conflict markers left in"
    exit 1
fi
echo "gate: all passed"
