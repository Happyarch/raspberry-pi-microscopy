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

### Versioning

The canonical version lives in `VERSION` at the repo root (e.g. `0.9.0-rc1`). `build-deb.sh` reads it first and falls back to `git describe --tags` if the file is absent. Dashes are replaced with dots for dpkg compatibility: `0.9.0-rc1` → `0.9.0.rc1`. Update `VERSION` before tagging a release.

### Docker build: `--provenance=false` required

`docker buildx build --load` with Buildx ≥ 0.10 produces an OCI image index (manifest list) by default due to provenance attestations. `docker create` cannot resolve an image index for a non-native platform (arm64 on x86_64), causing the extract step to fail. All `docker buildx build` invocations in `build-app.sh` and every CI workflow pass `--provenance=false` to force a plain manifest. Do not remove this flag.

### .deb vs full image
- **`.deb`** — install/upgrade the app on an existing Pi OS Bookworm system: `sudo dpkg -i microscopi_*_arm64.deb`. Fast iteration path.
- **Full image** — complete bespoke OS built with pi-gen; used for shipping SD cards. Slow (pi-gen clones and debootstraps from scratch).

The `.deb` postinst creates the `microscopi` user, output dirs, and enables the systemd service on first install. The full image does the same via `config/pi-gen/stage3/01-microscopi/00-run.sh`.

### .deb runtime dependencies
Derived from `readelf -d` on the compiled binary — update if linking changes:
`libcamera0.5`, `libcamera-ipa`, `libsdl2-2.0-0`, `libsdl2-ttf-2.0-0`, `libsdl2-image-2.0-0`, `libavformat59`, `libavcodec59`, `libturbojpeg0`, `libssl3`, `libsqlite3-0`, `libstdc++6`, `libgcc-s1`, `libc6`, `librsvg2-bin`, `v4l-utils`

For clangd LSP: `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && ln -sf build/compile_commands.json compile_commands.json`

## CI/CD Workflows

All workflows live in `.github/workflows/`. The table below summarises triggers and purpose.

| Workflow | File | Trigger | Purpose |
|---|---|---|---|
| **Binary build** | `build.yml` | `workflow_dispatch`, `release: published` | ARM64 Docker build + .deb; uploads both as GHA artifacts |
| **Docker image** | `docker.yml` | `v[0-9]+.0.0` tag push, `workflow_dispatch` | Publishes multi-arch image to `ghcr.io`; only on major version tags |
| **Release** | `release.yml` | `v*` tag push | Three-job pipeline: build → Pi image → GitHub release with .deb + .img.xz attached |
| **Build Pi image** | `build-image.yml` | `workflow_dispatch` | Standalone on-demand Pi image build; artifact retained 14 days |
| **Attach image** | `attach-image.yml` | `workflow_dispatch` (inputs: `source_run_id`, `release_tag`) | Downloads a pi-image artifact from a prior run and uploads it to an existing release |

### Pi image jobs: `qemu-user-static` via apt, not `setup-qemu-action`

Pi-gen's `build-docker.sh` calls `which qemu-aarch64-static` to find the host binary and copy it into the Docker build context for debootstrap chroots. `docker/setup-qemu-action@v3` registers binfmt via a Docker container but does **not** install the binary on the runner's filesystem. The image jobs therefore install via apt:

```yaml
- name: Install qemu-user-static
  run: |
    sudo apt-get update -qq
    sudo apt-get install -y qemu-user-static
```

Using `setup-qemu-action` alone will produce `qemu-aarch64-static not found` from pi-gen every time.

### Disk space

Pi-gen image builds consume ~14 GB. Image jobs free space before running pi-gen by removing dotnet, android SDK, GHC, PowerShell, and Swift from the runner, then pruning unused Docker images (`docker image prune -af`).

### Layer caching

Binary build jobs use a local cache (`/tmp/.buildx-cache`) with a rotate pattern (write to `-new`, then `mv -new`) to prevent unbounded growth. The Docker image workflow uses GHA registry cache (`type=gha`) instead, which is more efficient for the ghcr.io publish path.

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
- The web UI detects the client's `User-Agent` and serves a mobile-optimised HTML (FABs, drawer, bottom nav) for phones/tablets, and the full desktop grid for laptops/desktops.
- Timelapse sessions are served as `.tar.gz` via a forked `tar` subprocess piped directly to the HTTP socket (no temp file, no new library dependency).
- The download semaphore (`active_downloads_` atomic + `download_queue_max` config key) protects the Pi from simultaneous large downloads.
- WiFi AP fallback: a systemd oneshot service (`microscopi-wifi-ap.service`) starts a NetworkManager AP connection (`microscopi-ap.nmconnection`) 45 s after boot if `wlan0` is not associated. The AP is open (no password), SSID `Microscopi`, IP `192.168.42.1`. NM's dnsmasq resolves `microscopi.local` for clients on the AP. Avahi handles `microscopi.local` on a normal LAN.
- **Gallery tile sizing** uses `tile_w = min(dw/5, dh/4)` so that wider-but-shorter displays don't get oversized tiles that result in *fewer* visible tiles than a smaller screen. After computing `tiles_per_row` and the "< 6 tiles" adjustment block, `rows_visible` is always re-clamped to ≥ 1 (it must be clamped both before and after the adjustment, because the block recomputes `rows_visible`). See `src/ui/gallery.cpp`.

## pi-gen runtime packages (new additions)
- `iw` — used by the WiFi AP fallback script to detect wlan0 association
- `avahi-daemon` — mDNS for `microscopi.local` on LAN
- `dnsmasq` — pulled in by NM as a dependency; also used by NM's `ipv4.method=shared` AP mode

## Pi user account
The Pi user is `microscopi` (not `pi`). SSH and scp target `microscopi@192.168.1.220`.

## Permissions
When a task requires root privileges, do not attempt to run it or find a workaround. Instead, tell the user what needs to be done, provide the exact command, and explain what it does so they can run it themselves.

## Testing
Any major C++ code — new modules, non-trivial algorithms, data structures, parsers — must include tests in `tests/` as part of the verification stage of its plan. Tests are compiled natively on the host (no cross-compilation needed for pure logic tests) and are not required for thin SDL2/libcamera glue that can only run on the hardware.

### Framework
Tests use **GTest** (Google Test), not `assert()`-based plain C++. Each test file links against `GTest::gtest_main`.

### Test targets
| Target | File(s) tested | Notes |
|---|---|---|
| `test_timelapse` | `src/timelapse/timelapse.cpp` | Interval schedule functions |
| `test_config` | `src/config/config.cpp` | INI parser, default key map |
| `test_gallery_layout` | `src/ui/gallery.cpp` | `gallery_compute_layout()` geometry |
| `test_mjpeg_server` | `src/server/mjpeg_server.cpp` | Also needs `blurhash.cpp`, `media_db.cpp` |
| `test_mjpeg_media` | `src/server/mjpeg_media.cpp` | Also needs `blurhash.cpp`, `media_db.cpp` |

`test_mjpeg_server` and `test_mjpeg_media` are conditional on `SQLite3_FOUND` (the `find_package(SQLite3)` block in `tests/CMakeLists.txt` must appear before these targets).

### CI test dependencies
The `tests.yml` workflow installs:
```
cmake ninja-build
libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
libturbojpeg0-dev
libssl-dev
libsqlite3-dev
```

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
