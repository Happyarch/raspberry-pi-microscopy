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
```bash
# Build binary inside ARM64 Docker container (QEMU-emulated on x86_64 host):
./scripts/build-app.sh
# Then build the full Pi OS image:
./scripts/build-image.sh
# Flash to SD:
./scripts/flash.sh /dev/sdX
```

Host prerequisites: `docker`, `qemu-user-static`, `binfmt-qemu-static` (Arch Linux).

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

## Permissions
When a task requires root privileges, do not attempt to run it or find a workaround. Instead, tell the user what needs to be done, provide the exact command, and explain what it does so they can run it themselves.

## Commit discipline
Commit after each meaningful change. Use descriptive messages. Do not amend published commits.

## TODO
See TODO.md for known issues and planned improvements.
