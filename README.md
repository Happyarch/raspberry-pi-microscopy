# raspberry-pi-microscopy

A minimal Raspberry Pi 3 OS image and C++ camera application for microscopy.

The system boots directly into a full-screen live camera viewfinder with a Sony Alpha–style OSD. No desktop environment is installed. The `pi` user autologins and the microscopy app starts automatically via systemd.

## Features

- **Live viewfinder** — raw YUV420 camera feed displayed full-screen via SDL2 (kmsdrm backend, no X11)
- **Sony Alpha OSD** — translucent bottom bar showing aperture, exposure, focus mode, and still count; icons scale with display resolution
- **Automatic display resolution** — queries EDID at startup and selects the best supported mode up to 1080p (4:3 / 16:9 / 16:10, minimum 480p)
- **Hardware H.264 encoding** — VideoCore IV GPU encoder via `h264_v4l2m2m`; output in recoverable MKV format
- **Configurable video pipeline** — choose between the built-in encoder or a fully custom `ffmpeg` command in `microscopy.conf`
- **Runtime SVG icon rendering** — icons are rendered from SVG at the correct size for the display resolution via `rsvg-convert`, then cached per resolution
- **Minimal OS** — built with pi-gen Stage 2 (Raspberry Pi OS Lite) + a custom Stage 3

## Controls

| Key | Action |
|---|---|
| `↑` / `↓` | Aperture +/− |
| `Shift+↑` / `Shift+↓` | Focus +/− (switches to manual focus) |
| `a` | Toggle auto exposure |
| `Shift+a` | Toggle autofocus |
| `Space` | Capture still (saved to `~/stills/`) |
| `Shift+R` (hold 500ms) | Start/stop video recording (saved to `~/videos/`) |
| `c` | Toggle center guide overlay (circle + crosshair) |
| `q` / `Escape` | Quit |

While recording: a red dot flashes top-left with a `HH:MM:SS'AS` timestamp (arc-seconds, 1/60 s, divides cleanly at 30/60 fps).

## Building

### Prerequisites (Arch Linux host)

```bash
sudo pacman -S docker qemu-user-static binfmt-qemu-static
sudo systemctl restart systemd-binfmt
sudo systemctl start docker
```

### Build the binary

```bash
./scripts/build-app.sh
# Output: deploy/install/
```

### Build the OS image

```bash
./scripts/build-image.sh
# Output: deploy/pi-gen/deploy/*.img.xz
```

### Flash to SD card

```bash
./scripts/flash.sh /dev/sdX
```

## Configuration

Edit `/etc/microscopy.conf` on the Pi (or `config/microscopy.conf` in this repo for the default):

```ini
[video]
video_backend = builtin        # or: ffmpeg
builtin_bitrate = 5000000
ffmpeg_command = ffmpeg -f rawvideo -pix_fmt yuv420p -s {width}x{height} -r {fps} -i pipe:0 -c:v h264_v4l2m2m -b:v 5M -f matroska {output}

[camera]
camera_index = 0
fps = 30

[display]
fallback_width = 1280
fallback_height = 720
```

## Project Structure

```
src/
  main.cpp              Entry point and main loop
  camera/
    camera.h/cpp        libcamera C++ wrapper
    encoder.h/cpp       V4L2 h264_v4l2m2m + libavformat MKV muxer
  ui/
    renderer.h/cpp      SDL2 display init and frame presentation
    osd.h/cpp           Sony Alpha OSD (proportional sizing, SVG icons)
    input.h/cpp         Keyboard event handler (hold-to-record)
  config/
    config.h/cpp        INI config file parser
  util/
    resolution.h/cpp    EDID display mode selection

assets/
  icons/                Lucide SVG icons (MIT license)
  fonts/                Roboto Condensed (Apache 2.0)

config/
  microscopy.conf       Default runtime configuration
  pi-gen/               pi-gen build configuration and Stage 3

scripts/
  build-app.sh          Build binary in ARM64 Docker container
  build-image.sh        Build full Pi OS image with pi-gen
  flash.sh              Flash image to SD card

docker/
  Dockerfile.build      ARM64 Debian Bookworm build environment
```

## System details

- **Camera library**: libcamera (C++ API directly, no Python)
- **Display**: SDL2 with `SDL_VIDEODRIVER=kmsdrm` (DRM/KMS, no X11)
- **Video encode**: `h264_v4l2m2m` (VideoCore IV hardware) into MKV
- **Icons**: Lucide (MIT) — rendered from SVG to PNG at runtime via `rsvg-convert`, cached at `~/.cache/microscopy/{height}/`
- **Font**: Roboto Condensed (Apache 2.0)
- **User**: `pi` (no password, autologin); root password set to `microscopy` — **change before deployment**

## License

Source code: MIT. See `assets/icons/` and `assets/fonts/` for asset licenses.
