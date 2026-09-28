#!/usr/bin/env bash
# REBUILD_PLAN §9 removal checklist audit (task P4-05; the evidence and verdicts are in
# docs/removal-audit.md). Re-runs the grep checks over first-party code and exits non-zero when
# one of the removed jj-style parts of the old gg is back.
#
# Usage: scripts/removal_audit.sh [--packages DIR] [-v]
#   --packages DIR  also list every ggui-*.tar.gz / *.zip / *.deb in DIR and reject old gg files
#                   (headers, CMake package config, libraries, a `gg` binary).
#   -v              print every hit, allowed ones included, with the reason it is allowed.
#
# Scope: tracked first-party files (Source/, cmake/ except the vendored CPM.cmake, CMakeLists.txt,
# CMakePresets.json, scripts/, docs/, res/, .github/). Build directories and fetched third-party
# code are out of scope. The behavioral side (refs/gg and .git/gg after real operations) is the
# post-test hook in Source/tests/TestRunner.cpp and the "removal" scenarios in the test suite.
set -u
cd "$(dirname "$0")/.."

PACKAGES=""
VERBOSE=0
while [[ $# -gt 0 ]]; do
    case $1 in
        --packages) PACKAGES=$2; shift 2 ;;
        -v) VERBOSE=1; shift ;;
        -h|--help) sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

PATHS=(Source cmake CMakeLists.txt CMakePresets.json scripts docs res .github)
EXCLUDE=(':!cmake/CPM.cmake')
# This audit's own files name every pattern they look for.
SELF='^(scripts/removal_audit\.sh|docs/removal-audit\.md):'
failures=0

# search FLAGS PATTERN [PATHSPEC...]: file:line:text hits in tracked first-party files.
search() {
    local flags=$1 pattern=$2
    shift 2
    local spec=("$@")
    [[ ${#spec[@]} -eq 0 ]] && spec=("${PATHS[@]}")
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git grep -n -I -E $flags -e "$pattern" -- "${spec[@]}" "${EXCLUDE[@]}" 2>/dev/null
    else
        grep -rn -I -E $flags --exclude=CPM.cmake --exclude-dir=__pycache__ -e "$pattern" "${spec[@]}" 2>/dev/null
    fi | grep -v -E "$SELF"
}

# check NAME FLAGS PATTERN ALLOWED_REGEX REASON [PATHSPEC...]: hits outside ALLOWED_REGEX (matched
# against "file:line:text") are violations.
check() {
    local name=$1 flags=$2 pattern=$3 allowed=$4 reason=$5
    shift 5
    local hits bad
    hits=$(search "$flags" "$pattern" "$@")
    if [[ -n $allowed ]]; then
        bad=$(grep -v -E "$allowed" <<<"$hits")
    else
        bad=$hits
    fi
    bad=$(sed '/^$/d' <<<"$bad")
    if [[ -n $bad ]]; then
        echo "FAIL  $name"
        sed 's/^/        /' <<<"$bad"
        failures=$((failures + 1))
    else
        local n
        n=$(sed '/^$/d' <<<"$hits" | wc -l)
        echo "ok    $name ($n allowed hit(s))"
        if [[ $VERBOSE -eq 1 && $n -gt 0 ]]; then
            echo "        allowed: $reason"
            sed '/^$/d; s/^/        /' <<<"$hits"
        fi
    fi
}

fail() {
    echo "FAIL  $1"
    [[ -n ${2:-} ]] && sed 's/^/        /' <<<"$2"
    failures=$((failures + 1))
}

echo "== REBUILD_PLAN §9 removal checklist"

# 1. The old libgg and its packaging.
check "old libgg header <gg/gg.h>" "" 'gg/gg\.h' '' ''
check "old gg::gg target, ggConfig.cmake, find_package(gg)" "" \
    'gg::gg([^A-Za-z0-9_]|$)|ggConfig|gg-config\.cmake|find_package\(gg[ )]|install\(EXPORT|export\(TARGETS|configure_package_config_file|write_basic_package_version_file' \
    '' ''

# 2. No gg CLI binary: the only executables are ggui and git-gg, and only they are installed.
exes=$(search "" 'add_executable\(' | sed -E 's/.*add_executable\(\s*([^ )]+).*/\1/' | sort -u)
unexpected=$(grep -v -x -E 'ggui|git-gg' <<<"$exes" | sed '/^$/d')
[[ -n $unexpected ]] && fail "executables other than ggui and git-gg" "$unexpected" \
    || echo "ok    executables: $(tr '\n' ' ' <<<"$exes")"
installed=$(search "" 'install\(' CMakeLists.txt cmake Source/CMakeLists.txt 'Source/*/CMakeLists.txt')
bad=$(grep -E 'install\((EXPORT|DIRECTORY)|install\(TARGETS' <<<"$installed" | grep -v -E 'install\(TARGETS (ggui|git-gg) ')
[[ -n $bad ]] && fail "install rules other than ggui, git-gg and data files" "$bad" \
    || echo "ok    install rules: TARGETS ggui and git-gg only, no EXPORT or DIRECTORY"
check "OUTPUT_NAME gg" "" 'OUTPUT_NAME[[:space:]]+gg([^_A-Za-z]|$)' '' ''

# 3. refs/gg: read to offer the C3 cleanup, never written.
check "refs/gg only in the C3 cleanup and its tests" "" 'refs/gg' \
    '^(Source/core/Readers\.cpp:|Source/core/include/core/Types\.hpp:|Source/app/shell/SessionDialogs\.cpp:|Source/libgg/include/libgg/Operation\.hpp:[0-9]+:\s*//|Source/tests/|docs/)' \
    'C3 detection (Readers.cpp), its snapshot field (Types.hpp), the cleanup dialog (SessionDialogs.cpp), a comment on journaling leftovers (Operation.hpp), tests that plant old refs or assert none are written, docs'
check "no code creates a refs/gg ref" "" \
    '(update-ref|git_reference_create|git_reference_symbolic_create|"create ").*refs/gg|refs/gg.*(git_reference_create|git_reference_symbolic_create)' \
    '^Source/tests/' 'tests plant old gg refs with plain git' Source

# 4. Conflict metadata: conflicts live in file content only.
check "conflict metadata (refs/gg/conflicts, notes, metadata files)" "-i" \
    'refs/gg/conflicts|conflict[-_ ]?metadata|git_note_|"notes"' '^(docs/spec/|Source/tests/)' \
    'spec text and tests that state there is none'
# Everything under $GIT_COMMON_DIR/gg: journal, disposable cache, hook runner, journal bookkeeping.
ggfiles=$(search "" '"gg" / ' Source ':!Source/tests' | sed -E 's/.*"gg" \/ \(?"([^"]*)".*/\1/' | sort -u)
bad=$(grep -v -x -E 'journal|cache|hooks|rebase|symref-|rewrite-' <<<"$ggfiles" | sed '/^$/d')
[[ -n $bad ]] && fail ".git/gg entries other than journal, cache, hooks, rebase, symref-, rewrite-" "$bad" \
    || echo "ok    .git/gg entries written by the code: $(tr '\n' ' ' <<<"$ggfiles")"

# 5. Working-tree auto-snapshot and the max-new-file-size setting.
check "working-tree auto-snapshot" "-i" \
    'auto-?snapshot|autosnapshot|auto_snapshot|snapshot(_|-| )?(the )?working(_|-| )?(tree|copy)|working(_|-| )?copy(_|-| )?snapshot|snapshotWorking' \
    '' ''
check "max-new-file-size setting" "-i" 'max.?new.?file|maxNewFile|snapshot\.max' \
    '^docs/spec/ui-spec\.md:[0-9]+:.*\*\*D\*\* max-new-file-size' 'the UI spec records it as dropped (D)'

# 6. Revsets, filesets and the old gg CLI families.
check "revset and fileset languages" "-i" 'revsets?|filesets?' \
    '^(Source/tests/(test_removal\.cpp|spec_catalogue\.txt)|docs/traceability\.md):' \
    'the scenario and catalogue entry that assert their absence (and the matrix generated from the catalogue)'
subs=$(search "" 'add_subcommand\("' Source/gitgg | sed -E 's/.*add_subcommand\("([^"]+)".*/\1/' | sort -u)
bad=$(grep -v -x -E 'new|undo|redo|op|log|conflicts|hooks|hook|ui|sequence-editor|help' <<<"$subs" | sed '/^$/d')
[[ -n $bad ]] && fail "git gg subcommands outside REBUILD_PLAN §6" "$bad" \
    || echo "ok    git gg subcommands (§6 only): $(tr '\n' ' ' <<<"$subs")"
check "old gg CLI families" "" \
    'add_subcommand\("(branch|file|util|workspace|config|operation|restore|next|prev)"|"--what"' '' '' Source/gitgg

# 7. Agent skill docs.
skills=$(git ls-files 2>/dev/null | grep -i -E '(^|/)(skills?|\.claude|agents?)/|SKILL\.md$|(^|/)AGENTS?\.md$')
[[ -n $skills ]] && fail "agent skill docs" "$skills" || echo "ok    agent skill docs: none tracked"

# 8. Fetch never fast-forwards local branches by itself.
check "fetch into local branches (auto fast-forward)" "" \
    '"fetch".*refs/heads/|--update-head-ok' '^Source/app/shell/Actions\.cpp:[0-9]+:.*ctx\.git\(\{"fetch", "-q", "\.", ' \
    'Actions::fastForward, the explicit "Fast-forward to upstream" action (§4.7 N), fetches from "." into one branch' \
    Source ':!Source/tests'

# 9. UI wording (rule 10): HEAD, branch, commit, staged, unstaged.
UI=(Source/app Source/gitgg Source/core Source/libgg ':!Source/tests')
check "\"@ is a change you edit\"" "-i" 'change you edit' '' '' "${UI[@]}"
check "change IDs, aliases, workspaces, jj" "-i" \
    'change[ _-]?ids?([^A-Za-z]|$)|(^|[^A-Za-z])alias(es)?([^A-Za-z]|$)|workspaces?|jujutsu|(^|[^A-Za-z])jj([^A-Za-z]|$)' \
    '^Source/libgg/Rewrite\.cpp:[0-9]+:.*(alias\.gg-post-rewrite=|`!` alias so git)' \
    'a one-shot git `!` alias (git -c alias.gg-post-rewrite=...) runs the post-rewrite hook through git'"'"'s own shell on git before 2.40; not a gg alias' \
    "${UI[@]}"
check "\"@\" as a word in UI strings" "" '"[^"]*[ (]@([ ).,:!?][^"]*)?"|"@ [^"]*"' \
    '^[^:]+:[0-9]+:\s*//' 'comments quoting the plan' Source/app Source/gitgg
check "\"Change\" as a label" "" '"Change"' '' '' Source/app

# 10. Packages (optional): installed files only.
if [[ -n $PACKAGES ]]; then
    shopt -s nullglob
    pkgs=("$PACKAGES"/ggui*.tar.gz "$PACKAGES"/ggui*.zip "$PACKAGES"/ggui*.deb)
    [[ ${#pkgs[@]} -eq 0 ]] && fail "no packages in $PACKAGES"
    for p in "${pkgs[@]}"; do
        case $p in
            *.tar.gz) list=$(tar tzf "$p") ;;
            *.zip) list=$(unzip -Z1 "$p") ;;
            *.deb) d=$(mktemp -d); abs=$(realpath "$p")
                list=$(cd "$d" && ar x "$abs" && tar tf data.tar.*); rm -rf "$d" ;;
        esac
        bad=$(grep -E '(^|/)(include|cmake)/|gg/gg\.h|[Gg]gConfig|gg-config|\.(a|lib|h|hpp|pc)$|(^|/)bin/gg$|(^|/)gg(\.exe)?$|libgg' <<<"$list")
        [[ -n $bad ]] && fail "package $(basename "$p") ships old gg files" "$bad" \
            || echo "ok    package $(basename "$p"): $(grep -c -v '/$' <<<"$list") file(s), no old gg files"
    done
fi

echo
if [[ $failures -gt 0 ]]; then
    echo "removal audit: $failures violation(s)"
    exit 1
fi
echo "removal audit: clean"
