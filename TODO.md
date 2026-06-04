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

- [ ] **`pi` user group membership** — add `pi` to the `video` and `render` groups in
      `stage3/01-run.sh` so the camera and DRM device nodes are accessible without root.
      Without this the app will fail to open `/dev/video*` and the DRM display.
- [ ] **SDL2 kmsdrm on Pi OS Bookworm** — confirm that SDL2 ships with the kmsdrm backend
      enabled in the Bookworm package. If not, build SDL2 from source with `--enable-video-kmsdrm`.
- [ ] **`pi` user group membership** — add `pi` to the `video` and `render` groups in
      `stage3/01-run.sh` so the camera and DRM device nodes are accessible without root.
- [ ] **Change default root password** before any real deployment (currently `microscopy`).

## Medium Priority

- [ ] **Still capture quality** — `camera.cpp::capture_still` currently saves the viewfinder
      frame as JPEG. A proper implementation should switch to `StillCapture` role for full
      sensor resolution. Implement dual-stream (viewfinder + stills) or a brief reconfigure.
- [ ] **Aperture control UX** — aperture steps (+1 f-stop per key press) may be too coarse.
      Consider logarithmic half-stop or third-stop increments and clamp to the lens's
      reported `ApertureFpsRange`.
- [ ] **Focus step size** — 0.05 lens-position increments may be too coarse at macro distances.
      Consider making step size configurable in `microscopy.conf`.
- [ ] **OSD icon color** — Lucide icons are black by default; they need to be white for the
      dark OSD bar. Either tint via `SDL_SetTextureColorMod(tex, 255, 255, 255)` after inverting,
      or pre-process the SVGs to use `stroke="white"` before calling rsvg-convert.
- [ ] **Font weight** — Roboto Condensed [wght] is a variable font. TTF_OpenFont loads at
      default weight (400). Expose weight selection or switch to a static Regular TTF.
- [ ] **Graceful camera re-init** — if the camera is unplugged and re-plugged, the app
      currently crashes. Add a reconnect loop in `main.cpp`.
- [ ] **Config hot-reload** — allow editing `microscopy.conf` and reloading without a restart
      (e.g., on SIGHUP).
- [ ] **pi-gen STAGE_LIST** — verify that pi-gen accepts `stage3-microscopy` as a custom stage
      name; the standard convention uses `stage3`. Rename if needed.

## Low Priority / Future

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

## Build / CI

- [ ] Set up GitHub Actions workflow to build the Docker image and validate compilation
      (cannot run on x86_64 without QEMU, but at minimum check the CMake configure step).
- [ ] Add a `scripts/dev-build.sh` that builds and runs a mock version on the host using
      a USB webcam (V4L2) + X11 backend for iterative UI development without a Pi.
