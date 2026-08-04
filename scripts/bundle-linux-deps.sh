#!/usr/bin/env bash
# Make a Linux VST3 portable: copy the FluidSynth and glib shared libraries next to the plugin .so
# and set RPATH to $ORIGIN, so the plugin loads without the user installing FluidSynth or glib.
# Host provided libraries (libc, X11, OpenGL, ALSA, libstdc++...) are intentionally NOT bundled;
# only the FluidSynth/glib family is, mirroring what the macOS bundler embeds. Needs patchelf.
#
# Usage: bundle-linux-deps.sh <path-to-.vst3-bundle-dir>
set -euo pipefail

bundle="${1:?usage: bundle-linux-deps.sh <bundle-dir>}"
command -v patchelf >/dev/null || { echo "patchelf not found" >&2; exit 1; }

so=$(find "$bundle/Contents" -maxdepth 2 -name '*.so' -type f | head -1)
[[ -n "$so" ]] || { echo "no plugin .so found under $bundle/Contents" >&2; exit 1; }
libdir="$(dirname "$so")"

# Only the FluidSynth and glib dependency chain is bundled; everything else is a host library.
is_bundled() {
    case "$(basename "$1")" in
        libfluidsynth*|libglib-*|libgobject-*|libgthread-*|libgmodule-*|libpcre2-*|libpcre.*|libffi.*|libintl*|libiconv*)
            return 0 ;;
        *) return 1 ;;
    esac
}

# Copy the bundled-family deps of a file into libdir, recursing into what we just copied.
declare -A done_copy
copy_deps() {
    local target="$1" dep base
    while read -r dep; do
        [[ -f "$dep" ]] || continue
        base="$(basename "$dep")"
        is_bundled "$base" || continue
        [[ -n "${done_copy[$base]:-}" ]] && continue
        done_copy[$base]=1
        if [[ ! -e "$libdir/$base" ]]; then
            cp -L "$dep" "$libdir/$base"
            chmod u+w "$libdir/$base"
        fi
        copy_deps "$libdir/$base"
    done < <(ldd "$target" 2>/dev/null | awk '/=>/ {print $3}')
}

copy_deps "$so"

# Every ELF in the plugin dir should look for its siblings in its own directory.
for f in "$libdir"/*.so*; do
    [[ -f "$f" ]] || continue
    patchelf --set-rpath '$ORIGIN' "$f" 2>/dev/null || true
done

echo "Linux bundle: FluidSynth/glib copied into $libdir, RPATH set to \$ORIGIN"
ls -1 "$libdir"
