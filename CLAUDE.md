# CLAUDE.md — Project context for Claude Code

## What this project is
A minimal Raspberry Pi 3 OS image and C++ camera application for microscopy. The app is the only thing that runs on boot (no desktop). Source is in `src/`, build artifacts go to `deploy/`.

## Language and stack
- **C++17** — all application code
- **libcamera** (C++ API directly, not Python picamera2)
- **SDL2** with `SDL_VIDEODRIVER=kmsdrm` — no X11 or Wayland
- **ffmpeg / libavformat** — MKV muxing and optional ffmpeg subprocess encoding
- **V4L2** — direct H264 hardware encoding via `h264_v4l2m2m` (VideoCore IV)
- **rsvg-convert** — SVG → PNG rendering at runtime (Lucide icons, cached per display height)
- **SDL2_ttf** — OSD text (Roboto Condensed variable font)

## Build

Host prerequisites: `docker`, `qemu-user-static`, `binfmt-qemu-static` (Arch Linux).

```bash
# 1. Build binary inside ARM64 Docker container (QEMU-emulated on x86_64 host).
#    Output: deploy/install/usr/local/
./scripts/build-app.sh

# 2a. Package as a .deb for direct installation on a Pi running Pi OS Bookworm.
#     Output: deploy/microscopi_<version>_arm64.deb
#     Requires only: ar, tar, gzip — no dpkg needed on the host.
./scripts/build-deb.sh

# 2b. OR build a full Pi OS image (includes pi-gen; much slower).
#     Output: deploy/pi-gen/deploy/*.img.xz
./scripts/build-image.sh

# Flash image to SD (image build path only):
./scripts/flash.sh /dev/sdX
```

### .deb vs full image
- **`.deb`** — install/upgrade the app on an existing Pi OS Bookworm system: `sudo dpkg -i microscopi_*_arm64.deb`. Fast iteration path.
- **Full image** — complete bespoke OS built with pi-gen; used for shipping SD cards. Slow (pi-gen clones and debootstraps from scratch).

The `.deb` postinst creates the `microscopi` user, output dirs, and enables the systemd service on first install. The full image does the same via `config/pi-gen/stage3/01-microscopi/00-run.sh`.

### .deb runtime dependencies
Derived from `readelf -d` on the compiled binary — update if linking changes:
`libcamera0.5`, `libcamera-base0.5`, `libcamera-ipa`, `libsdl2-2.0-0`, `libsdl2-ttf-2.0-0`, `libsdl2-image-2.0-0`, `libavformat59`, `libavcodec59`, `libturbojpeg0`, `libssl3`, `libsqlite3-0`, `libstdc++6`, `libgcc-s1`, `libc6`, `librsvg2-bin`, `v4l-utils`

For clangd LSP: `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && ln -sf build/compile_commands.json compile_commands.json`

## Project layout
```
src/camera/      libcamera wrapper + V4L2/ffmpeg encoder
src/ui/          SDL2 renderer, Sony Alpha OSD, input handler
src/config/      INI config parser
src/util/        SDL2 display mode / resolution selector
assets/icons/    Lucide SVG icons (MIT)
assets/fonts/    Roboto Condensed TTF (Apache 2.0)
config/          microscopy.conf (default) + pi-gen stage files
docker/          ARM64 build container
scripts/         Build, image, flash scripts
```

## Key design decisions
- OSD sizing is **proportional to display height** (`bar_h = dh/14`). Never use fixed pixel values for OSD elements.
- Icons are rendered SVG→PNG at **runtime** via `rsvg-convert`, cached to `~/.cache/microscopy/{display_height}/`. If a new display height is seen, icons are re-rendered and cached.
- Video recording requires **Shift+R held ≥ 500ms** to avoid accidental triggers. A progress arc grows in the OSD while held.
- Video timestamps use **arc-seconds** (1/60 s) as the sub-second unit: `HH:MM:SS'AS`. This divides exactly at both 30fps and 60fps using integer arithmetic only.
- Video is saved as **MKV** (crash-recoverable).
- The `video_backend` config key selects `builtin` (V4L2 + libavformat) or `ffmpeg` (subprocess pipe with configurable command).
- Still capture currently saves the viewfinder frame as JPEG. Full-resolution StillCapture is in TODO.md.

## Pi user account
The Pi user is `microscopi` (not `pi`). SSH and scp target `microscopi@192.168.1.220`.

## Permissions
When a task requires root privileges, do not attempt to run it or find a workaround. Instead, tell the user what needs to be done, provide the exact command, and explain what it does so they can run it themselves.

## Testing
Any major C++ code — new modules, non-trivial algorithms, data structures, parsers — must include tests in `tests/` as part of the verification stage of its plan. The existing test in `tests/test_timelapse.cpp` is the model: plain C++17, no framework, `assert()`-based, compiled natively on the host (no cross-compilation needed for pure logic tests).

Tests are not required for thin SDL2/libcamera glue code that can only be exercised on the hardware, but any logic that can be isolated should be.

## Documentation
After verifying a feature works, update all documentation that is affected. This list is representative, not exhaustive — use judgement about what changed:

- **`TODO.md`** — mark completed items done, add newly discovered issues
- **`README.md`** — user-facing feature descriptions, install steps, quick-start
- **`CLAUDE.md`** (this file) — stack changes, new design decisions, updated build steps, new dependencies
- **`docs/controls.md`** — any new or changed keyboard bindings, commands, or config keys
- **`docs/web-ui.md`** — web UI or API changes
- **`config/microscopi.conf`** — new config keys with commented defaults

## Commit discipline
Commit after each meaningful change. Use descriptive messages. Do not amend published commits.

## TODO
See TODO.md for known issues and planned improvements.
