#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
lib="$src/build/libstl-preview-nautilus.so"
[ -f "$lib" ] || { echo 'Build with -DBUILD_NAUTILUS_EXTENSION=ON first.' >&2; exit 1; }
dir=$(pkg-config --variable=extensiondir libnautilus-extension-4)
[ -n "$dir" ] || { echo 'Nautilus development package is required.' >&2; exit 1; }
sudo install -m755 "$lib" "$dir/libstl-folder-preview.so"
echo 'Installed. Quit and reopen Files to load the direct menu entry.'
