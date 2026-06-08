# TODO

## High Priority

- ✅ **SDL texture format bug** — fixed: `renderer.cpp` uses `SDL_PIXELFORMAT_IYUV` (I420,
  Y then U then V) matching libcamera's YUV420 planar output order.

- ✅ **libcamera Viewfinder format** — after `cam_->configure()` returns, `camera.cpp` now
  checks `cfg_->at(0).pixelFormat == formats::YUV420` and logs a clear error + returns
  false if the driver adjusted it away. Prevents silent garbage output in both the renderer
  and the MJPEG encoder.

- ✅ **`microscopi` user group membership** — implemented: `stage3-microscopi/01-microscopi/00-run.sh`
  runs `usermod -aG video,render,audio,input microscopi` in the chroot so the camera
  (`/dev/video*`) and DRM display (`/dev/dri/*`) are accessible without root.

- ✅ **SDL2 kmsdrm on Pi OS Bookworm** — confirmed: `libsdl2-2.0-0` 2.26.5+dfsg-1 on
  Bookworm includes the kmsdrm backend (`KMSDRM_VideoInit` present in the shared library,
  `libdrm2` and `libgbm1` listed as package dependencies). No source build needed.

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

- ✅ **Aperture control UX** — `kApertureMaster` expanded to 31 standard 1/3-stop values
  (f/1.0 … f/32.0, ISO/CIPA convention). `aperture_index()` now uses log2 distance so
  one 1/3-stop step is equally sensitive across the full f-number range. Clamped to
  `aperture_range()` min/max when the lens reports a range (Pi cameras return fixed
  aperture; ladder is empty and keys are no-ops until a future camera exposes control).

- ✅ **Focus step size** — implemented: `focus_key_step` config key (default 0.05) sets the
  lens-position delta per keyboard Up/Down press. Scroll-wheel step remains separately
  configurable via `focus_scroll_step`.

- ✅ **OSD icon color** — implemented: all SVG icons in `assets/icons/` have `stroke="white"`,
  so rsvg-convert renders them white directly. No runtime tinting needed.

- ✅ **Font weight** — replaced the 362 KB variable font (fvar wght 100–900) with a static
  Regular (wght=400) instance generated via `fonttools varLib.instancer wght=400` (141 KB).
  SDL2_ttf now renders the intended weight reliably. Apache 2.0 license notice preserved in
  the font's name table; no code changes required.

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

- ✅ **Network streaming** — HTTP-MJPEG stream at `http://<pi>:8080/stream`; browser
  web UI at `http://<pi>:8080/`; REST control at `POST /api/<cmd>` + `GET /api/status`.
  Headless operation supported: if SDL2/kmsdrm is unavailable (no display attached), the
  app continues — camera runs, MJPEG streams, Unix socket control works. Config: `[stream]`
  section with `stream_port`, `stream_quality`, `stream_scale`, `stream_fps`. JPEG encoded
  with libjpeg-turbo (direct YUV420 input). Stream config is hot-reloadable via SIGHUP.

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
