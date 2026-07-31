#!/usr/bin/env bash
# Make a macOS plugin bundle portable: copy every non system dynamic library the plugin links
# (FluidSynth and its transitive dependencies, normally resolved from Homebrew) into the bundle's
# Contents/Frameworks, rewrite the install names to @rpath, add a loader rpath, and re-sign
# ad-hoc. After this the plugin loads on a clean machine with no Homebrew present.
#
# Usage: bundle-macos-deps.sh <path-to-bundle>   (a .vst3 or .component directory)
#
# Written for macOS's stock bash 3.2 (no associative arrays): the Frameworks directory itself is
# the "already copied" set. Idempotent beyond the re-sign.
set -euo pipefail

bundle="${1:?usage: bundle-macos-deps.sh <bundle.vst3|bundle.component>}"
[[ -d "$bundle" ]] || { echo "error: not a directory: $bundle" >&2; exit 1; }
bundle="$(cd "$bundle" && pwd -P)" # normalize (callers may pass .../MacOS/../..)

macos_dir="$bundle/Contents/MacOS"
frameworks="$bundle/Contents/Frameworks"
binary="$macos_dir/$(/bin/ls "$macos_dir" | head -1)"
[[ -f "$binary" ]] || { echo "error: no binary in $macos_dir" >&2; exit 1; }

# A dependency is "external" (must be bundled) when it lives under a package manager prefix.
is_external() { [[ "$1" == /opt/homebrew/* || "$1" == /usr/local/* ]]; }

# LC_LOAD_DYLIB references of a Mach-O (skip line 1, the file itself).
deps_of() { otool -L "$1" 2>/dev/null | tail -n +2 | awk '{print $1}'; }

mkdir -p "$frameworks"

# Breadth first copy: scan the binary and any library already placed in Frameworks (for example a
# minimal FluidSynth copied in by the build), then each library we pull in, copying any external
# dep that is not already in Frameworks. A file existing in Frameworks marks it as seen.
scan_list=("$binary")
if [[ -d "$frameworks" ]]; then
    while IFS= read -r pre; do scan_list+=("$pre"); done < <(find "$frameworks" -type f -name '*.dylib')
fi
idx=0
while (( idx < ${#scan_list[@]} )); do
    f="${scan_list[$idx]}"; idx=$((idx+1))
    while IFS= read -r dep; do
        [[ -n "$dep" ]] || continue
        is_external "$dep" || continue
        name="$(basename "$dep")"
        target="$frameworks/$name"
        if [[ ! -e "$target" ]]; then
            real="$(readlink -f "$dep" 2>/dev/null || echo "$dep")"
            [[ -f "$real" ]] || { echo "error: cannot find source lib: $real (from $dep)" >&2; exit 1; }
            cp -f "$real" "$target"
            chmod u+w "$target"
            scan_list+=("$target")
        fi
    done < <(deps_of "$f")
done

count=$(/bin/ls "$frameworks" 2>/dev/null | wc -l | tr -d ' ')
if [[ "$count" == "0" ]]; then
    rmdir "$frameworks" 2>/dev/null || true
    echo "  $(basename "$bundle"): no external dependencies to bundle"
    exit 0
fi

# Rewrite install names: each bundled lib's own id and every external reference become @rpath/<name>.
for lib in "$frameworks"/*; do
    install_name_tool -id "@rpath/$(basename "$lib")" "$lib"
    while IFS= read -r dep; do
        is_external "$dep" || continue
        install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$lib"
    done < <(deps_of "$lib")
done

# The plugin binary: redirect its external references and add the rpath to Frameworks.
while IFS= read -r dep; do
    is_external "$dep" || continue
    install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$binary"
done < <(deps_of "$binary")
# Contents/MacOS/<bin> -> Contents/Frameworks. Ignore the error if the rpath is already present.
install_name_tool -add_rpath "@loader_path/../Frameworks" "$binary" 2>/dev/null || true

# Modifying Mach-O invalidates code signatures; Apple Silicon needs at least an ad-hoc signature to
# load. Re-sign the whole bundle (nested Frameworks included).
codesign --force --deep --sign - "$bundle" >/dev/null 2>&1 || codesign --force --deep --sign - "$bundle"

echo "  $(basename "$bundle"): bundled $count libraries into Contents/Frameworks"
