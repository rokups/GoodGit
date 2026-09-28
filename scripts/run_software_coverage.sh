#!/usr/bin/env bash
# Builds with coverage instrumentation, runs the integration suite inside ggui and reports line
# and branch coverage of first-party code (REBUILD_PLAN §8.2, task P0-13). Since 2026-09-28 the
# report is informational (standing rule 8); it fails only on COVERAGE_EXCL markers beyond
# scripts/coverage_excl_allowlist.txt or a failing test.
#
#   scripts/run_software_coverage.sh                 # clang source-based coverage (default)
#   COVERAGE_TOOL=gcov scripts/run_software_coverage.sh   # GCC gcov + gcovr fallback
#
# Environment:
#   FILTER=...        test filter passed to ggui --test=FILTER
#   SHARDS=N          run the suite as N shards (same as CI)
#   PARALLEL=1        run the shards at the same time (each under its own virtual display)
#   GGUI_HEADLESS=1   no display: SDL's offscreen driver (default: a virtual display through
#                     xvfb-run when it is installed, as CI does, else headless)
#   MIN_LINE, MIN_BRANCH   optional gate thresholds (default: none, report only)
#   NO_GATE=1         report only (the default; kept for older instructions)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOL="${COVERAGE_TOOL:-clang}"
if [[ "$TOOL" == "gcov" ]]; then PRESET=coverage-gcov; else PRESET=coverage; fi
BUILD="$ROOT/build/$PRESET"
SHARDS="${SHARDS:-1}"

cmake --preset "$PRESET" >/dev/null
cmake --build --preset "$PRESET"

OUT="$BUILD/coverage"
rm -rf "$OUT"
mkdir -p "$OUT/profiles" "$OUT/traces"
export GGUI_TEST_ARTIFACTS="$OUT/test-artifacts"
# Every process (ggui and each git-gg child started by git hooks) writes its own profile.
export LLVM_PROFILE_FILE="$OUT/profiles/%p-%m.profraw"

# Like CI: a window on a virtual X display (the windowed code paths run too).
run_ggui() {
    if [[ -z "${GGUI_HEADLESS:-}" ]] && command -v xvfb-run >/dev/null; then
        xvfb-run -a -s "-screen 0 1920x1080x24" "$@"
    else
        GGUI_HEADLESS=1 "$@"
    fi
}

status=0
pids=()
for ((i = 0; i < SHARDS; i++)); do
    args=(--test${FILTER:+=$FILTER} --trace="$OUT/traces/trace$i.json")
    if ((SHARDS > 1)); then args+=(--shard="$i/$SHARDS"); fi
    if [[ -n "${PARALLEL:-}" ]]; then
        run_ggui "$BUILD/bin/ggui" "${args[@]}" > "$OUT/shard$i.log" 2>&1 &
        pids+=($!)
    else
        run_ggui "$BUILD/bin/ggui" "${args[@]}" || status=$?
    fi
done
for pid in "${pids[@]}"; do
    wait "$pid" || status=$?
done
if [[ -n "${PARALLEL:-}" ]]; then cat "$OUT"/shard*.log | grep -E "tests passed|^FAILED|^SKIPPED" || true; fi
# The traces decide whether the tests passed: xvfb-run can exit 1 after a passing run when its
# cleanup finds Xvfb already gone. A missing trace keeps the exit status.
if ((status != 0)); then
    if python3 - "$OUT"/traces/trace*.json "$SHARDS" <<'EOF'
import json, sys
paths, shards = sys.argv[1:-1], int(sys.argv[-1])
if len(paths) != shards:
    sys.exit(1)
ran = [t for p in paths for t in json.load(open(p))["tests"] if t["status"] != "not-run"]
sys.exit(0 if ran and all(t["status"] in ("success", "skipped") for t in ran) else 1)
EOF
    then
        echo "every test passed (the traces say so); ignoring exit status $status of the test run"
        status=0
    fi
fi

if [[ "$TOOL" == "gcov" ]]; then
    gcovr --root "$ROOT" --object-directory "$BUILD" \
        --filter "$ROOT/Source/(libgg|core|app|gitgg)/" \
        --exclude-throw-branches --exclude-unreachable-branches \
        --lcov "$OUT/coverage.lcov"
else
    llvm-profdata merge -sparse "$OUT"/profiles/*.profraw -o "$OUT/coverage.profdata"
    objects=("$BUILD/bin/ggui" -object "$BUILD/bin/git-gg")
    llvm-cov export -format=lcov -instr-profile="$OUT/coverage.profdata" "${objects[@]}" \
        -ignore-filename-regex='(_deps|\.cache/CPM|generated|Source/tests)' > "$OUT/coverage.lcov"
    llvm-cov report -instr-profile="$OUT/coverage.profdata" "${objects[@]}" \
        -ignore-filename-regex='(_deps|\.cache/CPM|generated|Source/tests)' > "$OUT/llvm-cov-report.txt" || true
fi

gate=(--min-line "${MIN_LINE:-0}" --min-branch "${MIN_BRANCH:-0}")
if [[ -n "${NO_GATE:-}" ]]; then gate=(--min-line 0 --min-branch 0); fi
python3 "$ROOT/scripts/coverage_report.py" --lcov "$OUT/coverage.lcov" --root "$ROOT" \
    --allowlist "$ROOT/scripts/coverage_excl_allowlist.txt" --out "$OUT/summary.md" "${gate[@]}" || status=$?
echo "coverage summary: $OUT/summary.md"
exit "$status"
