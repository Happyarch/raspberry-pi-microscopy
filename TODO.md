# TODO

## High Priority

- [ ] **SDL texture format bug** — `renderer.cpp` uses `SDL_PIXELFORMAT_YV12` (YVU order) but
  libcamera delivers YUV420 planar (Y, U, V order). Should be `SDL_PIXELFORMAT_IYUV`.
  With YV12, U and V planes are swapped, producing wrong chroma (purple/green tint).
  Fix: change `SDL_PIXELFORMAT_YV12` → `SDL_PIXELFORMAT_IYUV` in `renderer.cpp`.

- [ ] **libcamera Viewfinder format** — confirmed that Viewfinder role defaults to XRGB8888,
  not YUV420. `camera.cpp` already sets `pixelFormat = formats::YUV420` explicitly, which
  is correct, but after `validate()` the driver may adjust the format. Add a check that
  the configured format is actually YUV420 before starting the loop, and log a clear
  error if not.

- [ ] **`microscopi` user group membership** — add `microscopi` to the `video` and `render`
  groups in `stage3/01-run.sh` so the camera and DRM device nodes are accessible without
  root. Without this the app will fail to open `/dev/video*` and the DRM display.

- [ ] **SDL2 kmsdrm on Pi OS Bookworm** — confirm that SDL2 ships with the kmsdrm backend
  enabled in the Bookworm package. If not, build SDL2 from source with
  `--enable-video-kmsdrm`.

- [ ] **Change default root password** before any real deployment (currently `microscopy`).

## Medium Priority

- [ ] **Still capture quality** — `camera.cpp::capture_still` currently saves the viewfinder
  frame as JPEG. A proper implementation should switch to `StillCapture` role for full
  sensor resolution. Implement dual-stream (viewfinder + stills) or a brief reconfigure.

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

- [ ] **Focus step size** — 0.05 lens-position increments may be too coarse at macro distances.
  Consider making step size configurable in `microscopi.conf`.

- [ ] **OSD icon color** — Lucide icons are black by default; they need to be white for the
  dark OSD bar. Either tint via `SDL_SetTextureColorMod(tex, 255, 255, 255)` after inverting,
  or pre-process the SVGs to use `stroke="white"` before calling rsvg-convert.

- [ ] **Font weight** — Roboto Condensed [wght] is a variable font. TTF_OpenFont loads at
  default weight (400). Expose weight selection or switch to a static Regular TTF.

- [ ] **Graceful camera re-init** — if the camera is unplugged and re-plugged, the app
  currently crashes. Add a reconnect loop in `main.cpp`.

- [ ] **Config hot-reload** — allow editing `microscopi.conf` and reloading without a restart
  (e.g., on SIGHUP).

- [ ] **pi-gen STAGE_LIST** — verify that pi-gen accepts `stage3-microscopy` as a custom stage
  name; the standard convention uses `stage3`. Rename if needed.

## Low Priority / Future

- ✅ **EXIF metadata on stills** — implemented: self-contained TIFF/APP1 writer injects
  aperture, shutter speed, ISO, lens position, camera model, exposure mode, and timestamp
  into captured JPEGs with no extra library dependencies.

- ✅ **Mouse scroll-wheel focus** — implemented: `SDL_MOUSEWHEEL` steps lens position by
  `focus_scroll_step` (default 0.01, configurable). Scroll also navigates the camera mode
  list when it is open.

- [ ] **Raw / JPEG capture toggle** — allow toggling between JPEG-only, DNG/raw-only, and
  raw+JPEG (simultaneous). Raw output would use the StillCapture role with DNG encoding
  via libcamera or ffmpeg. Configurable per-session key or config file option.

- [ ] **USB webcam / V4L2 backend** — investigate replacing or supplementing libcamera with a
  V4L2 capture path so standard USB webcams (UVC devices) can be used as the image source.
  Useful for cheaper sensors, USB microscope cameras, or host-side development without a
  Pi camera module.

- [ ] **Histogram overlay** — add an optional small luminance histogram to the OSD.

- [ ] **Zoom / digital crop** — let the user zoom in on a region of the sensor using
  libcamera's `ScalerCrop` control.

- [ ] **Time-lapse mode** — capture a still every N seconds; configurable from conf.

- [ ] **Network streaming** — optional RTSP or HTTP-MJPEG stream for remote viewing.

- [ ] **Headless/remote control** — accept commands via a Unix socket so a companion
  script or web interface can trigger captures without physical keyboard access.

- [ ] **Calibration image** — press a key to overlay a reference grid for focus calibration.

- [ ] **White balance control** — expose `AwbEnable` and manual `ColourGains`.

- [ ] **ISO display** — add ISO to the OSD bar once the analogue gain → ISO mapping is confirmed.

- [ ] **Bluetooth image sync** — allow the user to pull captured stills/videos to a phone or
  laptop over Bluetooth without plugging in a cable. Options to explore: BlueZ OBEX Object
  Push (standard; works with Android and most desktops), or a lightweight custom BLE GATT
  service that advertises new files. Triggered either on demand (key binding) or
  automatically on Wi-Fi/BT connection. Would require `bluez` and `obexd` (or equivalent)
  in the pi-gen runtime package list.

## Build / CI

- [ ] Set up GitHub Actions workflow to build the Docker image and validate compilation
  (cannot run on x86_64 without QEMU, but at minimum check the CMake configure step).

- [ ] **GitHub Actions full image build** — on push to `main`, run `build-app-debug.sh` then
  `build-image-debug.sh` via `docker/setup-qemu-action` (ARM64 binfmt on Ubuntu runner).
  Upload the `.img.gz` to a GitHub Release rather than an artifact (releases allow 2 GB;
  artifacts cap at 500 MB). Trigger manually via `workflow_dispatch` to avoid burning the
  2,000 min/month free-tier on every push. Build takes ~40 min per run.

- [ ] Add a `scripts/dev-build.sh` that builds and runs a mock version on the host using
  a USB webcam (V4L2) + X11 backend for iterative UI development without a Pi.
