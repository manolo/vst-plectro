#!/usr/bin/env bash
# Fetch the macOS build dependencies for one architecture into vendor/<subdir>/, so the plugin can
# be built for both arm64 and x86_64 on an Apple Silicon machine.
#
# They come from conda-forge rather than Homebrew because Homebrew switched off its Intel bottle
# builders on 2026-08-28: no formula we need has an x86_64 bottle any more, not even an old one, so
# `brew install glib` on an Intel target now means compiling glib from source. conda-forge still
# ships both architectures of the same version, and its libraries are already linked with @rpath
# instead of absolute /usr/local paths, which is what bundle-macos-deps.sh wants anyway.
#
# Usage: vendor-macos-deps.sh <osx-64|osx-arm64>
set -euo pipefail

subdir="${1:?usage: vendor-macos-deps.sh <osx-64|osx-arm64>}"
case "$subdir" in
    osx-64|osx-arm64) ;;
    *) echo "error: subdir must be osx-64 or osx-arm64" >&2; exit 1 ;;
esac

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$here/vendor/$subdir"

# Pinned so a build is reproducible and both architectures get the same sources. FluidSynth is
# built from source by CMake and needs only glib; the rest is glib's own dependency chain.
packages=(
    "libglib:2.90.0"
    "glib:2.90.0"      # only for lib/glib-2.0/include/glibconfig.h, which libglib leaves out
    "libintl:0.25.1"
    "libiconv:1.18"
    "pcre2:10.47"
)

if [[ -f "$out/.complete" ]]; then
    echo "vendor/$subdir already populated (delete $out to refetch)"
    exit 0
fi

command -v zstd >/dev/null 2>&1 || tar --zstd --help >/dev/null 2>&1 || {
    echo "error: need zstd to unpack .conda archives (brew install zstd)" >&2; exit 1
}

rm -rf "$out"
mkdir -p "$out"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

for spec in "${packages[@]}"; do
    name="${spec%%:*}"
    version="${spec##*:}"

    # conda-forge rebuilds a version several times; take the highest build number for this subdir.
    file="$(curl -fsSL "https://api.anaconda.org/package/conda-forge/$name" | python3 -c "
import json,sys
want_sub, want_ver = sys.argv[1], sys.argv[2]
fs = [f for f in json.load(sys.stdin)['files']
      if f.get('attrs',{}).get('subdir') == want_sub and f['version'] == want_ver]
if not fs:
    sys.exit('no build of %s %s for %s' % (sys.argv[3], want_ver, want_sub))
fs.sort(key=lambda f: f.get('attrs',{}).get('build_number', 0))
print(fs[-1]['basename'])
" "$subdir" "$version" "$name")"

    echo "  $name $version  ($(basename "$file"))"
    curl -fsSL -o "$tmp/pkg.conda" \
        "https://anaconda.org/conda-forge/$name/$version/download/$file"

    # A .conda is a zip holding pkg-*.tar.zst (payload) and info-*.tar.zst (metadata we ignore).
    rm -rf "$tmp/x"; mkdir "$tmp/x"
    unzip -qo "$tmp/pkg.conda" -d "$tmp/x"
    tar --zstd -xf "$tmp/x"/pkg-*.tar.zst -C "$out"
done

# conda ships only the versioned libraries (libglib-2.0.0.dylib), not the unversioned development
# symlink (libglib-2.0.dylib). FluidSynth's FindGLib2 takes the pkg-config result as a HINT and then
# calls find_library, which would miss the hinted directory and silently fall through to Homebrew,
# linking the host's arm64 glib into an x86_64 build. Add the symlinks so the hint actually hits.
for lib in "$out"/lib/*.dylib; do
    [[ -f "$lib" ]] || continue
    unversioned="${lib%.dylib}"            # libglib-2.0.0 / libintl.8
    unversioned="${unversioned%.[0-9]}"    # libglib-2.0   / libintl
    unversioned="${unversioned%.[0-9][0-9]}.dylib"
    [[ "$unversioned" != "$lib" && ! -e "$unversioned" ]] && ln -s "$(basename "$lib")" "$unversioned"
done

# conda pads prefix= in the .pc files with a long placeholder path that only exists on the builder.
# Point it at where the files actually landed so pkg-config resolves -L and -I correctly.
if [[ -d "$out/lib/pkgconfig" ]]; then
    for pc in "$out"/lib/pkgconfig/*.pc; do
        [[ -f "$pc" ]] || continue
        sed -i '' "s|^prefix=.*|prefix=$out|" "$pc"
    done
fi

touch "$out/.complete"

arch_got="$(lipo -archs "$out/lib/libglib-2.0.0.dylib" 2>/dev/null || echo unknown)"
want_arch="$([[ "$subdir" == "osx-64" ]] && echo x86_64 || echo arm64)"
[[ "$arch_got" == "$want_arch" ]] || {
    echo "error: fetched libglib is $arch_got, expected $want_arch" >&2; exit 1
}
echo "vendor/$subdir ready ($arch_got)"
