#!/bin/sh
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$HOME/.local/bin" "$HOME/.local/lib/stl-folder-preview" "$HOME/.local/share/nautilus/scripts"
install -m755 "$src/build/stl-folder-preview" "$HOME/.local/lib/stl-folder-preview/stl-folder-preview"
# Optional extra runtime libraries for a locally extracted build toolchain.
if [ -n "${STL_PREVIEW_RUNTIME_DIR:-}" ]; then
    install -m755 "$STL_PREVIEW_RUNTIME_DIR/libOpenGL.so.0" "$HOME/.local/lib/stl-folder-preview/libOpenGL.so.0"
fi
cat > "$HOME/.local/bin/stl-folder-preview" <<'WRAPPER'
#!/bin/sh
export LD_LIBRARY_PATH="$HOME/.local/lib/stl-folder-preview${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HOME/.local/lib/stl-folder-preview/stl-folder-preview" "$@"
WRAPPER
chmod +x "$HOME/.local/bin/stl-folder-preview"
install -m755 "$src/scripts/Render STL Preview" "$HOME/.local/share/nautilus/scripts/Render STL Preview"
printf 'Installed. Right click a folder → Scripts → Render STL Preview.\n'
