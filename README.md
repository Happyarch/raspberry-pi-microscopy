# raspberry-pi-microscopy

A minimal Raspberry Pi 3 OS image and C++ camera application for microscopy. The system boots directly into a full-screen live camera viewfinder with a Sony Alpha–style OSD. No desktop environment is installed — the `microscopi` user autologins and the app starts automatically via systemd.

## Downloads

Pre-built SD card images and binaries are available on the [Releases](../../releases) page.

## Features

- **Live viewfinder** — YUV420 camera feed displayed full-screen via SDL2 (kmsdrm backend, no X11 or Wayland)
- **Sony Alpha OSD** — translucent bottom bar showing exposure mode, shutter speed, ISO, focus mode, and still count; all elements scale proportionally to display resolution
- **Exposure modes** — Program (P), Aperture priority (A), Shutter priority (S), Manual (M)
- **ISO control** — AUTO or manual steps (100–6400); adjustable in any mode
- **Automatic display resolution** — queries EDID at startup and selects the best supported mode up to 1080p (4:3 / 16:9 / 16:10, minimum 480p)
- **Hardware H.264 encoding** — VideoCore IV GPU encoder via `h264_v4l2m2m`; output in crash-recoverable MKV format
- **Configurable video pipeline** — choose between the built-in libavformat muxer or a fully custom `ffmpeg` subprocess command
- **Rebindable key map** — all key bindings configurable in `microscopi.conf`
- **Runtime SVG icon rendering** — icons rendered from SVG to PNG at the correct size for the display via `rsvg-convert`, cached per resolution in `~/.cache/microscopi/`
- **Minimal OS** — pi-gen Stage 2 (Raspberry Pi OS Lite) + custom Stage 3; nothing else runs on boot

## Controls

All bindings are rebindable in the `[keys]` section of `microscopi.conf`. Defaults:

| Key | Action |
|---|---|
| `T` / `Shift+T` | Cycle exposure mode forward / backward (P→A→S→M) |
| `P` / `A` / `S` / `M` | Jump directly to that exposure mode |
| `↑` / `↓` | Focus step (switches to manual focus) |
| `Shift+↑` / `Shift+↓` | Shutter speed +/− one stop (S and M modes) |
| `←` / `→` | Aperture +/− (A and M modes; cosmetic on fixed-aperture lenses) |
| `I` / `Shift+I` | ISO up / down (all modes; first press exits auto-ISO) |
| `Shift+A` | Toggle autofocus on/off |
| `Space` | Capture still (saved to `~/stills/`) |
| `Shift+R` (hold 500 ms) | Start / stop video recording (saved to `~/videos/`) |
| `C` | Toggle center guide overlay (circle + crosshair) |
| `H` (hold 3 s) | Show key binding help overlay |
| `Esc` or `Q` (hold 5 s) | Quit (warning shown after 2.5 s) |

While recording: a red dot flashes top-left with a `HH:MM:SS'AS` timestamp (arc-seconds = 1/60 s, divides evenly at 30 and 60 fps with integer arithmetic only).

## Building

### Prerequisites (Arch Linux host)

```bash
sudo pacman -S docker qemu-user-static binfmt-qemu-static
sudo systemctl restart systemd-binfmt
sudo systemctl start docker
```

### Build the binary

Compiles inside an ARM64 Debian Bookworm Docker container (QEMU-emulated on x86_64):

```bash
./scripts/build-app.sh
# Output: deploy/install/usr/local/
```

### Build the full OS image

```bash
./scripts/build-image.sh
# Output: deploy/pi-gen/deploy/*.img.xz
```

### Flash to SD card

```bash
./scripts/flash.sh /dev/sdX
```

### LSP / IDE support

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
ln -sf build/compile_commands.json compile_commands.json
```

## Configuration

`/etc/microscopi.conf` on the Pi (or `config/microscopi.conf` in this repo for the build-time default):

```ini
[video]
video_backend = builtin        # builtin or ffmpeg
builtin_bitrate = 5000000      # bits/s for builtin backend
ffmpeg_command = ffmpeg -f rawvideo -pix_fmt yuv420p -s {width}x{height} -r {fps} -i pipe:0 -c:v h264_v4l2m2m -b:v 5M -f matroska {output}
video_dir = /home/microscopi/videos
stills_dir = /home/microscopi/stills

[camera]
camera_index = 0
fps = 30
initial_ae_enabled = true
initial_af_enabled = true

[display]
fallback_width = 1280
fallback_height = 720
show_crosshair = false

[keys]
# All values: lowercase letter/digit, or: up down left right space escape return
# Prefix with "shift+" for shift-modified bindings (e.g. "shift+t")
mode_cycle_fwd  = t
mode_cycle_back = shift+t
mode_p = p
mode_a = a
mode_s = s
mode_m = m
iso_up = i
iso_down = shift+i
shutter_up = shift+up
shutter_down = shift+down
focus_up = up
focus_down = down
aperture_up = right
aperture_down = left
toggle_af = shift+a
still = space
record = shift+r
crosshair = c
quit = escape
help = h
```

## Project structure

```
src/
  main.cpp                Entry point, main loop, exposure mode state machine
  camera/
    camera.h / camera.cpp libcamera C++ wrapper; controls: AE, AF, shutter, ISO
    encoder.h / encoder.cpp V4L2 h264_v4l2m2m + libavformat MKV muxer
  ui/
    renderer.h / cpp      SDL2 window, IYUV texture, frame presentation
    osd.h / cpp           Sony Alpha OSD; proportional sizing, SVG icons, overlays
    input.h / cpp         Table-driven key dispatch; hold timers for record/quit/help
  config/
    config.h / cpp        INI parser; KeyMap loaded into InputHandler at startup
  util/
    resolution.h / cpp    EDID display mode selection via SDL2

assets/
  icons/                  Lucide SVG icons (ISC license)
  fonts/                  Roboto Condensed (Apache 2.0)

config/
  microscopi.conf         Default runtime configuration (installed to /etc/)
  pi-gen/                 pi-gen build config and Stage 3 scripts

scripts/
  build-app.sh            Build binary in ARM64 Docker container
  build-image.sh          Build full Pi OS image with pi-gen
  build-image-debug.sh    Debug image with gdb, debug symbols, crash tooling
  flash.sh                Flash image to SD card

docker/
  Dockerfile.build        ARM64 Debian Bookworm build environment
```

## System details

| Component | Choice |
|---|---|
| Camera API | libcamera C++ (not Python picamera2) |
| Display | SDL2 `kmsdrm` backend — no X11 or Wayland |
| Video encode | `h264_v4l2m2m` (VideoCore IV) into MKV |
| OSD font | Roboto Condensed via SDL2_ttf |
| Icons | Lucide SVGs → PNG via `rsvg-convert` at runtime, cached per display height |
| Service user | `microscopi` (no password, autologin on tty1) |

> **Security note:** change the root password before any network-connected deployment.

## License

See [LICENSE](LICENSE).

Asset licenses: `assets/icons/` — ISC (Lucide); `assets/fonts/` — Apache 2.0 (Roboto Condensed).
