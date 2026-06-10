# Controls Reference

Complete reference for all keyboard controls, timelapse functions, remote commands, and configuration options. All key bindings shown are defaults and can be changed in the `[keys]` section of `microscopi.conf`.

---

## Keyboard Controls

### Exposure Mode

| Key | Action |
|-----|--------|
| `C` | Cycle mode forward: P → A → S → M → P |
| `Shift+C` | Cycle mode backward: P → M → S → A → P |
| `P` | Jump directly to Program mode |
| `A` | Jump directly to Aperture-priority mode |
| `S` | Jump directly to Shutter-priority mode |
| `M` | Jump directly to Manual mode |

**P — Program:** Camera controls both shutter speed and gain automatically. Good general-purpose starting point.

**A — Aperture-priority:** AE adjusts shutter and gain. On fixed-aperture lenses (all Pi camera modules) the aperture value is cosmetic. If ISO is set to a specific value (not AUTO), AE locks that gain and adjusts shutter only.

**S — Shutter-priority:** AE is disabled. You set shutter speed; ISO must also be set manually. Use for controlling motion blur at known magnifications with repeatable exposure.

**M — Manual:** AE is disabled. You control both shutter speed and ISO. Essential for quantitative fluorescence or drift-free timelapse exposure.

---

### Focus

| Key | Action |
|-----|--------|
| `↑` | Step lens toward macro (shorter focus distance); exits AF |
| `↓` | Step lens toward infinity (longer focus distance); exits AF |
| `F` | Toggle autofocus (AF) on/off |
| Mouse wheel | Scroll to adjust focus (same step size as keyboard) |

Lens position is a normalized value `0.0` (infinity) to `1.0` (minimum focus distance / macro). Steps are configurable: `focus_key_step` (default `0.05`) and `focus_scroll_step` (default `0.01`).

When AF is active the OSD shows **AF**. When manual it shows **MF**. Pressing `↑` or `↓` always switches to manual focus immediately.

---

### Shutter Speed

| Key | Action |
|-----|--------|
| `Shift+↑` | Shutter speed up one stop (shorter exposure) |
| `Shift+↓` | Shutter speed down one stop (longer exposure) |

Only active in **S** and **M** modes. Steps follow a standard 1/3-stop ladder from 1/4000 s to 2 s.

---

### Aperture

| Key | Action |
|-----|--------|
| `→` | Aperture up one 1/3 stop (wider) |
| `←` | Aperture down one 1/3 stop (narrower) |

Pi camera modules have a fixed aperture — these keys set the OSD display value (useful when you attach a manual lens adapter) but do not affect the sensor. If a camera reports an aperture range, the keys apply it.

The ladder covers **f/1.0 – f/32** in 1/3-stop steps (ISO/CIPA convention, 31 values). Nearest-neighbor search uses log₂ distance so each step is equally sensitive across the entire f-number range.

---

### ISO

| Key | Action |
|-----|--------|
| `I` | ISO up one step |
| `Shift+I` | ISO down one step |

ISO is cyclic. Steps snap to standard values: 100, 125, 160, 200, 250, 320, 400, 500, 640, 800, 1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6400. Pressing `I` up from the maximum (6400) wraps back to AUTO. Pressing `Shift+I` down from AUTO wraps to the maximum; pressing down from the minimum (100) also returns to AUTO.

---

### Capture

| Key | Action |
|-----|--------|
| `Space` | Capture still |
| `Shift+R` (hold ≥ 500 ms) | Start / stop video recording |
| `T` (tap) | Open timelapse dialog (when idle) |
| `T` (hold ≥ 3 s, while running) | Stop timelapse |

**Still capture:** Briefly reconfigures the sensor to full-resolution `StillCapture` mode, takes one frame, injects EXIF metadata (aperture, shutter, ISO, lens position, exposure mode, timestamp, camera model), then returns to the viewfinder. Saved to `~/stills/YYYYMMDD_HHMMSS.jpg`. Format is configurable: `jpeg`, `raw`, or `jpeg+raw`.

**Video recording:** Hold `Shift+R` for 500 ms — a red arc grows in the top-left corner while held. On release the recording starts (or stops if already recording). A blinking red dot and `HH:MM:SS'AS` timestamp appear top-left. The timestamp format uses arc-seconds (1/60 s) as the sub-second unit: it divides evenly at both 30 fps and 60 fps using integer arithmetic only. Saved to `~/videos/YYYYMMDD_HHMMSS.mkv`.

**Timelapse:** See the [Timelapse Mode](#timelapse-mode) section below. Tap `T` to open the timelapse dialog: enter an interval in seconds and an optional max frame count (0 = unlimited), Tab to switch between fields, Enter to start, Esc to cancel. While timelapse is running, hold `T` for 3 s to stop — an amber arc grows in the top bar as you hold. Timelapse and video recording are mutually exclusive.

---

### UI Controls

| Key | Action |
|-----|--------|
| `X` | Toggle center guide overlay (circle + crosshair) |
| `V` | Open / close camera resolution list |
| `H` | Show key binding overlay (auto-dismisses after 4 s; press again to close early) |
| `Esc` or `Q` (hold ≥ 5 s) | Quit (warning text appears after 2.5 s) |

---

## Timelapse Mode

Timelapse captures a still at a configurable interval and stores frames in a named session directory. It inherits the current camera state — whatever exposure mode, AF, focus position, and white balance are active at the moment timelapse starts.

### Starting from the keyboard

Tap `T` to open the timelapse dialog. Enter an interval in seconds (minimum 0.5 s, default 5 s) and an optional max frame count (0 = unlimited, default 0). Tab to switch between the two fields; Enter to confirm and start; Esc to cancel.

The keyboard dialog sets the linear base interval and frame cap only. For advanced interval schedule functions (`exp_grow`, `logistic`, etc.) or fine-grained parameter tuning, use the socket or web UI instead.

To stop a running timelapse from the keyboard, hold `T` for 3 s — an amber arc in the top-right of the OSD grows as you hold, then the session stops and the session directory is renamed.

### Session directories

**With RTC / NTP** (`tl_use_rtc = true`, default):
- Created as `~/timelapses/YYYYMMDD_HHMMSS/`
- Renamed to `YYYYMMDD_HHMMSS--YYYYMMDD_HHMMSS` when stopped
- Files: `frame_000001.jpg`, `frame_000002.jpg`, … (sequential, ffmpeg-compatible)

**Without RTC** (`tl_use_rtc = false` — for Pi units with no clock and no NTP):
- Created as `~/timelapses/tl_{monotonic_ms}/`
- Renamed to `01h23m45s678ms` (elapsed duration) when stopped
- Files: `t0000005123.jpg` (10-digit milliseconds since session start — sortable by capture time)

If the app crashes during a timelapse, the start-only directory name remains. Frames already saved are intact.

### Post-processing to video

```bash
# Assemble frames into a 24 fps video (with-RTC naming)
ffmpeg -r 24 -i 'frame_%06d.jpg' -c:v libx264 -crf 18 timelapse.mp4

# Without-RTC naming (glob sort, then pipe)
ls t*.jpg | sort | ffmpeg -f concat -safe 0 -i <(printf "file '%s'\n" $(ls t*.jpg | sort)) \
  -c:v libx264 -crf 18 timelapse.mp4
```

### Interval schedule functions

All functions compute `I(n)` — the wait in milliseconds before the (n+1)-th capture, where `n` = frames already taken. The result is always clamped to `[tl_floor_ms, tl_ceil_ms]`.

| `tl_fn` value | Formula | Rate law | Growth |
|---------------|---------|----------|--------|
| `linear` | `B` | zero-order | constant |
| `exp_grow` | `B · eᵏⁿ` | first-order | exponential ↑ |
| `exp_decay` | `B · e⁻ᵏⁿ` | first-order | exponential ↓ |
| `log` | `B + k·ln(n+1)` | sublinear | slow ↑ |
| `power` | `B + k·nᵖ` | power-law | polynomial ↑ |
| `quadratic` | `B + k·n²` | power alias (p=2) | polynomial ↑ |
| `cubic` | `B + k·n³` | power alias (p=3) | polynomial ↑ |
| `quintic` | `B + k·n⁵` | power alias (p=5) | polynomial ↑ |
| `michaelis` | `B + (C-B)·kn/(1+kn)` | MM saturation | soft-ceil ↑ |
| `logistic` | `B + (C-B)·σ(k(n-m))` | sigmoid / Hill | S-curve ↑ |
| `stretched_exp` | `B · exp(k·nᵝ)` | KWW dispersive | sub-exp ↑ |
| `hyperbolic` | `B / (1+kn)` | 2nd-order | hyperbolic ↓ |

**Parameters:**

| Config key | CLI arg | Default | Meaning |
|-----------|---------|---------|---------|
| `tl_base_ms` | `base=N` | 5000 | `B` — interval at n=0 for all functions (ms) |
| `tl_rate_constant` | `k=V` | 0.05 | `k` — rate constant; units depend on function |
| `tl_power` | `power=P` | 2.0 | `p` — exponent for `fn=power`; overridden by aliases |
| `tl_beta` | `beta=V` | 0.7 | `β` — stretch exponent for `fn=stretched_exp` (0 < β ≤ 1) |
| `tl_inflection` | `inflection=N` | 50 | `m` — frame at sigmoid midpoint for `fn=logistic` |
| `tl_floor_ms` | `floor=N` | 2000 | Hard minimum interval (ms); mandatory for decay modes |
| `tl_ceil_ms` | `ceil=N` | 300000 | Hard maximum interval (ms); mandatory for growth modes |
| `tl_max_frames` | `max=N` | 0 | Stop after N frames (0 = unlimited) |

**Choosing `k`:**

| Function | `k` units | Example |
|----------|-----------|---------|
| `exp_grow` / `exp_decay` | nepers/frame | `k=0.1` → each frame ~10% longer/shorter; half-life = ln2/k ≈ 6.9 frames |
| `log` / `power` | ms/frameᵖ | `k=100` with p=2 → +100 ms at n=1, +400 ms at n=2, +900 ms at n=3 |
| `michaelis` / `hyperbolic` | 1/frame | `k=0.1` → half-saturation or half-decay at n=10 |
| `logistic` | 1/frame (steepness) | `k=0.1` gives a gradual S-curve; `k=0.5` is steep |
| `stretched_exp` | nepers/frameᵝ | `k=0.05, β=0.7` grows slower than pure exp for small n |

**When to use each function:**

- **`linear`** — General purpose. Unknown kinetics, simple timelapse. Only mode with guaranteed constant CPU load.
- **`exp_grow`** — First-order processes: fluorescence bleaching, drug clearance, mRNA decay. Equal density per octave of time (logarithmic time axis).
- **`exp_decay`** — Accelerating events: bacteria entering log-phase, autocatalytic reactions post-induction. Floor is essential.
- **`log`** — Safe non-linear default. Dense early coverage, slow thinning, very low runaway risk.
- **`power`** — Burst then wait. Initial event is brief, followed by long stasis. Use low p (1.5–2) for moderate thinning; p=5 hits ceiling within ~7 frames even at k=10.
- **`michaelis`** — Enzyme kinetics, receptor binding, nutrient-limited growth. Naturally bounded by B and ceil — no runaway possible.
- **`logistic`** — Lag → explosion → plateau (bacterial growth curves, cooperative binding, epidemic models). Set `tl_inflection` to the frame where the event peaks.
- **`stretched_exp`** — Heterogeneous/crowded systems: FRAP in dense cytoplasm, anomalous diffusion, multi-phase fluorescence lifetime decays.
- **`hyperbolic`** — Gentler decay than exponential. Good for autocatalytic events with damping.

---

## Remote Control

### Unix Socket

Connect with:
```bash
socat - UNIX-CONNECT:/run/microscopi/microscopi.sock
# or
nc -U /run/microscopi/microscopi.sock
```

Line-based text protocol. Each line is a command; each response is a line.

### REST API (HTTP)

```bash
curl -X POST "http://192.168.1.220:8080/api/still"
curl "http://192.168.1.220:8080/api/status"
```

### Full Command Reference

| Command | Response | Description |
|---------|----------|-------------|
| `ping` | `PONG` | Connectivity check |
| `status` | JSON | Full camera + timelapse state |
| `still` | `OK /path/to/file.jpg` | Capture still to `stills_dir` |
| `record_start` | `OK /path/to/file.mkv` | Start video recording |
| `record_stop` | `OK` | Stop recording and close file |
| `focus <0.0–1.0>` | `OK` | Set lens position |
| `focus up` | `OK` | Step focus toward infinity |
| `focus down` | `OK` | Step focus toward macro |
| `iso <value\|auto>` | `OK` | Set ISO (e.g. `iso 400`, `iso auto`) |
| `shutter <us>` | `OK` | Set shutter speed in microseconds (e.g. `shutter 16667`) |
| `mode <p\|a\|s\|m>` | `OK` | Set exposure mode |
| `af on\|off` | `OK` | Toggle autofocus |
| `crosshair on\|off` | `OK` | Toggle crosshair overlay |
| `timelapse start [args]` | `OK fn=… base=…ms` | Start timelapse (see below) |
| `timelapse stop` | `OK` | Stop timelapse and rename session dir |
| `timelapse status` | JSON | Timelapse state + countdown |
| `quit` | `OK` | Graceful shutdown |
| `help` | `OK …` | List available commands |

**Timelapse start arguments** (all optional, override config for this session only):

```
timelapse start base=5000 fn=exp_grow k=0.05 max=200 floor=2000 ceil=60000
timelapse start base=10000 fn=logistic k=0.1 inflection=30 max=100
timelapse start fn=quadratic base=3000 k=50 ceil=120000
```

**`timelapse status` JSON:**

```json
{
  "active": true,
  "count": 42,
  "fn": "exp_grow",
  "base_ms": 5000,
  "k": 0.05,
  "floor_ms": 2000,
  "ceil_ms": 300000,
  "interval_ms": 18473,
  "next_in_ms": 6201
}
```

**`status` JSON (full):**

```json
{
  "mode": "M",
  "iso": 400,
  "shutter_us": 16667,
  "aperture": 0.0,
  "lens_pos": 0.42,
  "af": false,
  "ae": false,
  "recording": false,
  "still_count": 7,
  "dual_stream": false,
  "tl_active": true,
  "tl_count": 42,
  "tl_fn": "exp_grow"
}
```

`iso = 0` → auto. `lens_pos < 0` → AF active, position unknown. `aperture = 0` → fixed-aperture lens.

### Hot-reload

Send `SIGHUP` (or `systemctl reload microscopi`) to reload `microscopi.conf` at runtime without restarting. Hot-reloadable keys: `crop_*`, `focus_scroll_step`, `focus_key_step`, `show_crosshair`, `stills_dir`, `video_dir`, `stream_quality`, `stream_fps`, `stream_scale`.

Camera, display, key bindings, and timelapse directory changes require a full restart.

---

## Web UI

Open `http://<pi-ip>:8080/` in any browser. The page works on phones and tablets. The live MJPEG stream is embedded at the top; controls are below.

| Panel | Controls |
|-------|----------|
| **Capture** | Still button; Record start/stop (turns red when active) |
| **Mode** | P / A / S / M buttons; AF toggle |
| **Focus** | Slider (0=∞ to 100=macro); Near / Far step buttons |
| **ISO** | Dropdown (AUTO, 100–6400) |
| **Shutter** | Dropdown (1/4000 s – 2 s) |
| **Timelapse** | Start/Stop button (turns amber when active); interval (s) and max-frames inputs; live frame counter and function name |

The OSD bar at the bottom of the stream image mirrors the on-device display: mode, shutter, ISO, aperture, focus position. A blinking **REC** indicator and amber **TL** indicator appear when recording or timelapse are active.

For HTTPS setup and advanced streaming options see [`docs/web-ui.md`](web-ui.md).
