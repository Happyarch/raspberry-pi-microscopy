#include "mjpeg_server.h"
#include "media_db.h"

#include <arpa/inet.h>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <turbojpeg.h>

// ---------------------------------------------------------------------------
// Embedded web UI — keep in sync with assets/web/index.html
// ---------------------------------------------------------------------------
static const char kWebUiHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>Microscopi</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#111;color:#ddd;font-family:system-ui,sans-serif;font-size:14px;user-select:none}
/* Top nav */
#topnav{display:flex;gap:4px;padding:6px 10px;background:#1a1a1a;border-bottom:1px solid #2a2a2a}
.nav-btn{background:none;border:none;color:#777;font-size:13px;padding:4px 14px;cursor:pointer;border-radius:4px;transition:background .15s}
.nav-btn:hover{background:#252525;color:#bbb}
.nav-btn.active{color:#fff;background:#2e2e2e}
/* Viewer */
#viewer{position:relative;background:#000;display:flex;justify-content:center;align-items:center;min-height:40vw}
#stream{max-width:100%;max-height:70vh;display:block;object-fit:contain}
/* OSD overlay — mirrors the physical bar */
#osd{
  position:absolute;bottom:0;left:0;right:0;
  background:rgba(0,0,0,.65);padding:5px 10px;
  display:flex;gap:14px;align-items:center;flex-wrap:wrap;
  font-size:12px;font-family:'Roboto Condensed','Arial Narrow',monospace;letter-spacing:.02em
}
.oitem{display:flex;flex-direction:column;align-items:center;gap:1px}
.oitem .lbl{font-size:9px;text-transform:uppercase;color:#666;letter-spacing:.06em}
.oitem .val{color:#fff;font-weight:500}
.oitem .val.dim{color:#666}
#rec{margin-left:auto;color:#f44;font-weight:700;display:none}
/* Status message */
#msg{padding:5px 10px;min-height:22px;font-size:12px;color:#888}
/* Controls grid */
#ctrl{padding:10px;display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:8px}
.grp{background:#1a1a1a;border-radius:6px;padding:9px}
.grp h3{font-size:10px;text-transform:uppercase;color:#555;letter-spacing:.09em;margin-bottom:7px}
button{
  background:#2a2a2a;color:#ccc;border:1px solid #333;border-radius:4px;
  padding:6px 8px;cursor:pointer;font-size:13px;width:100%;margin-top:4px;transition:background .1s
}
button:hover{background:#333}
button:active{background:#444}
button.on{background:#1a5c30;border-color:#2a8a50;color:#7fc}
button.rec-on{background:#5c1a1a;border-color:#8a3030;color:#faa}
button.tl-on{background:#3a2e00;border-color:#7a5e00;color:#fca}
.numrow{display:flex;align-items:center;gap:4px;margin-top:4px}
.numrow label{font-size:11px;color:#666;white-space:nowrap}
.numrow input{flex:1;background:#2a2a2a;color:#ccc;border:1px solid #333;border-radius:4px;padding:4px 6px;font-size:13px;width:0}
.row{display:flex;gap:5px}
.row button{flex:1}
input[type=range]{width:100%;margin:5px 0;accent-color:#4af;cursor:pointer}
select{
  width:100%;background:#2a2a2a;color:#ccc;border:1px solid #333;
  border-radius:4px;padding:5px;font-size:13px;margin-top:4px;cursor:pointer
}
/* Gallery */
#gallery-view{display:none;padding:10px}
.gallery-bar{display:flex;gap:5px;align-items:center;margin-bottom:10px;flex-wrap:wrap}
.gtab{background:none;border:1px solid #333;color:#777;padding:3px 12px;border-radius:4px;cursor:pointer;font-size:12px;width:auto;margin-top:0}
.gtab.active{color:#fff;border-color:#555}
.gtab:hover{background:#252525}
#scan-btn{margin-left:auto;width:auto;font-size:12px;padding:3px 12px;margin-top:0}
#gallery-breadcrumb{font-size:12px;color:#888;margin-bottom:8px;display:none}
#gallery-breadcrumb a{color:#5af;text-decoration:none;cursor:pointer}
#gallery-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:6px}
.gcard{background:#1a1a1a;border-radius:6px;overflow:hidden;cursor:pointer;transition:background .12s}
.gcard:hover{background:#232323}
.gthumb{width:100%;aspect-ratio:1;background:#111;display:flex;align-items:center;justify-content:center;overflow:hidden}
.gthumb img{width:100%;height:100%;object-fit:cover;display:block}
.gthumb-icon{font-size:36px;color:#444}
.gmeta{padding:5px 7px 7px}
.gname{font-size:11px;color:#bbb;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.gdate{font-size:10px;color:#555;margin-top:2px}
#gallery-pagination{display:flex;align-items:center;gap:10px;margin-top:12px;font-size:12px;color:#666}
#gallery-pagination button{width:auto;padding:3px 12px;margin-top:0;font-size:12px}
#gallery-pagination button:disabled{opacity:.35;cursor:default}
/* Lightbox */
#lightbox{display:none;position:fixed;inset:0;z-index:200;align-items:center;justify-content:center;flex-direction:column}
#lb-bg{position:absolute;inset:0;background:rgba(0,0,0,.93)}
#lb-img{position:relative;max-width:95vw;max-height:88vh;object-fit:contain}
#lb-close{position:absolute;top:10px;right:16px;background:none;border:none;color:#fff;font-size:30px;cursor:pointer;line-height:1;width:auto;padding:0}
#lb-caption{position:absolute;bottom:8px;left:0;right:0;text-align:center;font-size:12px;color:#aaa;pointer-events:none}
</style>
</head>
<body>

<div id="topnav">
  <button class="nav-btn active" id="nav-live" onclick="showView('live')">Live</button>
  <button class="nav-btn" id="nav-gallery" onclick="showView('gallery')">Gallery</button>
</div>

<div id="live-view">
  <div id="viewer">
    <img id="stream" src="/stream" alt="Connecting to camera…">
    <div id="osd">
      <div class="oitem"><div class="lbl">mode</div><div class="val" id="o-mode">P</div></div>
      <div class="oitem"><div class="lbl">shutter</div><div class="val dim" id="o-shut">---</div></div>
      <div class="oitem"><div class="lbl">iso</div><div class="val dim" id="o-iso">AUTO</div></div>
      <div class="oitem"><div class="lbl">aperture</div><div class="val" id="o-ap">f/--</div></div>
      <div class="oitem"><div class="lbl">focus</div><div class="val" id="o-foc">AF</div></div>
      <div id="rec">&#9679; REC <span id="rec-t">00:00</span></div>
      <div id="tl-ind" style="display:none">&#9654; TL <span id="tl-n">0</span></div>
    </div>
  </div>

  <div id="msg"></div>

  <div id="ctrl">

    <div class="grp">
      <h3>Capture</h3>
      <button id="btn-still">&#128247; Still</button>
      <button id="btn-rec">&#9654; Record</button>
    </div>

    <div class="grp">
      <h3>Mode</h3>
      <div class="row">
        <button id="mp" onclick="setMode('p')">P</button>
        <button id="ma" onclick="setMode('a')">A</button>
        <button id="ms" onclick="setMode('s')">S</button>
        <button id="mm" onclick="setMode('m')">M</button>
      </div>
      <button id="btn-af" onclick="toggleAf()">AF: ON</button>
    </div>

    <div class="grp">
      <h3>Focus</h3>
      <input type="range" id="fslider" min="0" max="100" value="50">
      <div class="row">
        <button onclick="cmd('focus down')">&#8593; Near</button>
        <button onclick="cmd('focus up')">&#8595; Far</button>
      </div>
    </div>

    <div class="grp">
      <h3>ISO</h3>
      <select id="iso-sel" onchange="setIso(this.value)">
        <option value="auto">AUTO</option>
        <option value="100">100</option>
        <option value="200">200</option>
        <option value="400">400</option>
        <option value="800">800</option>
        <option value="1600">1600</option>
        <option value="3200">3200</option>
        <option value="6400">6400</option>
      </select>
    </div>

    <div class="grp">
      <h3>Shutter</h3>
      <select id="shut-sel" onchange="setShutter(this.value)">
        <option value="500000">2"</option>
        <option value="250000">1"</option>
        <option value="125000">1/8</option>
        <option value="50000">1/20</option>
        <option value="33333">1/30</option>
        <option value="16667" selected>1/60</option>
        <option value="8333">1/120</option>
        <option value="4000">1/250</option>
        <option value="2000">1/500</option>
        <option value="1000">1/1000</option>
        <option value="500">1/2000</option>
        <option value="250">1/4000</option>
      </select>
    </div>

    <div class="grp">
      <h3>Timelapse</h3>
      <button id="btn-tl" onclick="toggleTl()">&#9654; Start</button>
      <div id="tl-status" style="font-size:11px;color:#888;margin-top:5px;min-height:14px"></div>
      <div class="numrow"><label>Interval (s)</label><input type="number" id="tl-iv" min="2" max="3600" value="5" step="1"></div>
      <div class="numrow"><label>Max frames</label><input type="number" id="tl-max" min="0" max="99999" value="0" title="0 = unlimited"></div>
    </div>

  </div>
</div><!-- #live-view -->

<div id="gallery-view">
  <div class="gallery-bar">
    <button class="gtab active" id="gtab-stills"     onclick="setGTab('stills')">Stills</button>
    <button class="gtab"        id="gtab-videos"     onclick="setGTab('videos')">Videos</button>
    <button class="gtab"        id="gtab-timelapses" onclick="setGTab('timelapses')">Timelapses</button>
    <button id="scan-btn" onclick="doScan()">Scan</button>
  </div>
  <div id="gallery-breadcrumb">
    <a onclick="exitSession()">&#8592; Sessions</a>
    &nbsp;<span id="sess-name"></span>
  </div>
  <div id="gallery-grid"></div>
  <div id="gallery-pagination">
    <button id="prev-btn" onclick="changePage(-1)" disabled>&#8592; Prev</button>
    <span id="page-info">Page 1</span>
    <button id="next-btn" onclick="changePage(1)" disabled>Next &#8594;</button>
  </div>
</div>

<div id="lightbox">
  <div id="lb-bg" onclick="closeLightbox()"></div>
  <img id="lb-img" src="" alt="">
  <button id="lb-close" onclick="closeLightbox()">&#x2715;</button>
  <div id="lb-caption"></div>
</div>

<script>
'use strict';
const $ = id => document.getElementById(id);
const msg = t => { $('msg').textContent = t; };

let af = true, recording = false, recStart = 0, recTick = null, tl_active = false;

// --- View switching ---
function showView(v) {
  $('live-view').style.display    = v === 'live'    ? '' : 'none';
  $('gallery-view').style.display = v === 'gallery' ? '' : 'none';
  $('nav-live').classList.toggle('active',    v === 'live');
  $('nav-gallery').classList.toggle('active', v === 'gallery');
  if (v === 'gallery') loadGallery();
}

// --- API call ---
async function cmd(c) {
  try {
    const r = await fetch('/api/' + encodeURIComponent(c), {method:'POST'});
    const t = (await r.text()).trim();
    msg(t);
    return t;
  } catch(e) { msg('Connection lost'); return 'ERR'; }
}

// --- Controls ---
function setMode(m) {
  cmd('mode ' + m);
  ['p','a','s','m'].forEach(x => $('m'+x).classList.remove('on'));
  $('m'+m).classList.add('on');
}
function toggleAf() {
  af = !af;
  cmd('af ' + (af?'on':'off'));
  $('btn-af').textContent = 'AF: ' + (af?'ON':'OFF');
  $('btn-af').classList.toggle('on', af);
}
function setIso(v) { cmd(v==='auto'?'iso auto':'iso '+v); }
function setShutter(us) { cmd('shutter '+us); }

// Focus slider with debounce
let fTimer = null;
$('fslider').addEventListener('input', function() {
  const pos = (this.value/100).toFixed(2);
  clearTimeout(fTimer);
  fTimer = setTimeout(() => cmd('focus '+pos), 80);
});

// Still capture
$('btn-still').addEventListener('click', async function() {
  this.disabled = true;
  msg('Capturing…');
  const r = await cmd('still');
  this.disabled = false;
  if (r.startsWith('OK ')) msg('Saved: ' + r.slice(3).trim().split('/').pop());
});

// Record toggle
$('btn-rec').addEventListener('click', async function() {
  if (!recording) {
    const r = await cmd('record_start');
    if (r.startsWith('OK')) {
      recording = true; recStart = Date.now();
      this.innerHTML = '&#9646;&#9646; Stop';
      this.classList.add('rec-on');
      $('rec').style.display = 'block';
      recTick = setInterval(tickRec, 1000);
    }
  } else {
    await cmd('record_stop');
    stopRec();
  }
});

function tickRec() {
  const s = Math.floor((Date.now()-recStart)/1000);
  $('rec-t').textContent = String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0');
}
function stopRec() {
  recording = false;
  clearInterval(recTick); recTick = null;
  $('btn-rec').innerHTML = '&#9654; Record';
  $('btn-rec').classList.remove('rec-on');
  $('rec').style.display = 'none';
}

function toggleTl() {
  if (!tl_active) {
    const iv = Math.max(2, parseInt($('tl-iv').value) || 5);
    const max = parseInt($('tl-max').value) || 0;
    cmd('timelapse start base=' + (iv * 1000) + ' max=' + max);
  } else { cmd('timelapse stop'); }
}
function startTlUi(count) {
  tl_active = true;
  $('btn-tl').innerHTML = '&#9646;&#9646; Stop'; $('btn-tl').classList.add('tl-on');
  $('tl-ind').style.display = 'block';
  $('tl-n').textContent = count || 0; $('tl-status').textContent = (count || 0) + ' frames';
}
function stopTlUi() {
  tl_active = false;
  $('btn-tl').innerHTML = '&#9654; Start'; $('btn-tl').classList.remove('tl-on');
  $('tl-ind').style.display = 'none'; $('tl-status').textContent = '';
}

// --- Status polling ---
function fmtShutter(us) {
  if (!us || us <= 0) return '---';
  const s = us/1e6;
  return s < 0.5 ? '1/'+Math.round(1/s) : s.toFixed(1)+'"';
}

async function pollStatus() {
  try {
    const r = await fetch('/api/status');
    const st = await r.json();

    $('o-mode').textContent = st.mode||'--';
    const sm = st.mode==='S'||st.mode==='M';
    $('o-shut').textContent = fmtShutter(st.shutter_us);
    $('o-shut').className = 'val'+(sm?'':' dim');
    $('o-iso').textContent = st.iso===0?'AUTO':st.iso;
    $('o-iso').className = 'val'+(st.iso===0?' dim':'');
    $('o-ap').textContent = st.aperture>0?'f/'+st.aperture.toFixed(1):'f/--';
    const lp = st.lens_pos;
    $('o-foc').textContent = (lp<0||st.af)?'AF':lp.toFixed(2);

    if (st.recording && !recording) {
      recording = true; recStart = Date.now();
      $('btn-rec').innerHTML = '&#9646;&#9646; Stop';
      $('btn-rec').classList.add('rec-on');
      $('rec').style.display = 'block';
      recTick = setInterval(tickRec, 1000);
    } else if (!st.recording && recording) {
      stopRec();
    }

    if (st.tl_active && !tl_active) startTlUi(st.tl_count);
    else if (!st.tl_active && tl_active) stopTlUi();
    else if (tl_active) { $('tl-n').textContent = st.tl_count; $('tl-status').textContent = st.tl_count + ' frames  fn=' + st.tl_fn; }

    if (!$('fslider').matches(':active') && lp >= 0)
      $('fslider').value = Math.round(lp*100);

    if (st.af !== af) { af = st.af; $('btn-af').textContent='AF: '+(af?'ON':'OFF'); $('btn-af').classList.toggle('on',af); }

    const m = (st.mode||'P').toLowerCase();
    ['p','a','s','m'].forEach(x => $('m'+x).classList.toggle('on', x===m));

  } catch(e) {}
  setTimeout(pollStatus, 500);
}
pollStatus();

// ---- Gallery ----
let gTab = 'stills', gPage = 0, gLimit = 20;
let gSessionId = null, gSessionName = '';
let gItems = [];

function setGTab(t) {
  gTab = t; gPage = 0; gItems = [];
  gSessionId = null; gSessionName = '';
  ['stills','videos','timelapses'].forEach(x =>
    $('gtab-'+x).classList.toggle('active', x===t));
  $('gallery-breadcrumb').style.display = 'none';
  loadGallery();
}

function exitSession() {
  gSessionId = null; gSessionName = '';
  gPage = 0; gItems = [];
  $('gallery-breadcrumb').style.display = 'none';
  loadGallery();
}

function changePage(d) {
  gPage = Math.max(0, gPage + d);
  loadGallery();
}

function esc(s) {
  return String(s)
    .replace(/&/g,'&amp;').replace(/</g,'&lt;')
    .replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

async function loadGallery() {
  let c;
  if (gTab === 'stills') {
    c = 'gallery list type=stills page='+gPage+' limit='+gLimit;
  } else if (gTab === 'videos') {
    c = 'gallery list type=videos page='+gPage+' limit='+gLimit;
  } else {
    if (gSessionId !== null) {
      c = 'gallery list type=frames session='+gSessionId+' page='+gPage+' limit='+gLimit;
    } else {
      c = 'gallery list type=timelapses page='+gPage+' limit='+gLimit;
    }
  }

  const txt = await cmd(c);
  try { gItems = JSON.parse(txt); } catch(e) { gItems = []; }
  renderGrid();
}

function renderGrid() {
  const grid = $('gallery-grid');
  grid.innerHTML = '';

  for (const item of gItems) {
    const card = document.createElement('div');
    card.className = 'gcard';

    const isSession = 'frame_count' in item;
    if (isSession) {
      card.innerHTML =
        '<div class="gthumb"><span class="gthumb-icon">&#127897;</span></div>'
        +'<div class="gmeta">'
        +'<div class="gname">'+esc(item.session_name)+'</div>'
        +'<div class="gdate">'+(item.started_at||'').substring(0,10)+' &middot; '+item.frame_count+' frames</div>'
        +'</div>';
      card.onclick = () => {
        gSessionId = item.id; gSessionName = item.session_name;
        gPage = 0;
        $('gallery-breadcrumb').style.display = '';
        $('sess-name').textContent = item.session_name;
        loadGallery();
      };
    } else {
      const isStill = item.type==='still'||item.type==='tl_frame';
      card.innerHTML =
        '<div class="gthumb"><img src="/api/media/'+item.id+'/thumb" loading="lazy" alt=""></div>'
        +'<div class="gmeta">'
        +'<div class="gname">'+esc(item.filename)+'</div>'
        +'<div class="gdate">'+(item.captured_at||'').substring(0,10)+'</div>'
        +'</div>';
      if (isStill) {
        card.onclick = () => openLightbox('/api/media/'+item.id, item.filename);
      }
    }
    grid.appendChild(card);
  }

  $('prev-btn').disabled = gPage === 0;
  $('next-btn').disabled = gItems.length < gLimit;
  $('page-info').textContent = 'Page '+(gPage+1);
}

async function doScan() {
  $('scan-btn').disabled = true;
  $('scan-btn').textContent = 'Scanning…';
  const r = await cmd('gallery scan');
  $('scan-btn').disabled = false;
  $('scan-btn').textContent = 'Scan';
  try {
    const j = JSON.parse(r);
    msg('Scan complete: +'+j.added+' added, -'+j.removed+' removed');
  } catch(e) { msg(r); }
  loadGallery();
}

function openLightbox(src, name) {
  $('lb-img').src = src;
  $('lb-caption').textContent = name;
  $('lightbox').style.display = 'flex';
}
function closeLightbox() {
  $('lightbox').style.display = 'none';
  $('lb-img').src = '';
}
document.addEventListener('keydown', e => {
  if (e.key === 'Escape' && $('lightbox').style.display !== 'none') closeLightbox();
});
</script>
</body>
</html>)HTML";

// ---------------------------------------------------------------------------
// Per-connection wrapper — unified read/write over plain TCP or TLS
// ---------------------------------------------------------------------------

struct Conn {
    int  fd  = -1;
    SSL* ssl = nullptr;

    ssize_t read1(char& c) const {
        if (ssl) return SSL_read(ssl, &c, 1);
        return ::recv(fd, &c, 1, 0);
    }

    bool write(const void* buf, size_t n) const {
        const char* p = static_cast<const char*>(buf);
        while (n > 0) {
            int w = ssl ? SSL_write(ssl, p, static_cast<int>(n))
                        : static_cast<int>(::send(fd, p, n, MSG_NOSIGNAL));
            if (w <= 0) return false;
            p += w;
            n -= static_cast<size_t>(w);
        }
        return true;
    }

    void close() {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
        if (fd >= 0) { ::close(fd); fd = -1; }
    }
};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string url_decode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            char hex[3] = {in[i+1], in[i+2], '\0'};
            out += static_cast<char>(std::strtol(hex, nullptr, 16));
            i += 2;
        } else if (in[i] == '+') {
            out += ' ';
        } else {
            out += in[i];
        }
    }
    return out;
}

static bool read_line(const Conn& c, std::string& out) {
    out.clear();
    char ch;
    while (true) {
        ssize_t n = c.read1(ch);
        if (n == 1) {
            if (ch == '\n') return true;
            if (ch != '\r') out += ch;
            if (out.size() > 8192) return false;
        } else {
            return false;
        }
    }
}

static void send_http(const Conn& c, int code, const char* ct,
                      const char* body, size_t body_len) {
    const char* reason = (code == 200) ? "OK" : "Not Found";
    std::string hdr;
    hdr.reserve(256);
    hdr += "HTTP/1.0 "; hdr += std::to_string(code); hdr += ' '; hdr += reason; hdr += "\r\n";
    hdr += "Content-Type: "; hdr += ct; hdr += "\r\n";
    hdr += "Content-Length: "; hdr += std::to_string(body_len); hdr += "\r\n";
    hdr += "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n\r\n";
    c.write(hdr.c_str(), hdr.size());
    if (body_len > 0) c.write(body, body_len);
}

static void downsample_yuv420(
    const uint8_t* sy, int sy_stride,
    const uint8_t* su, const uint8_t* sv, int suv_stride,
    int sw, int sh,
    uint8_t* dy, uint8_t* du, uint8_t* dv,
    int dw, int dh)
{
    for (int y = 0; y < dh; ++y) {
        int src_y = y * sh / dh;
        const uint8_t* row = sy + src_y * sy_stride;
        for (int x = 0; x < dw; ++x)
            dy[y * dw + x] = row[x * sw / dw];
    }
    int duw = dw / 2, duh = dh / 2;
    int suw = sw / 2, suh = sh / 2;
    for (int y = 0; y < duh; ++y) {
        int src_y = y * suh / duh;
        const uint8_t* ru = su + src_y * suv_stride;
        const uint8_t* rv = sv + src_y * suv_stride;
        for (int x = 0; x < duw; ++x) {
            int sx = x * suw / duw;
            du[y * duw + x] = ru[sx];
            dv[y * duw + x] = rv[sx];
        }
    }
}

// ---------------------------------------------------------------------------
// MjpegServer
// ---------------------------------------------------------------------------

MjpegServer::MjpegServer(int port, int jpeg_quality, float scale, int max_fps,
                          bool https, const std::string& cert_file,
                          const std::string& key_file)
    : port_(port), quality_(jpeg_quality), max_fps_(max_fps), scale_(scale), https_(https)
{
    // Ignore SIGPIPE — broken client connections should return EPIPE, not kill the process
    ::signal(SIGPIPE, SIG_IGN);

    if (https_) {
        SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
        if (!ctx) {
            std::cerr << "[mjpeg] SSL_CTX_new failed — falling back to HTTP\n";
            https_ = false;
        } else if (cert_file.empty() || key_file.empty()) {
            std::cerr << "[mjpeg] stream_https = true but stream_cert/stream_key not set"
                         " — falling back to HTTP\n";
            SSL_CTX_free(ctx);
            https_ = false;
        } else if (SSL_CTX_use_certificate_file(ctx, cert_file.c_str(), SSL_FILETYPE_PEM) != 1) {
            std::cerr << "[mjpeg] failed to load TLS cert: " << cert_file
                      << " — falling back to HTTP\n";
            SSL_CTX_free(ctx);
            https_ = false;
        } else if (SSL_CTX_use_PrivateKey_file(ctx, key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
            std::cerr << "[mjpeg] failed to load TLS key: " << key_file
                      << " — falling back to HTTP\n";
            SSL_CTX_free(ctx);
            https_ = false;
        } else if (!SSL_CTX_check_private_key(ctx)) {
            std::cerr << "[mjpeg] TLS cert/key mismatch — falling back to HTTP\n";
            SSL_CTX_free(ctx);
            https_ = false;
        } else {
            ssl_ctx_ = ctx;
        }
    }

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[mjpeg] socket(): " << ::strerror(errno) << "\n";
        return;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Non-blocking so listen_loop can poll stopping_ without a dedicated wake mechanism
    ::fcntl(listen_fd_, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[mjpeg] bind(:" << port_ << "): " << ::strerror(errno) << "\n";
        ::close(listen_fd_); listen_fd_ = -1;
        return;
    }
    if (::listen(listen_fd_, 8) != 0) {
        std::cerr << "[mjpeg] listen(): " << ::strerror(errno) << "\n";
        ::close(listen_fd_); listen_fd_ = -1;
        return;
    }

    encode_thread_ = std::thread([this]{ encode_loop(); });
    listen_thread_ = std::thread([this]{ listen_loop(); });

    std::cerr << "[mjpeg] listening on port " << port_
              << " (" << (https_ ? "HTTPS" : "HTTP") << ")\n";
}

MjpegServer::~MjpegServer() {
    stopping_.store(true);
    yuv_cv_.notify_all();
    jpeg_cv_.notify_all();

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
    }

    if (listen_thread_.joinable()) listen_thread_.join();
    if (encode_thread_.joinable()) encode_thread_.join();

    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    // Give any running stream threads up to 2 s to notice stopping_ and finish.
    // Stream threads decrement stream_count_ only after SSL_free+close, so it's
    // safe to free ssl_ctx_ once stream_count_ reaches zero.
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::seconds(2);
    while (stream_count_.load() > 0 && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (ssl_ctx_) {
        SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
        ssl_ctx_ = nullptr;
    }
}

void MjpegServer::push_frame(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                              int w, int h, int y_stride, int uv_stride) {
    if (stopping_) return;

    // Write into the "back" buffer (encode thread will not touch yuv_[yuv_write_])
    YuvBuf& buf = yuv_[yuv_write_];
    buf.w = w; buf.h = h; buf.y_stride = y_stride; buf.uv_stride = uv_stride;
    size_t y_sz  = static_cast<size_t>(y_stride)  * h;
    size_t uv_sz = static_cast<size_t>(uv_stride) * (h / 2);
    buf.data.resize(y_sz + uv_sz + uv_sz);
    std::memcpy(buf.data.data(),              y, y_sz);
    std::memcpy(buf.data.data() + y_sz,       u, uv_sz);
    std::memcpy(buf.data.data() + y_sz + uv_sz, v, uv_sz);

    // Swap: tell encode thread about the new frame and redirect main to the other slot
    {
        std::lock_guard<std::mutex> lk(yuv_mtx_);
        yuv_latest_ = yuv_write_;
        yuv_write_  = 1 - yuv_write_;
        ++yuv_seq_;
    }
    yuv_cv_.notify_one();
}

void MjpegServer::set_status(const std::string& json) {
    std::lock_guard<std::mutex> lk(status_mtx_);
    status_json_ = json;
}

bool MjpegServer::pop_command(std::string& cmd_out,
                               std::function<void(const std::string&)>& reply_fn) {
    std::lock_guard<std::mutex> lk(cmd_mtx_);
    if (cmd_queue_.empty()) return false;
    auto& front = cmd_queue_.front();
    cmd_out = front.cmd;
    auto prom = front.reply;
    cmd_queue_.pop();
    reply_fn = [prom](const std::string& r) { prom->set_value(r); };
    return true;
}

// ---------------------------------------------------------------------------
// Encode thread
// ---------------------------------------------------------------------------

void MjpegServer::encode_loop() {
    tjhandle tj = tjInitCompress();
    if (!tj) {
        std::cerr << "[mjpeg] tjInitCompress failed\n";
        return;
    }

    unsigned char* jpeg_raw = nullptr;
    unsigned long  jpeg_cap = 0;
    std::vector<uint8_t> tmp_buf;

    using Clock = std::chrono::steady_clock;
    const auto min_interval = (max_fps_ > 0)
        ? std::chrono::microseconds(1000000 / max_fps_)
        : std::chrono::microseconds(0);
    auto last_encode = Clock::now() - min_interval;

    uint64_t last_seq = 0;

    while (!stopping_) {
        // Wait for a new YUV frame
        int encode_from;
        {
            std::unique_lock<std::mutex> lk(yuv_mtx_);
            yuv_cv_.wait(lk, [&]{ return yuv_seq_ != last_seq || stopping_; });
            if (stopping_) break;
            last_seq   = yuv_seq_;
            encode_from = yuv_latest_;
            yuv_write_ = 1 - encode_from; // redirect main to the other slot
        }

        // Throttle: skip if we encoded too recently
        auto now = Clock::now();
        if (now - last_encode < min_interval) continue;
        last_encode = now;

        const YuvBuf& buf = yuv_[encode_from];
        if (buf.w <= 0 || buf.h <= 0) continue;

        // Compute destination dimensions (even, >= 2)
        int dw = std::max(2, static_cast<int>(buf.w * scale_) & ~1);
        int dh = std::max(2, static_cast<int>(buf.h * scale_) & ~1);

        // Downsample YUV420 into tmp_buf
        size_t y_sz  = static_cast<size_t>(dw) * dh;
        size_t uv_sz = static_cast<size_t>(dw / 2) * (dh / 2);
        tmp_buf.resize(y_sz + uv_sz + uv_sz);
        uint8_t* dy = tmp_buf.data();
        uint8_t* du = dy + y_sz;
        uint8_t* dv = du + uv_sz;

        const uint8_t* sy = buf.data.data();
        const uint8_t* su = sy + static_cast<size_t>(buf.y_stride)  * buf.h;
        const uint8_t* sv = su + static_cast<size_t>(buf.uv_stride) * (buf.h / 2);

        downsample_yuv420(sy, buf.y_stride, su, sv, buf.uv_stride, buf.w, buf.h,
                          dy, du, dv, dw, dh);

        // Pre-allocate JPEG output buffer (worst-case size)
        unsigned long needed = tjBufSize(dw, dh, TJSAMP_420);
        if (needed > jpeg_cap) {
            if (jpeg_raw) tjFree(jpeg_raw);
            jpeg_raw = static_cast<unsigned char*>(tjAlloc(static_cast<int>(needed)));
            jpeg_cap = needed;
        }

        const unsigned char* planes[3] = {dy, du, dv};
        int strides[3] = {dw, dw / 2, dw / 2};
        unsigned long jpeg_sz = jpeg_cap;

        int ret = tjCompressFromYUVPlanes(
            tj,
            planes, dw, strides, dh, TJSAMP_420,
            &jpeg_raw, &jpeg_sz,
            quality_,
            TJFLAG_FASTDCT | TJFLAG_NOREALLOC);

        if (ret != 0) {
            std::cerr << "[mjpeg] encode error: " << tjGetErrorStr2(tj) << "\n";
            continue;
        }

        auto new_frame = std::make_shared<std::vector<uint8_t>>(
            jpeg_raw, jpeg_raw + jpeg_sz);
        {
            std::lock_guard<std::mutex> lk(jpeg_mtx_);
            jpeg_ptr_ = std::move(new_frame);
            ++jpeg_seq_;
        }
        jpeg_cv_.notify_all();
    }

    if (jpeg_raw) tjFree(jpeg_raw);
    tjDestroy(tj);
}

// ---------------------------------------------------------------------------
// Media DB
// ---------------------------------------------------------------------------

void MjpegServer::set_media_db(MediaDb* db, const std::string& thumb_cache_dir) {
    std::lock_guard<std::mutex> lk(db_mtx_);
    db_               = db;
    thumb_cache_dir_  = thumb_cache_dir;
}

// ---------------------------------------------------------------------------
// File/thumbnail serving helpers (static — only used in client threads)
// ---------------------------------------------------------------------------

static const char* content_type_for(const std::string& path) {
    if (path.size() >= 4) {
        std::string ext = path.substr(path.size() - 4);
        for (char& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".jpg"  || ext == "jpeg") return "image/jpeg";
        if (ext == ".mkv")                    return "video/x-matroska";
        if (ext == ".mp4")                    return "video/mp4";
        if (ext == ".raw")                    return "application/octet-stream";
    }
    return "application/octet-stream";
}

// Stream a file (or a byte range of it) over the connection.
static void serve_file(const Conn& c, const std::string& path,
                       const std::string& range_hdr)
{
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        const char kErr[] = "Not Found";
        send_http(c, 404, "text/plain", kErr, sizeof(kErr) - 1);
        return;
    }

    struct stat st;
    if (::fstat(fd, &st) < 0) { ::close(fd); return; }
    int64_t total = st.st_size;
    int64_t start = 0, end = total - 1;
    bool have_range = false;

    if (!range_hdr.empty() && range_hdr.rfind("bytes=", 0) == 0) {
        std::string rng = range_hdr.substr(6);
        auto dash = rng.find('-');
        if (dash != std::string::npos) {
            have_range = true;
            try {
                if (dash > 0) start = std::stoll(rng.substr(0, dash));
                if (dash + 1 < rng.size()) end = std::stoll(rng.substr(dash + 1));
            } catch (...) { have_range = false; start = 0; end = total - 1; }
        }
    }
    if (end >= total) end = total - 1;
    int64_t length = end - start + 1;
    if (length <= 0) { ::close(fd); return; }

    const char* ct = content_type_for(path);
    std::string hdr;
    hdr.reserve(256);
    if (have_range) {
        hdr += "HTTP/1.0 206 Partial Content\r\n";
        hdr += "Content-Range: bytes ";
        hdr += std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(total);
        hdr += "\r\n";
    } else {
        hdr += "HTTP/1.0 200 OK\r\n";
    }
    hdr += "Content-Type: ";  hdr += ct;  hdr += "\r\n";
    hdr += "Content-Length: "; hdr += std::to_string(length); hdr += "\r\n";
    hdr += "Accept-Ranges: bytes\r\n";
    hdr += "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n\r\n";

    if (!c.write(hdr.c_str(), hdr.size())) { ::close(fd); return; }

    if (::lseek(fd, start, SEEK_SET) < 0) { ::close(fd); return; }
    int64_t remaining = length;
    char buf[65536];
    while (remaining > 0) {
        ssize_t n = ::read(fd, buf, (size_t)std::min(remaining, (int64_t)sizeof(buf)));
        if (n <= 0) break;
        if (!c.write(buf, (size_t)n)) break;
        remaining -= n;
    }
    ::close(fd);
}

// Generate a 320-px-wide JPEG thumbnail from a still image using libturbojpeg.
static bool generate_still_thumb(const std::string& src, const std::string& dst, int target_w) {
    FILE* f = ::fopen(src.c_str(), "rb");
    if (!f) return false;
    ::fseek(f, 0, SEEK_END);
    long sz = ::ftell(f);
    ::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { ::fclose(f); return false; }
    std::vector<uint8_t> buf((size_t)sz);
    if ((long)::fread(buf.data(), 1, (size_t)sz, f) < sz) { ::fclose(f); return false; }
    ::fclose(f);

    tjhandle tj = tjInitDecompress();
    if (!tj) return false;

    int w, h, subsamp, colorspace;
    if (tjDecompressHeader3(tj, buf.data(), (unsigned long)buf.size(),
                            &w, &h, &subsamp, &colorspace) < 0) {
        tjDestroy(tj); return false;
    }

    int dw = target_w;
    int dh = (w > 0) ? (h * dw / w) : dw;
    if (dh < 1) dh = 1;

    std::vector<uint8_t> full_rgb((size_t)(w * h * 3));
    if (tjDecompress2(tj, buf.data(), (unsigned long)buf.size(),
                      full_rgb.data(), w, 0, h, TJPF_RGB, 0) < 0) {
        tjDestroy(tj); return false;
    }

    // Bilinear-ish downsample (nearest).
    std::vector<uint8_t> thumb_rgb((size_t)(dw * dh * 3));
    for (int y = 0; y < dh; ++y) {
        int sy = y * h / dh;
        for (int x = 0; x < dw; ++x) {
            int sx = x * w / dw;
            const uint8_t* sp = &full_rgb[(size_t)(sy * w + sx) * 3];
            uint8_t*       dp = &thumb_rgb[(size_t)(y * dw + x) * 3];
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
        }
    }

    uint8_t* jpeg_buf = nullptr;
    unsigned long jpeg_sz = 0;
    bool ok = tjCompress2(tj, thumb_rgb.data(), dw, 0, dh, TJPF_RGB,
                          &jpeg_buf, &jpeg_sz, TJSAMP_420, 75, 0) == 0;
    tjDestroy(tj);

    if (!ok || !jpeg_buf) { if (jpeg_buf) tjFree(jpeg_buf); return false; }

    FILE* out = ::fopen(dst.c_str(), "wb");
    bool wrote = out && (::fwrite(jpeg_buf, 1, jpeg_sz, out) == jpeg_sz);
    if (out) ::fclose(out);
    tjFree(jpeg_buf);
    return wrote;
}

// Generate a thumbnail from a video by spawning ffmpeg.
static bool generate_video_thumb(const std::string& src, const std::string& dst, int target_w) {
    std::string scale = "scale=" + std::to_string(target_w) + ":-1";
    pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        int devnull = ::open("/dev/null", O_RDWR);
        if (devnull >= 0) {
            ::dup2(devnull, STDIN_FILENO);
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        const char* argv[] = {
            "ffmpeg", "-i", src.c_str(), "-vframes", "1",
            "-vf", scale.c_str(), "-y", dst.c_str(), nullptr
        };
        ::execvp("ffmpeg", const_cast<char* const*>(argv));
        ::_exit(1);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// Serve (or generate) a thumbnail for a media item.
static void serve_thumbnail(const Conn& c, int64_t id,
                             const std::string& src_path,
                             const std::string& thumb_dir)
{
    std::string cache_path = thumb_dir + "/" + std::to_string(id) + ".jpg";

    if (::access(cache_path.c_str(), F_OK) != 0) {
        // Generate thumbnail based on file type.
        bool is_jpg = src_path.size() >= 4 &&
                      src_path.substr(src_path.size() - 4) == ".jpg";
        bool is_mkv = src_path.size() >= 4 &&
                      src_path.substr(src_path.size() - 4) == ".mkv";

        bool ok = false;
        if (is_jpg) ok = generate_still_thumb(src_path, cache_path, 320);
        else if (is_mkv) ok = generate_video_thumb(src_path, cache_path, 320);

        if (!ok || ::access(cache_path.c_str(), F_OK) != 0) {
            const char kErr[] = "Thumbnail unavailable";
            send_http(c, 404, "text/plain", kErr, sizeof(kErr) - 1);
            return;
        }
    }

    serve_file(c, cache_path, "");
}

// ---------------------------------------------------------------------------
// Listen + client threads
// ---------------------------------------------------------------------------

void MjpegServer::listen_loop() {
    SSL_CTX* ctx = static_cast<SSL_CTX*>(ssl_ctx_);
    while (!stopping_) {
        int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd >= 0) {
            SSL* ssl = nullptr;
            if (https_ && ctx) {
                ssl = SSL_new(ctx);
                SSL_set_fd(ssl, fd);
                if (SSL_accept(ssl) <= 0) {
                    SSL_free(ssl);
                    ::close(fd);
                    continue;
                }
            }
            auto t = std::thread([this, fd, ssl]{ client_loop(fd, ssl); });
            t.detach();
        } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } else {
            break; // EBADF or other — socket was shut down
        }
    }
}

void MjpegServer::client_loop(int fd, void* ssl_vp) {
    Conn c{fd, static_cast<SSL*>(ssl_vp)};

    // 5-second receive timeout for reading the HTTP request
    struct timeval rtv{5, 0};
    ::setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    // 2-second send timeout (protects against slow clients blocking stream threads)
    struct timeval stv{2, 0};
    ::setsockopt(c.fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));

    // Parse request line
    std::string request_line;
    if (!read_line(c, request_line)) { c.close(); return; }

    std::string method, raw_path;
    {
        std::istringstream ss(request_line);
        ss >> method >> raw_path;
    }

    // Drain remaining headers; capture Range for byte-range serving.
    std::string hdr, range_hdr;
    while (read_line(c, hdr) && !hdr.empty()) {
        if (hdr.rfind("Range: ", 0) == 0) range_hdr = hdr.substr(7);
    }

    // URL-decode and strip query string
    std::string path = url_decode(raw_path);
    auto qpos = path.find('?');
    if (qpos != std::string::npos) path.resize(qpos);

    // Route
    if (method == "GET" && path == "/") {
        send_http(c, 200, "text/html; charset=utf-8",
                  kWebUiHtml, sizeof(kWebUiHtml) - 1);

    } else if (method == "GET" && path == "/stream") {
        // Remove recv timeout — stream connection is long-lived
        struct timeval zero{0, 0};
        ::setsockopt(c.fd, SOL_SOCKET, SO_RCVTIMEO, &zero, sizeof(zero));

        // MJPEG multipart stream — inline loop.
        // stream_count_ is decremented only after c.close() so the destructor
        // knows ssl_ctx_ is still referenced until the SSL* is freed.
        ++stream_count_;

        static const char kMjpegHeader[] =
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: multipart/x-mixed-replace;boundary=mjpegframe\r\n"
            "Cache-Control: no-store\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n";

        if (c.write(kMjpegHeader, sizeof(kMjpegHeader) - 1)) {
            uint64_t last_seq = 0;
            while (!stopping_) {
                std::shared_ptr<const std::vector<uint8_t>> jpeg;
                {
                    std::unique_lock<std::mutex> lk(jpeg_mtx_);
                    jpeg_cv_.wait(lk, [&]{ return jpeg_seq_ != last_seq || stopping_; });
                    if (stopping_) break;
                    last_seq = jpeg_seq_;
                    jpeg = jpeg_ptr_; // O(1) ref-count copy
                }
                if (!jpeg || jpeg->empty()) continue;
                char boundary[128];
                int blen = std::snprintf(boundary, sizeof(boundary),
                    "--mjpegframe\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
                    jpeg->size());
                if (!c.write(boundary, static_cast<size_t>(blen))) break;
                if (!c.write(jpeg->data(), jpeg->size())) break;
            }
        }

        c.close();        // SSL_shutdown + SSL_free + ::close before decrement
        --stream_count_;  // destructor may free ssl_ctx_ once this reaches 0
        return;

    } else if (method == "GET" && path == "/api/status") {
        std::string json;
        {
            std::lock_guard<std::mutex> lk(status_mtx_);
            json = status_json_;
        }
        send_http(c, 200, "application/json", json.c_str(), json.size());

    } else if (method == "POST" && path.rfind("/api/", 0) == 0) {
        std::string cmd_str = path.substr(5); // strip "/api/"
        if (cmd_str.empty()) {
            const char kErr[] = "ERR empty command";
            send_http(c, 200, "text/plain", kErr, sizeof(kErr) - 1);
        } else {
            auto prom = std::make_shared<std::promise<std::string>>();
            auto fut  = prom->get_future();
            {
                std::lock_guard<std::mutex> lk(cmd_mtx_);
                cmd_queue_.push({cmd_str, prom});
            }
            std::string result;
            if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
                result = fut.get();
            } else {
                result = "ERR timeout";
            }
            send_http(c, 200, "text/plain", result.c_str(), result.size());
        }

    } else if (method == "GET" && path.rfind("/api/media/", 0) == 0) {
        // Route: GET /api/media/<id>  or  GET /api/media/<id>/thumb
        std::string tail = path.substr(11); // strip "/api/media/"
        bool want_thumb = false;
        if (tail.size() > 6 && tail.substr(tail.size() - 6) == "/thumb") {
            want_thumb = true;
            tail = tail.substr(0, tail.size() - 6);
        }

        int64_t id = 0;
        try { id = std::stoll(tail); } catch (...) {}

        if (id <= 0) {
            const char kErr[] = "Bad id";
            send_http(c, 404, "text/plain", kErr, sizeof(kErr) - 1);
        } else {
            // Read db pointer and thumb dir under lock, then release before I/O.
            MediaDb* db = nullptr;
            std::string tdir;
            {
                std::lock_guard<std::mutex> lk(db_mtx_);
                db   = db_;
                tdir = thumb_cache_dir_;
            }

            std::optional<std::string> maybe_path;
            if (db) maybe_path = db->get_path_for_serving(id);

            if (!maybe_path) {
                const char kErr[] = "Not Found";
                send_http(c, 404, "text/plain", kErr, sizeof(kErr) - 1);
            } else if (want_thumb) {
                serve_thumbnail(c, id, *maybe_path, tdir);
            } else {
                serve_file(c, *maybe_path, range_hdr);
            }
        }

    } else {
        const char kNotFound[] = "Not Found";
        send_http(c, 404, "text/plain", kNotFound, sizeof(kNotFound) - 1);
    }

    c.close();
}
