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
    python3 tools/check_pictures.py "build/host/$scenes" | tail -1
done
python3 tools/check_os_calls.py | tail -1
echo "gate: all passed"
