# Web UI & Network Streaming

The microscopi app includes an HTTP-MJPEG server that exposes a browser-based
control interface and a raw camera stream. No separate software is needed —
open a browser on any device on the same network.

## Accessing the UI

```
http://<pi-ip>:8080/          Web UI — desktop, mobile, or tablet layout (auto-detected)
http://<pi-ip>:8080/stream    Raw MJPEG stream (works in <img> or mpv)
http://<pi-ip>:8080/api/status  JSON status snapshot (GET)
http://<pi-ip>:8080/api/<cmd>   Send a command (POST)
```

Default IP: `192.168.1.220`. Default port: `8080` (configurable).

The server detects the client's `User-Agent` and serves a mobile-optimised layout for phones
(FAB capture buttons, slide-up controls drawer, bottom tab bar) and the full desktop grid layout
for laptops and desktops. Tablets receive the mobile layout.

### Connecting headless (no WiFi configured)

If the Pi is powered on without a known WiFi network in range, it automatically starts a
**WiFi access point** approximately 45 seconds after boot:

| Setting | Value |
|---------|-------|
| SSID | `Microscopi` |
| Password | *(none — open network)* |
| Pi IP on AP | `192.168.42.1` |
| Web UI | `http://microscopi.local:8080/` or `http://192.168.42.1:8080/` |

Connect your phone, tablet, or laptop to the `Microscopi` WiFi network, then open the web UI.
`microscopi.local` resolves via the Pi's built-in DNS — no mDNS/Bonjour required on the client.

On a normal LAN, `microscopi.local` also resolves via mDNS (Avahi) on macOS, iOS, Android, and
Windows 10+.

> **Note:** The AP fallback is configured during the full image build (pi-gen). If you installed
> the `.deb` on an existing Pi OS system, copy the NetworkManager connection and service files
> from `config/pi-gen/stage3/01-microscopi/files/` manually:
> ```bash
> sudo install -m 600 microscopi-ap.nmconnection /etc/NetworkManager/system-connections/
> sudo install -m 644 nm-dnsmasq-microscopi.conf /etc/NetworkManager/dnsmasq-shared.d/microscopi.conf
> sudo install -m 755 microscopi-wifi-ap-check.sh /usr/local/bin/
> sudo install -m 644 microscopi-wifi-ap.service /etc/systemd/system/
> sudo systemctl enable --now microscopi-wifi-ap.service
> ```

## Downloading files

Files can be downloaded directly from the gallery in the web UI (⬇ Download button on each card).
Downloads are also available via direct URL:

```
GET /api/download/<media_id>           Download a still or video by media ID (Content-Disposition: attachment)
GET /api/download/session/<session_id> Download a timelapse session as a .tar.gz archive (streamed, no temp file)
```

A semaphore limits concurrent downloads to avoid overloading the Pi. Excess requests receive
HTTP 429. The limit is configurable in `microscopi.conf`:

```ini
[stream]
download_queue_max = 2   # default; raise on Pi 4+ for faster concurrent downloads
```

## Configuration (`[stream]` section in `microscopi.conf`)

| Key | Default | Description |
|-----|---------|-------------|
| `stream_port` | `8080` | TCP port for the HTTP/HTTPS server. |
| `stream_quality` | `75` | JPEG quality 1–100. Lower = less CPU + bandwidth. |
| `stream_scale` | `0.5` | Linear scale before encoding. `0.5` → half-resolution (~30 % CPU vs full). |
| `stream_fps` | `15` | Max frames/s delivered to MJPEG clients. Camera + encoder speed also cap this. Set to `0` for unlimited. |
| `stream_https` | `false` | Enable TLS. Browser will warn about self-signed certs unless you import the cert. |
| `stream_cert` | *(empty)* | Path to a PEM certificate file. Required when `stream_https = true`. |
| `stream_key` | *(empty)* | Path to a PEM private key file. Required when `stream_https = true`. |
| `download_queue_max` | `2` | Maximum simultaneous file downloads. Requests over this limit receive HTTP 429. |

All `[stream]` keys **except `stream_port`** are hot-reloadable via
`systemctl reload microscopi` (or `kill -HUP <pid>`). Port changes require a
full restart.

## Enabling HTTPS

HTTPS is off by default because the app is intended for local LAN use and
HTTP is sufficient. Enable it if you need encrypted transport (e.g. over a
bridged Wi-Fi or VPN where you can't guarantee confidentiality).

### 1. Generate a self-signed certificate

```bash
sudo mkdir -p /etc/microscopi
sudo openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 \
  -keyout /etc/microscopi/server.key \
  -out    /etc/microscopi/server.crt \
  -days 3650 -nodes -subj '/CN=microscopi'
sudo chmod 640 /etc/microscopi/server.key
sudo chown root:microscopi /etc/microscopi/server.key
```

### 2. Update the config

```ini
[stream]
stream_https = true
stream_cert  = /etc/microscopi/server.crt
stream_key   = /etc/microscopi/server.key
```

### 3. Restart the service

```bash
sudo systemctl restart microscopi
```

The startup log will confirm: `[mjpeg] listening on port 8080 (HTTPS)`.

### Trusting the certificate in your browser

Because the cert is self-signed, browsers will show a security warning.
To suppress it, import the `.crt` file into your OS/browser trust store:

- **Chrome/Edge (Linux)**: Settings → Privacy → Manage certificates → Authorities → Import
- **Firefox**: Settings → Privacy → View Certificates → Authorities → Import
- **macOS**: Drag `.crt` into Keychain Access, double-click → Trust → Always Trust
- **iOS**: AirDrop the `.crt` to the device → Settings → General → VPN & Device Management → trust

### Fallback behaviour

If `stream_https = true` but the cert or key file is missing/invalid, the
server automatically falls back to plain HTTP and logs the reason. The app
never refuses to start because of a TLS configuration error.

## REST API

All commands use `POST /api/<cmd>`. The command string is URL-encoded in the
path. The response is plain text.

| Command | Example | Effect |
|---------|---------|--------|
| `ping` | `POST /api/ping` | Returns `PONG`. Useful for connectivity checks. |
| `still` | `POST /api/still` | Capture a still. Returns `OK /path/to/file.jpg`. |
| `record_start` | `POST /api/record_start` | Start recording. Returns `OK`. |
| `record_stop` | `POST /api/record_stop` | Stop recording. Returns `OK /path/to/file.mkv`. |
| `focus <pos>` | `POST /api/focus%200.35` | Set lens position 0.0–1.0. |
| `focus up` | `POST /api/focus%20up` | Step focus toward infinity. |
| `focus down` | `POST /api/focus%20down` | Step focus toward macro. |
| `iso <val\|auto>` | `POST /api/iso%20400` | Set ISO or auto. |
| `shutter <us>` | `POST /api/shutter%2016667` | Set shutter speed in microseconds. |
| `mode <p\|a\|s\|m>` | `POST /api/mode%20s` | Switch exposure mode. |
| `af on\|off` | `POST /api/af%20on` | Toggle autofocus. |
| `quit` | `POST /api/quit` | Graceful shutdown. |
| `timelapse start [args]` | `POST /api/timelapse%20start%20base%3D5000` | Start timelapse. See [`docs/controls.md`](controls.md) for all arguments. |
| `timelapse stop` | `POST /api/timelapse%20stop` | Stop timelapse; renames session directory. |
| `timelapse status` | `POST /api/timelapse%20status` | Returns JSON with active state, count, function, and next-in countdown. |
| `cam_mode list` | `POST /api/cam_mode%20list` | Returns JSON array of `{index, label}` for all available camera modes. |
| `cam_mode status` | `POST /api/cam_mode%20status` | Returns `{index, label}` of the active mode. |
| `cam_mode set <N>` | `POST /api/cam_mode%20set%201` | Switch to camera mode by index (restarts sensor, rebuilds exposure ladders). |

`GET /api/status` returns a JSON object with current camera and timelapse state:

```json
{
  "mode": "P",
  "shutter_us": 16667,
  "iso": 0,
  "aperture": 0.0,
  "lens_pos": 0.42,
  "ae": true,
  "af": true,
  "recording": false,
  "still_count": 3,
  "dual_stream": false,
  "tl_active": false,
  "tl_count": 0,
  "tl_fn": "linear",
  "cam_mode_index": 0,
  "cam_mode_count": 4
}
```

`iso = 0` means auto. `lens_pos < 0` means AF is active and position is unknown. `tl_active` is true while a timelapse session is running.

## Streaming with external players

```bash
# mpv
mpv http://192.168.1.220:8080/stream

# ffmpeg record to file
ffmpeg -i http://192.168.1.220:8080/stream -c copy output.mjpeg

# SSH tunnel (access over the internet securely)
ssh -L 8080:localhost:8080 microscopi@192.168.1.220
# then open http://localhost:8080/ locally
```
