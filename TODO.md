# TODO

## High Priority

- ✅ **SDL texture format bug** — fixed: `renderer.cpp` uses `SDL_PIXELFORMAT_IYUV` (I420,
  Y then U then V) matching libcamera's YUV420 planar output order.

- [ ] **libcamera Viewfinder format** — confirmed that Viewfinder role defaults to XRGB8888,
  not YUV420. `camera.cpp` already sets `pixelFormat = formats::YUV420` explicitly, which
  is correct, but after `validate()` the driver may adjust the format. Add a check that
  the configured format is actually YUV420 before starting the loop, and log a clear
  error if not.

- ✅ **`microscopi` user group membership** — implemented: `stage3-microscopi/01-microscopi/00-run.sh`
  runs `usermod -aG video,render,audio,input microscopi` in the chroot so the camera
  (`/dev/video*`) and DRM display (`/dev/dri/*`) are accessible without root.

- [ ] **SDL2 kmsdrm on Pi OS Bookworm** — confirm that SDL2 ships with the kmsdrm backend
  enabled in the Bookworm package. If not, build SDL2 from source with
  `--enable-video-kmsdrm`.

- [ ] **Change default root password** before any real deployment (currently `microscopy`).

## Medium Priority

- ✅ **Still capture quality** — implemented: `capture_still` now does a brief reconfigure
  to `StreamRole::StillCapture` (full sensor resolution), takes one frame, then restarts
  the viewfinder. The brief pause during still capture is expected and unavoidable on Pi 3
  without dual-stream; Pi 4+ could be upgraded to dual-stream later (see Option B above).

- [ ] **Still capture during video recording** — pressing Space during recording should save
  a JPEG without interrupting the video stream. Three implementation paths:

  - **Option A (recommended) — `std::thread` + libjpeg:** On Space press, copy the current
    YUV frame (~3 MB at 1080p) into a bounded async queue. A worker thread encodes to JPEG
    via libjpeg (~50–80 ms on Cortex-A53), injects EXIF, and saves. VideoCore handles H.264
    in parallel so no video frames are dropped. RAM cost: ~10–16 MB (libjpeg buffers + queued
    frame), freed after encode. **Avoid `fork()` without `exec`**: libcamera's internal threads
    vanish in the child with mutexes permanently locked, causing instant deadlock on any
    libcamera call. `fork()`→`exec()` avoids this but adds ~50–100 ms overhead per still and
    leaks all open DMA-BUF fds into the child until exec completes.

  - **Option B — libcamera dual-stream (Pi 4+ recommended):** Configure
    `StreamRole::Viewfinder` + `StreamRole::StillCapture` simultaneously for full sensor
    resolution (3280×2464 on v2 camera). Pi 3 may drop viewfinder frames at 1080p30 with
    dual-stream active; Pi 4+ handles it cleanly. Full-res JPEG encode (~400–800 ms on
    Cortex-A53) must run in the Option A worker thread regardless. Adds ~24 MB still buffer
    pool. Implement Option A first at viewfinder resolution; upgrade the frame source to
    Option B later.

  - **Option C (zero-cost fallback) — MKV timestamp marker:** Write a chapter/cue point into
    the MKV container on Space press; extract the nearest I-frames at shutdown via
    `ffmpeg -ss`. Zero I/O overhead during recording. Quality is bounded by H.264 bitrate and
    GOP structure — artefacts at high magnification, frame may be up to one GOP interval from
    the actual moment. Suitable as a supplementary annotation log alongside Option A.

- [ ] **Aperture control UX** — aperture steps (+1 f-stop per key press) may be too coarse.
  Consider logarithmic half-stop or third-stop increments and clamp to the lens's
  reported `ApertureFpsRange`.

- ✅ **Focus step size** — implemented: `focus_key_step` config key (default 0.05) sets the
  lens-position delta per keyboard Up/Down press. Scroll-wheel step remains separately
  configurable via `focus_scroll_step`.

- ✅ **OSD icon color** — implemented: all SVG icons in `assets/icons/` have `stroke="white"`,
  so rsvg-convert renders them white directly. No runtime tinting needed.

- [ ] **Font weight** — Roboto Condensed [wght] is a variable font. TTF_OpenFont loads at
  default weight (400). Expose weight selection or switch to a static Regular TTF.

- ✅ **Graceful camera re-init** — implemented: after 30 consecutive `get_frame()` failures,
  `main.cpp` calls `camera.stop()`, `camera.reconnect()` (releases and re-acquires the
  libcamera device), then `camera.start()`. Retries every 2 s until the camera returns.
  Any active recording is closed cleanly before the reconnect attempt.

- ✅ **Config hot-reload** — implemented: `kill -HUP <pid>` (or `systemctl reload microscopi`)
  reloads `microscopi.conf` at runtime. Hot-reloadable keys: `crop_*`, `focus_scroll_step`,
  `focus_key_step`, `show_crosshair`, `stills_dir`, `video_dir`. Camera/display/key settings
  require a full restart.

- ✅ **pi-gen STAGE_LIST** — implemented: `config/pi-gen/config` sets
  `STAGE_LIST="stage0 stage1 stage2 stage3-microscopi"` and `build-image.sh` copies
  the stage into pi-gen as `stage3-microscopi`. Standard stages 3–5 are SKIPped.

## Low Priority / Future

- ✅ **EXIF metadata on stills** — implemented: self-contained TIFF/APP1 writer injects
  aperture, shutter speed, ISO, lens position, camera model, exposure mode, and timestamp
  into captured JPEGs with no extra library dependencies.

- ✅ **Mouse scroll-wheel focus** — implemented: `SDL_MOUSEWHEEL` steps lens position by
  `focus_scroll_step` (default 0.01, configurable). Scroll also navigates the camera mode
  list when it is open.

- ✅ **Raw / JPEG capture toggle** — implemented: `capture_format` config key selects
  `jpeg` (default), `raw`, or `jpeg+raw`. Raw output uses `StreamRole::Raw` (native Bayer
  at full sensor resolution) saved as `<name>.raw` with a `<name>.raw.meta` sidecar
  (width, height, pixel format, stride) for decoding with dcraw / darktable / numpy.
  DNG output (proper metadata + color matrices) remains a future improvement.

- [ ] **USB webcam / V4L2 backend** — investigate replacing or supplementing libcamera with a
  V4L2 capture path so standard USB webcams (UVC devices) can be used as the image source.
  Useful for cheaper sensors, USB microscope cameras, or host-side development without a
  Pi camera module.

- [ ] **Histogram overlay** — add an optional small luminance histogram to the OSD.

- [ ] **Zoom / digital crop** — let the user zoom in on a region of the sensor using
  libcamera's `ScalerCrop` control.

- [ ] **Time-lapse mode** — capture a still every N seconds; configurable from conf.

- [ ] **Network streaming** — optional RTSP or HTTP-MJPEG stream for remote viewing.

- ✅ **Headless/remote control** — Unix domain socket at `/run/microscopi/microscopi.sock`
  (configurable via `socket_path` in `[remote]` config section). Line-based text protocol;
  connect with `socat - UNIX-CONNECT:/run/microscopi/microscopi.sock` or `nc -U`.
  Commands: `ping`, `status` (JSON), `still`, `record_start`, `record_stop`,
  `focus <pos|up|down>`, `iso <val|auto>`, `shutter <us>`, `mode <p|a|s|m>`,
  `af on|off`, `ae on|off`, `crosshair on|off`, `quit`, `help`.

- [ ] **Calibration image** — press a key to overlay a reference grid for focus calibration.

- [ ] **White balance control** — expose `AwbEnable` and manual `ColourGains`.

- ✅ **ISO display** — implemented: ISO is shown in the OSD bar (`osd.cpp`). Mapping is
  `ISO ≈ AnalogueGain × 100` (confirmed in `src/util/exposure.h`); displayed as AUTO or
  numeric value with a gauge icon.

- [ ] **Bluetooth image sync** — allow the user to pull captured stills/videos to a phone or
  laptop over Bluetooth without plugging in a cable. Options to explore: BlueZ OBEX Object
  Push (standard; works with Android and most desktops), or a lightweight custom BLE GATT
  service that advertises new files. Triggered either on demand (key binding) or
  automatically on Wi-Fi/BT connection. Would require `bluez` and `obexd` (or equivalent)
  in the pi-gen runtime package list.

## Build / CI

- ✅ **GitHub Actions binary build** — `.github/workflows/build.yml` builds the ARM64 binary
  via QEMU + Docker Buildx on push to `master` (src/CMakeLists.txt/docker/assets changes)
  and on `workflow_dispatch`. Uploads the binary as a 14-day artifact.

- [ ] **GitHub Actions full image build** — extend `build.yml` to run `build-image.sh` and
  upload the `.img.gz` to a GitHub Release (releases allow 2 GB; artifacts cap at 500 MB).
  Trigger manually via `workflow_dispatch` only — build takes ~40 min per run.

- [ ] Add a `scripts/dev-build.sh` that builds and runs a mock version on the host using
  a USB webcam (V4L2) + X11 backend for iterative UI development without a Pi.
