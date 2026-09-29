#!/usr/bin/env bash
# Package smoke test for a Windows ZIP. Runs in bash on Windows (MSYS2 or Git Bash;
# the CI package jobs) and, for the file and import checks, on Linux with a MinGW cross build.
#
#   scripts/package_smoke_windows.sh ZIP
#
# Extracts ZIP into a temporary directory and checks
#   - the files: ggui.exe, git-gg.exe, LICENSE.txt (and the MSVC runtime DLLs only if the build
#     used the DLL CRT);
#   - imports (objdump -p or dumpbin /dependents): no MinGW runtime DLLs (libstdc++, libgcc_s,
#     libwinpthread) and no DLL that is not shipped or part of Windows;
#   - no test engine: no test-engine strings or test names from Source/tests in ggui.exe;
#   - on Windows, with PATH = the package directory plus the directory of git:
#     `ggui --version`, `ggui --test` rejected, `git gg --version`, `git gg help`,
#     `git gg new` / `git gg undo` in a new repository.
set -euo pipefail

zip=${1:?usage: package_smoke_windows.sh ZIP}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
version=$(grep -oE 'project\(ggui VERSION [0-9.]+' "$root/CMakeLists.txt" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
fail=0
ok() { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
if command -v unzip >/dev/null; then unzip -q "$zip" -d "$tmp"; else tar -xf "$zip" -C "$tmp"; fi
dir=$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | head -1)
echo "== $(basename "$zip") ($(du -h "$zip" | cut -f1))"

for f in ggui.exe git-gg.exe LICENSE.txt; do
    [[ -f $dir/$f ]] && ok "$f ($(du -h "$dir/$f" | cut -f1))" || bad "missing $f"
done
shipped=$(cd "$dir" && ls | tr 'A-Z' 'a-z')
other=$(cd "$dir" && ls | grep -viE '^(ggui\.exe|git-gg\.exe|LICENSE\.txt|(msvcp|vcruntime|concrt|vccorlib)[0-9_a-z]*\.dll|api-ms-win-.*\.dll|ucrtbase\.dll)$' || true)
[[ -z $other ]] && ok "no other files" || bad "unexpected files: $other"

imports() {
    if command -v objdump >/dev/null && objdump -p "$1" >/dev/null 2>&1; then
        objdump -p "$1" | sed -nE 's/.*DLL Name: (.*)/\1/p'
    elif command -v x86_64-w64-mingw32-objdump >/dev/null; then
        x86_64-w64-mingw32-objdump -p "$1" | sed -nE 's/.*DLL Name: (.*)/\1/p'
    elif command -v dumpbin >/dev/null; then
        dumpbin //nologo //dependents "$(cygpath -w "$1" 2>/dev/null || echo "$1")" | grep -iE '^ +[^ ]+\.dll' | tr -d ' \r'
    fi
}
system_dll='^(kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|imm32|winmm|version|setupapi|ws2_32|secur32|crypt32|bcrypt|shlwapi|comdlg32|dwmapi|uxtheme|dbghelp|ntdll|rpcrt4|hid|cfgmgr32|dinput8|dxgi|d3d11|d3d12|d3dcompiler_47|xinput1_4|api-ms-win-[a-z0-9-]+|ucrtbase|msvcrt)\.dll$'
names=$(mktemp)
grep -rhoE 'GG_TEST\("[^"]+", *"[^"]+"' "$root/Source/tests" | sed -E 's/.*, *"([^"]+)"$/\1/' | sort -u > "$names"
for exe in ggui.exe git-gg.exe; do
    list=$(imports "$dir/$exe" | tr 'A-Z' 'a-z' | tr -d '\r' | sort -u)
    if [[ -z $list ]]; then
        bad "$exe: no objdump or dumpbin to list imports"
        continue
    fi
    missing=""
    for dll in $list; do
        grep -qE "$system_dll" <<<"$dll" || grep -qxF "$dll" <<<"$shipped" || missing+=" $dll"
    done
    [[ -z $missing ]] && ok "$exe imports only Windows or shipped DLLs ($(wc -w <<<"$list"))" \
        || bad "$exe imports DLLs not shipped:$missing"
    hits=$( (strings -n 6 "$dir/$exe" | grep -E 'ImGuiTestEngine|imgui_te_|ggtest'; strings -n 6 "$dir/$exe" | grep -xFf "$names") \
        | head -3 || true)
    [[ -z $hits ]] && ok "$exe: no test-engine strings or test names" || bad "$exe contains: $hits"
done
rm -f "$names"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *) echo "== not on Windows: runtime checks skipped"; [[ $fail == 0 ]] && echo "== passed"; exit $fail ;;
esac

gitdir=$(dirname "$(command -v git)")
work=$(mktemp -d)
trap 'rm -rf "$tmp" "$work"' EXIT
mkdir -p "$work/home"
printf '[user]\n\tname = Smoke Test\n\temail = smoke@example.invalid\n[init]\n\tdefaultBranch = main\n' > "$work/home/.gitconfig"
clean() { HOME="$work/home" USERPROFILE="$work/home" GIT_CONFIG_NOSYSTEM=1 PATH="$dir:$gitdir" "$@" </dev/null; }

out=$(clean ggui.exe --version 2>&1 | tr -d '\r') && [[ $out == "ggui $version" ]] \
    && ok "ggui --version: $out" || bad "ggui --version: '$out'"
rc=0; out=$(clean ggui.exe --test 2>&1 | tr -d '\r'; exit "${PIPESTATUS[0]}") || rc=$?
[[ $rc == 2 && $out == *"no test engine"* ]] && ok "ggui --test rejected (exit 2)" || bad "ggui --test: exit $rc '$out'"
out=$(clean git --version 2>&1) && ok "git: $out" || bad "git --version: '$out'"
out=$(clean git gg --version 2>&1 | tr -d '\r') && [[ $out == "git gg $version" ]] \
    && ok "git gg --version: $out" || bad "git gg --version: '$out'"
out=$(clean git gg help 2>&1 | tr -d '\r') && [[ $out == "usage: git gg"* ]] && ok "git gg help" || bad "git gg help: '$out'"
repo=$work/repo
clean git init -q "$repo"
(cd "$repo" && clean git commit -q --allow-empty -m base)
before=$(cd "$repo" && clean git rev-parse HEAD)
out=$(cd "$repo" && clean git gg new -m "from git gg" 2>&1) || true
[[ $(cd "$repo" && clean git log -1 --format=%s) == "from git gg" ]] && ok "git gg new" || bad "git gg new: '$out'"
out=$(cd "$repo" && clean git gg undo 2>&1) || true
[[ $(cd "$repo" && clean git rev-parse HEAD) == "$before" ]] && ok "git gg undo" || bad "git gg undo: '$out'"

if [[ $fail == 0 ]]; then echo "== all package checks passed"; else echo "== FAILED"; fi
exit $fail
