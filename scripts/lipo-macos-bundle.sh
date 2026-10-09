#!/usr/bin/env bash
# Merge the arm64 and the x86_64 build of a macOS plugin bundle into one universal bundle.
#
# Homebrew only ever installs the architecture of the machine it runs on, so glib (and through it
# FluidSynth) cannot be cross compiled in a single job. Instead each architecture is built on its
# own runner and the two bundles are fused here: every Mach-O in the bundle (the plugin binary and
# every library under Contents/Frameworks) is combined with lipo, and the non code resources are
# taken from the arm64 side, where they are identical by construction.
#
# Usage: lipo-macos-bundle.sh <arm64-bundle> <x86_64-bundle> <output-bundle>
#
# Both inputs must already be portable (bundle-macos-deps.sh has run), so their install names are
# @rpath relative and match across architectures. Written for macOS's stock bash 3.2.
set -euo pipefail

arm_bundle="${1:?usage: lipo-macos-bundle.sh <arm64-bundle> <x86_64-bundle> <output-bundle>}"
x86_bundle="${2:?usage: lipo-macos-bundle.sh <arm64-bundle> <x86_64-bundle> <output-bundle>}"
out_bundle="${3:?usage: lipo-macos-bundle.sh <arm64-bundle> <x86_64-bundle> <output-bundle>}"

for b in "$arm_bundle" "$x86_bundle"; do
    [[ -d "$b" ]] || { echo "error: not a directory: $b" >&2; exit 1; }
done
arm_bundle="$(cd "$arm_bundle" && pwd -P)"
x86_bundle="$(cd "$x86_bundle" && pwd -P)"

# Mach-O files only: lipo reports the architectures of anything it understands and fails otherwise.
archs_of() { lipo -archs "$1" 2>/dev/null || true; }
is_macho() { [[ -n "$(archs_of "$1")" ]]; }
has_arch() { case " $(archs_of "$1") " in *" $2 "*) return 0 ;; *) return 1 ;; esac; }

# Write the single architecture slice of a Mach-O to a path. lipo -thin rejects a file that is
# already thin, so pass those through untouched.
slice_of() {
    local file="$1" arch="$2" dest="$3"
    if [[ "$(archs_of "$file")" == "$arch" ]]; then
        cp -f "$file" "$dest"
    else
        lipo "$file" -thin "$arch" -output "$dest"
    fi
}

rm -rf "$out_bundle"
mkdir -p "$(dirname "$out_bundle")"
ditto "$arm_bundle" "$out_bundle"
out_bundle="$(cd "$out_bundle" && pwd -P)"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

merged=0
while IFS= read -r dst; do
    is_macho "$dst" || continue
    rel="${dst#$out_bundle/}"
    src="$x86_bundle/$rel"
    [[ -f "$src" ]] || { echo "error: $rel exists in the arm64 bundle but not in the x86_64 one" >&2; exit 1; }

    if has_arch "$dst" arm64 && has_arch "$dst" x86_64; then
        echo "  $rel: already universal, left as is"
        continue
    fi
    has_arch "$src" x86_64 || { echo "error: $rel in the x86_64 bundle has no x86_64 slice" >&2; exit 1; }

    slice_of "$dst" arm64  "$tmp/arm"
    slice_of "$src" x86_64 "$tmp/x86"
    lipo -create "$tmp/arm" "$tmp/x86" -output "$tmp/fat"
    cat "$tmp/fat" > "$dst" # overwrite in place to keep the original mode and ownership
    rm -f "$tmp/arm" "$tmp/x86" "$tmp/fat"
    merged=$((merged+1))
done < <(find "$out_bundle" -type f)

# A library present only on the x86_64 side would silently go missing: Homebrew can resolve a
# different dependency chain per architecture, and the plugin would then fail to load on Intel.
while IFS= read -r src; do
    is_macho "$src" || continue
    rel="${src#$x86_bundle/}"
    [[ -f "$out_bundle/$rel" ]] || { echo "error: $rel exists in the x86_64 bundle but not in the arm64 one" >&2; exit 1; }
done < <(find "$x86_bundle" -type f)

[[ "$merged" -gt 0 ]] || { echo "error: no Mach-O files found in $arm_bundle" >&2; exit 1; }

# lipo invalidates the ad-hoc signatures the per architecture builds carried.
codesign --force --deep --sign - "$out_bundle" >/dev/null 2>&1 || codesign --force --deep --sign - "$out_bundle"

echo "  $(basename "$out_bundle"): merged $merged Mach-O files into a universal bundle"
