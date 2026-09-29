#!/usr/bin/env bash
# Package smoke test for Linux.
#
#   scripts/package_smoke.sh [--no-build] [--no-sandbox] [--container IMAGE]...
#
# 1. Builds the release preset and runs CPack (cmake --workflow --preset package-linux) unless
#    --no-build is given.
# 2. For each package (.tar.gz, .zip, .deb) in build/packages: extracts it into a temporary prefix
#    and checks
#      - the files: bin/ggui, bin/git-gg, the .desktop file (desktop-file-validate if present), the
#        icon and the license;
#      - no test engine: `ggui --test` and `--list-tests` are rejected, and neither the symbols of
#        the unstripped build nor the strings of the packaged binaries contain test-engine names or
#        any test name from Source/tests;
#      - dynamic dependencies (ldd, RUNPATH): everything resolves, nothing under the source or
#        build tree;
#      - with a clean environment (env -i, PATH = the prefix plus a directory holding only the
#        system git): `ggui --version`, `ggui --help`, `git gg --version`, `git gg help`, and
#        `git gg new` / `git gg undo` in a new repository; `ggui --smoke` (offscreen) when a
#        Vulkan device is available.
#    The runtime checks run in a user + mount namespace (unshare -rm) when the kernel allows it:
#    /home, /root and /tmp are empty tmpfs mounts (no source or build tree, no user config) and
#    the package is mounted at /mnt, which is as close to a clean machine as this host gets
#    without a container.
# 3. --container IMAGE (docker or podman, repeatable; e.g. ubuntu:24.04, debian:trixie): installs
#    the .deb with apt into a fresh container (so the package's Depends are checked) and runs the
#    same runtime checks there against /usr. This is the clean-VM check; CI runs it on ubuntu:24.04.
#
# Exit status 0 when every check passed.
set -euo pipefail

VERSION_RE='[0-9]+\.[0-9]+\.[0-9]+'
fail=0
ok() { printf '  ok    %s\n' "$*"; }
bad() { printf '  FAIL  %s\n' "$*"; fail=1; }
note() { printf '  note  %s\n' "$*"; }

# ---------------------------------------------------------------------------------------------
# --check PREFIX VERSION: runtime checks of an installed tree (runs inside bwrap or a container).
# ---------------------------------------------------------------------------------------------
check_runtime() {
    local prefix=$1 version=$2
    local git_bin
    git_bin=$(command -v git) || { bad "no git on this system"; return; }
    local work
    work=$(mktemp -d)
    mkdir -p "$work/gitbin" "$work/home" "$work/runtime"
    chmod 700 "$work/runtime"
    ln -s "$git_bin" "$work/gitbin/git"
    printf '[user]\n\tname = Smoke Test\n\temail = smoke@example.invalid\n[init]\n\tdefaultBranch = main\n' \
        > "$work/home/.gitconfig"
    clean() {
        env -i HOME="$work/home" PATH="$prefix/bin:$work/gitbin" LANG=C.UTF-8 TERM=dumb \
            XDG_RUNTIME_DIR="$work/runtime" GIT_CONFIG_NOSYSTEM=1 "$@" </dev/null
    }
    local out rc

    out=$(clean ggui --version 2>&1) && [[ $out == "ggui $version" ]] \
        && ok "ggui --version: $out" || bad "ggui --version: '$out'"
    out=$(clean ggui --help 2>&1) && [[ $out == "usage: ggui"* && $out != *--test* ]] \
        && ok "ggui --help (no test options listed)" || bad "ggui --help: '$out'"
    for opt in --test --test=cli --list-tests; do
        rc=0; out=$(clean ggui "$opt" 2>&1) || rc=$?
        [[ $rc == 2 && $out == *"no test engine"* ]] && ok "ggui $opt rejected (exit 2)" \
            || bad "ggui $opt: exit $rc '$out'"
    done
    out=$(clean git --version 2>&1) && ok "system git: $out" || bad "git --version: '$out'"
    out=$(clean git gg --version 2>&1) && [[ $out == "git gg $version" ]] \
        && ok "git gg --version: $out" || bad "git gg --version: '$out'"
    out=$(clean git gg help 2>&1) && [[ $out == "usage: git gg"* ]] \
        && ok "git gg help" || bad "git gg help: '$out'"

    local repo=$work/repo
    if clean git init -q "$repo" && (cd "$repo" && clean git commit -q --allow-empty -m base); then
        local before after
        before=$(cd "$repo" && clean git rev-parse HEAD)
        out=$(cd "$repo" && clean git gg new -m "from git gg" 2>&1) || true
        after=$(cd "$repo" && clean git rev-parse HEAD)
        if [[ $after != "$before" && $(cd "$repo" && clean git log -1 --format=%s) == "from git gg" ]]; then
            ok "git gg new: $after"
        else
            bad "git gg new: '$out'"
        fi
        out=$(cd "$repo" && clean git gg undo 2>&1) || true
        [[ $(cd "$repo" && clean git rev-parse HEAD) == "$before" ]] && ok "git gg undo: HEAD back at $before" \
            || bad "git gg undo: '$out'"
        out=$(cd "$repo" && clean git fsck --no-progress 2>&1) && ok "git fsck" || bad "git fsck: $out"
    else
        bad "git init/commit in a clean environment"
    fi

    # ggui itself: start offscreen, render a few frames, exit. Needs a Vulkan device (lavapipe
    # counts); machines without one are reported, not failed.
    rc=0
    out=$(clean GGUI_HEADLESS=1 ${VK_DRIVER_FILES:+VK_DRIVER_FILES=$VK_DRIVER_FILES} \
        "$(command -v timeout)" 60 ggui --smoke "$repo" 2>&1) || rc=$?
    if [[ $rc == 0 ]]; then
        ok "ggui --smoke --headless on a repository"
    elif [[ $out == *[Vv]ulkan* || $out == *GPU* || $out == *"No supported"* ]]; then
        note "ggui --smoke skipped: no usable Vulkan device here (exit $rc): ${out%%$'\n'*}"
    else
        bad "ggui --smoke: exit $rc '$out'"
    fi
    rm -rf "$work"
}

if [[ ${1:-} == --check ]]; then
    check_runtime "$2" "$3"
    exit $fail
fi

# ---------------------------------------------------------------------------------------------
# Outer mode
# ---------------------------------------------------------------------------------------------
build=1 sandbox=1
containers=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --no-build) build=0 ;;
        --no-sandbox) sandbox=0 ;;
        --container) containers+=("$2"); shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
self=$root/scripts/package_smoke.sh
cd "$root"
if [[ $build == 1 ]]; then
    echo "== cmake --workflow --preset package-linux"
    cmake --workflow --preset package-linux
fi
version=$(grep -oE "project\(ggui VERSION $VERSION_RE" CMakeLists.txt | grep -oE "$VERSION_RE")
pkgdir=$root/build/packages
builddir=$root/build/release
base=ggui-$version-linux-$(uname -m)
tgz=$pkgdir/$base.tar.gz
zip=$pkgdir/$base.zip
deb=$(ls "$pkgdir"/ggui_"$version"_*.deb 2>/dev/null | head -1 || true)

echo "== packages ($pkgdir)"
for f in "$tgz" "$zip" "$deb"; do
    if [[ -n $f && -f $f ]]; then
        ok "$(basename "$f") ($(du -h "$f" | cut -f1))"
    else
        bad "missing package ${f:-ggui_${version}_*.deb}"
    fi
done
[[ $fail == 0 ]] || { echo "FAILED"; exit 1; }

echo "== no test engine in the build (symbols of the unstripped $builddir/bin)"
grep -q '^GGUI_ENABLE_IMGUI_TEST_ENGINE:BOOL=OFF' "$builddir/CMakeCache.txt" \
    && ok "GGUI_ENABLE_IMGUI_TEST_ENGINE=OFF" || bad "release build has the test engine on"
te_syms='ImGuiTest[A-Z]|ggtest::|imgui_te_|TestRunner'
for exe in ggui git-gg; do
    n=$(nm -C "$builddir/bin/$exe" 2>/dev/null | grep -cE "$te_syms" || true)
    total=$(nm "$builddir/bin/$exe" 2>/dev/null | wc -l)
    [[ $total -gt 1000 && $n == 0 ]] && ok "$exe: $total symbols, none from the test engine or tests" \
        || bad "$exe: $n test-engine symbols (of $total)"
done
# Every test name from Source/tests; none may appear in a packaged binary.
names=$(mktemp)
grep -rhoE 'GG_TEST\("[^"]+", *"[^"]+"' Source/tests | sed -E 's/.*, *"([^"]+)"$/\1/' | sort -u > "$names"
echo "   ($(wc -l < "$names") test names from Source/tests)"

check_tree() { # PREFIX (contains bin/ and share/)
    local prefix=$1
    for f in bin/ggui bin/git-gg; do
        [[ -x $prefix/$f ]] && ok "$f ($(du -h "$prefix/$f" | cut -f1))" || bad "missing $f"
    done
    for f in share/applications/ggui.desktop share/icons/hicolor/256x256/apps/ggui.png share/doc/ggui/LICENSE; do
        [[ -f $prefix/$f ]] && ok "$f" || bad "missing $f"
    done
    if command -v desktop-file-validate >/dev/null && [[ -f $prefix/share/applications/ggui.desktop ]]; then
        desktop-file-validate "$prefix/share/applications/ggui.desktop" && ok "desktop-file-validate" \
            || bad "desktop-file-validate"
    fi
    local other
    other=$(cd "$prefix" && find . -type f ! -path ./bin/ggui ! -path ./bin/git-gg ! -path ./share/applications/ggui.desktop \
        ! -path ./share/icons/hicolor/256x256/apps/ggui.png ! -path ./share/doc/ggui/LICENSE | sed 's|^\./||')
    [[ -z $other ]] && ok "no other files" || bad "unexpected files: $other"
    for exe in ggui git-gg; do
        local bin=$prefix/bin/$exe
        [[ -x $bin ]] || continue
        local hits
        hits=$( (strings -n 6 "$bin" | grep -E 'ImGuiTestEngine|imgui_te_|ggtest'; strings -n 6 "$bin" | grep -xFf "$names") \
            | head -3 || true)
        [[ -z $hits ]] && ok "$exe: no test-engine strings or test names" || bad "$exe contains: $hits"
        local deps
        deps=$(ldd "$bin" 2>&1 || true)
        if grep -q "not found" <<<"$deps"; then
            bad "$exe: unresolved libraries: $(grep 'not found' <<<"$deps" | tr -s ' \t' ' ')"
        elif grep -qF "$root" <<<"$deps"; then
            bad "$exe links into the source/build tree: $(grep -F "$root" <<<"$deps")"
        else
            ok "$exe links $(readelf -d "$bin" | sed -nE 's/.*NEEDED.*\[(.*)\]/\1/p' | xargs), all resolved by ldd outside the tree"
        fi
        local rpath
        rpath=$(readelf -d "$bin" | grep -E 'RPATH|RUNPATH' | sed -E 's/.*\[(.*)\]/\1/' || true)
        [[ -z $rpath || $rpath == '$ORIGIN' ]] && ok "$exe RUNPATH: ${rpath:-none}" || bad "$exe RUNPATH: $rpath"
        local glibc
        glibc=$(objdump -T "$bin" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1)
        note "$exe needs $glibc; $(objdump -T "$bin" | grep -qE 'GLIBCXX|CXXABI' && echo 'shared libstdc++' || echo 'static libstdc++')"
    done
}

run_checks() { # PREFIX
    local prefix=$1
    # A user + mount namespace hides the home directories (so the source and build trees) and /tmp;
    # the package is mounted at /mnt and this script is read from stdin.
    if [[ $sandbox == 1 ]] && unshare -rm true 2>/dev/null; then
        note "runtime checks in a mount namespace: /home and /tmp empty, package at /mnt"
        unshare -rm --propagation private bash -c '
            mount --bind "$1" /mnt && mount -t tmpfs tmpfs /home && mount -t tmpfs tmpfs /tmp \
                && { [[ ! -d /root ]] || mount -t tmpfs tmpfs /root; } || exit 1
            cd / && exec env -i PATH=/usr/bin:/bin ${3:+VK_DRIVER_FILES=$3} bash -s -- --check /mnt "$2"
        ' _ "$prefix" "$version" "${VK_DRIVER_FILES:-}" < "$self" || fail=1
    else
        note "runtime checks without a sandbox (unshare not permitted or --no-sandbox)"
        bash "$self" --check "$prefix" "$version" || fail=1
    fi
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp" "$names"' EXIT

echo "== $(basename "$tgz")"
mkdir -p "$tmp/tgz" && tar -xzf "$tgz" -C "$tmp/tgz"
check_tree "$tmp/tgz/$base"
run_checks "$tmp/tgz/$base"

echo "== $(basename "$zip")"
mkdir -p "$tmp/zip"
if command -v unzip >/dev/null; then unzip -q "$zip" -d "$tmp/zip"; else bsdtar -xf "$zip" -C "$tmp/zip"; fi
check_tree "$tmp/zip/$base"
run_checks "$tmp/zip/$base"

echo "== $(basename "$deb")"
mkdir -p "$tmp/deb/root" && (cd "$tmp/deb" && ar x "$deb")
tar -xf "$tmp"/deb/data.tar.* -C "$tmp/deb/root"
tar -xf "$tmp"/deb/control.tar.* -C "$tmp/deb"
grep -E '^(Package|Version|Architecture|Depends|Recommends|Installed-Size):' "$tmp/deb/control" | sed 's/^/   /'
check_tree "$tmp/deb/root/usr"
run_checks "$tmp/deb/root/usr"

for image in "${containers[@]}"; do
    echo "== container $image: apt install ./$(basename "$deb"), checks against /usr"
    engine=$(command -v podman || command -v docker || true)
    if [[ -z $engine ]]; then bad "no docker or podman"; continue; fi
    "$engine" run --rm -v "$pkgdir:/pkg:ro" -v "$self:/smoke.sh:ro" "$image" bash -c "
        set -e
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq >/dev/null
        apt-get install -y -qq --no-install-recommends /pkg/$(basename "$deb") >/dev/null
        echo '  ok    apt installed $(basename "$deb") with its dependencies'
        bash /smoke.sh --check /usr $version" || fail=1
done

if [[ $fail == 0 ]]; then echo "== all package checks passed"; else echo "== FAILED"; fi
exit $fail
