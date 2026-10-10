# STL Folder Preview

A small native Linux/Qt window for rendering STL thumbnails from a folder context menu. Surface angle colors use the same OpenGL shaders and camera as [STL Browser](https://github.com/JoeMirai/stl-browser), derived from [fstl](https://github.com/fstl-app/fstl).

Right click a local folder (or empty space inside it) in GNOME Files and choose **Render STL Preview**. The window shows a sample model and lets you choose:

- Surface angle colors (default), shaded polygons, wireframe, custom lighting, or solid with triangle edges.
- Isometric, top, front, or right camera view. Drag the sample to inspect it; batch renders use the selected camera preset.
- 256, 512, or 1024 pixel thumbnails.
- File size limit: 30 MiB by default, adjustable.
- Triangle limit: 2 million by default; 0 means unlimited. This skips complex meshes rather than simplifying geometry.

Press **Render** to process all `.stl` files directly in that folder, including uppercase extensions. Subfolders are excluded. Settings persist. The window closes automatically when rendering succeeds; write/render errors or a stopped batch keep it open. **Stop** stops after the current file; completed images are kept. Invalid or unreadable models are logged and skipped.

## Cache behavior

PNG images are stored in the standard size folders under `$XDG_CACHE_HOME/thumbnails` (normally `~/.cache/thumbnails`): `normal`, `large`, `x-large`, and `xx-large` with uncompressed PNG `tEXt` URI, modification time, and size metadata for desktop file managers. The app owns an index under its Qt cache directory, normally `~/.cache/JoeMirai/stl-folder-preview/index.json`.

Only pressing Render checks the index from previous runs and removes this app's thumbnails whose source files disappeared. Cleanup spans previously rendered folders, but it preserves thumbnails replaced by another app. Opening the window and the sample preview never cleans the cache. There is no timer, service, scheduled task, or background renderer. The small Nautilus extension only supplies a menu item.

Each render regenerates thumbnails with the chosen settings. Files modified during loading are skipped. Concurrent rendering windows are protected by a cache lock. An already open file-manager folder may need a refresh (Ctrl+R), and depend on Files' thumbnail preferences. STL Browser's own private cache is separate.

## Build

Ubuntu/Debian dependencies:

```sh
sudo apt install build-essential cmake qtbase5-dev libqt5opengl5-dev libgl-dev libnautilus-extension-dev pkg-config zlib1g-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_NAUTILUS_EXTENSION=ON
cmake --build build -j2
./scripts/install-local.sh
```

The user installer installs the app and a **Scripts → Render STL Preview** fallback without admin access. For the direct top-level menu entry, install the compiled extension in the directory reported by `pkg-config --variable=extensiondir libnautilus-extension-4`:

```sh
sudo install -m755 build/libstl-preview-nautilus.so "$(pkg-config --variable=extensiondir libnautilus-extension-4)/libstl-folder-preview.so"
nautilus --quit
```

Open Files again after installing the extension. The extension launches `~/.local/bin/stl-folder-preview` with the selected folder as a separate argument, including paths with spaces.

Launch directly:

```sh
~/.local/bin/stl-folder-preview /path/to/folder
~/.local/bin/stl-folder-preview --render-and-exit /path/to/folder
```

The second form uses saved settings and exits after rendering; it does not register background work.

## Verification

Run the integration test in a desktop session with OpenGL, or under Xvfb:

```sh
python3 tests/integration.py build/stl-folder-preview
```

The test uses temporary configuration and caches, and checks real OpenGL PNG output, PNG CRCs, actual GIO thumbnail validity at every size, size and triangle limits, uppercase extensions, exclusion of subfolders, changed rendering settings, Unicode paths, and cleanup only on the next render. It also checks that unrelated cache files survive.

To test auto-close using a real Render button click:

```sh
cmake -S . -B build -DBUILD_WINDOW_TEST=ON
cmake --build build -j2
./build/window-test
```

## Remove

Delete `~/.local/bin/stl-folder-preview`, `~/.local/lib/stl-folder-preview`, and `~/.local/share/nautilus/scripts/Render STL Preview`. Remove `/usr/lib/x86_64-linux-gnu/nautilus/extensions-4/libstl-folder-preview.so` with admin access, then restart Files. Cached images and settings can be kept or removed separately.

## License

MIT. The renderer in `src/` and shaders in `gl/` are adapted from STL Browser/fstl; the original license is retained in LICENSE.

Renderer source snapshot: STL Browser commit `b8bf7634a0910cd278e3fd0ba50804c4f45d6368`. The direct menu uses Nautilus's [menu provider interface](https://gnome.pages.gitlab.gnome.org/nautilus-python/class-nautilus-menu-provider.html); compiled extensions are loaded from the system extension directory ([Nautilus module loader](https://github.com/GNOME/nautilus/blob/main/src/nautilus-module.c)).
