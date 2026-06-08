#include "mjpeg_server.h"

#include <arpa/inet.h>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

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
#viewer{position:relative;background:#000;display:flex;justify-content:center;align-items:center;min-height:40vw}
#stream{max-width:100%;max-height:70vh;display:block;object-fit:contain}
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
#msg{padding:5px 10px;min-height:22px;font-size:12px;color:#888}
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
.row{display:flex;gap:5px}
.row button{flex:1}
input[type=range]{width:100%;margin:5px 0;accent-color:#4af;cursor:pointer}
select{
  width:100%;background:#2a2a2a;color:#ccc;border:1px solid #333;
  border-radius:4px;padding:5px;font-size:13px;margin-top:4px;cursor:pointer
}
</style>
</head>
<body>
<div id="viewer">
  <img id="stream" src="/stream" alt="Connecting to camera&hellip;">
  <div id="osd">
    <div class="oitem"><div class="lbl">mode</div><div class="val" id="o-mode">P</div></div>
    <div class="oitem"><div class="lbl">shutter</div><div class="val dim" id="o-shut">---</div></div>
    <div class="oitem"><div class="lbl">iso</div><div class="val dim" id="o-iso">AUTO</div></div>
    <div class="oitem"><div class="lbl">aperture</div><div class="val" id="o-ap">f/--</div></div>
    <div class="oitem"><div class="lbl">focus</div><div class="val" id="o-foc">AF</div></div>
    <div id="rec">&#9679; REC <span id="rec-t">00:00</span></div>
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
    <button id="btn-ae" onclick="toggleAe()">AE: ON</button>
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
      <option value="100">100</option><option value="200">200</option>
      <option value="400">400</option><option value="800">800</option>
      <option value="1600">1600</option><option value="3200">3200</option>
      <option value="6400">6400</option>
    </select>
  </div>
  <div class="grp">
    <h3>Shutter</h3>
    <select id="shut-sel" onchange="setShutter(this.value)">
      <option value="500000">2"</option><option value="250000">1"</option>
      <option value="125000">1/8</option><option value="50000">1/20</option>
      <option value="33333">1/30</option><option value="16667" selected>1/60</option>
      <option value="8333">1/120</option><option value="4000">1/250</option>
      <option value="2000">1/500</option><option value="1000">1/1000</option>
      <option value="500">1/2000</option><option value="250">1/4000</option>
    </select>
  </div>
</div>
<script>
'use strict';
const $=id=>document.getElementById(id);
const msg=t=>{$('msg').textContent=t;};
let ae=true,af=true,recording=false,recStart=0,recTick=null;
async function cmd(c){
  try{
    const r=await fetch('/api/'+encodeURIComponent(c),{method:'POST'});
    const t=(await r.text()).trim();msg(t);return t;
  }catch(e){msg('Connection lost');return'ERR';}
}
function setMode(m){
  cmd('mode '+m);
  ['p','a','s','m'].forEach(x=>$('m'+x).classList.remove('on'));
  $('m'+m).classList.add('on');
}
function toggleAe(){ae=!ae;cmd('ae '+(ae?'on':'off'));$('btn-ae').textContent='AE: '+(ae?'ON':'OFF');$('btn-ae').classList.toggle('on',ae);}
function toggleAf(){af=!af;cmd('af '+(af?'on':'off'));$('btn-af').textContent='AF: '+(af?'ON':'OFF');$('btn-af').classList.toggle('on',af);}
function setIso(v){cmd(v==='auto'?'iso auto':'iso '+v);}
function setShutter(us){cmd('shutter '+us);}
let fTimer=null;
$('fslider').addEventListener('input',function(){
  const pos=(this.value/100).toFixed(2);
  clearTimeout(fTimer);fTimer=setTimeout(()=>cmd('focus '+pos),80);
});
$('btn-still').addEventListener('click',async function(){
  this.disabled=true;msg('Capturing…');
  const r=await cmd('still');this.disabled=false;
  if(r.startsWith('OK '))msg('Saved: '+r.slice(3).trim().split('/').pop());
});
$('btn-rec').addEventListener('click',async function(){
  if(!recording){
    const r=await cmd('record_start');
    if(r.startsWith('OK')){
      recording=true;recStart=Date.now();
      this.innerHTML='&#9646;&#9646; Stop';this.classList.add('rec-on');
      $('rec').style.display='block';recTick=setInterval(tickRec,1000);
    }
  }else{await cmd('record_stop');stopRec();}
});
function tickRec(){
  const s=Math.floor((Date.now()-recStart)/1000);
  $('rec-t').textContent=String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0');
}
function stopRec(){
  recording=false;clearInterval(recTick);recTick=null;
  $('btn-rec').innerHTML='&#9654; Record';$('btn-rec').classList.remove('rec-on');
  $('rec').style.display='none';
}
function fmtShutter(us){
  if(!us||us<=0)return'---';
  const s=us/1e6;return s<0.5?'1/'+Math.round(1/s):s.toFixed(1)+'"';
}
async function pollStatus(){
  try{
    const r=await fetch('/api/status');const st=await r.json();
    $('o-mode').textContent=st.mode||'--';
    const sm=st.mode==='S'||st.mode==='M';
    $('o-shut').textContent=fmtShutter(st.shutter_us);$('o-shut').className='val'+(sm?'':' dim');
    $('o-iso').textContent=st.iso===0?'AUTO':st.iso;$('o-iso').className='val'+(st.iso===0?' dim':'');
    $('o-ap').textContent=st.aperture>0?'f/'+st.aperture.toFixed(1):'f/--';
    const lp=st.lens_pos;$('o-foc').textContent=(lp<0||st.af)?'AF':lp.toFixed(2);
    if(st.recording&&!recording){
      recording=true;recStart=Date.now();
      $('btn-rec').innerHTML='&#9646;&#9646; Stop';$('btn-rec').classList.add('rec-on');
      $('rec').style.display='block';recTick=setInterval(tickRec,1000);
    }else if(!st.recording&&recording){stopRec();}
    if(!$('fslider').matches(':active')&&lp>=0)$('fslider').value=Math.round(lp*100);
    if(st.af!==af){af=st.af;$('btn-af').textContent='AF: '+(af?'ON':'OFF');$('btn-af').classList.toggle('on',af);}
    if(st.ae!==ae){ae=st.ae;$('btn-ae').textContent='AE: '+(ae?'ON':'OFF');$('btn-ae').classList.toggle('on',ae);}
    const m=(st.mode||'P').toLowerCase();
    ['p','a','s','m'].forEach(x=>$('m'+x).classList.toggle('on',x===m));
  }catch(e){}
  setTimeout(pollStatus,500);
}
pollStatus();
</script>
</body>
</html>)HTML";

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

static bool send_all(int fd, const void* buf, size_t n) {
    const char* p = static_cast<const char*>(buf);
    while (n > 0) {
        ssize_t sent = ::send(fd, p, n, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        p += sent; n -= sent;
    }
    return true;
}

static bool read_line(int fd, std::string& out) {
    out.clear();
    char c;
    while (true) {
        ssize_t n = ::recv(fd, &c, 1, 0);
        if (n == 1) {
            if (c == '\n') return true;
            if (c != '\r') out += c;
            if (out.size() > 8192) return false;
        } else {
            return false;
        }
    }
}

static void send_http(int fd, int code, const char* ct,
                      const char* body, size_t body_len) {
    const char* reason = (code == 200) ? "OK" : "Not Found";
    std::string hdr;
    hdr.reserve(256);
    hdr += "HTTP/1.0 "; hdr += std::to_string(code); hdr += ' '; hdr += reason; hdr += "\r\n";
    hdr += "Content-Type: "; hdr += ct; hdr += "\r\n";
    hdr += "Content-Length: "; hdr += std::to_string(body_len); hdr += "\r\n";
    hdr += "Access-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n\r\n";
    send_all(fd, hdr.c_str(), hdr.size());
    if (body_len > 0) send_all(fd, body, body_len);
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

MjpegServer::MjpegServer(int port, int jpeg_quality, float scale, int max_fps)
    : port_(port), quality_(jpeg_quality), max_fps_(max_fps), scale_(scale)
{
    // Ignore SIGPIPE — broken client connections should return EPIPE, not kill the process
    ::signal(SIGPIPE, SIG_IGN);

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

    std::cerr << "[mjpeg] listening on port " << port_ << "\n";
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

    // Give any running stream threads up to 2 s to notice stopping_ and finish
    using Clock = std::chrono::steady_clock;
    auto deadline = Clock::now() + std::chrono::seconds(2);
    while (stream_count_.load() > 0 && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
// Listen + client threads
// ---------------------------------------------------------------------------

void MjpegServer::listen_loop() {
    while (!stopping_) {
        int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd >= 0) {
            auto t = std::thread([this, fd]{ client_loop(fd); });
            t.detach();
        } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } else {
            break; // EBADF or other — socket was shut down
        }
    }
}

void MjpegServer::client_loop(int fd) {
    // 5-second timeout for reading the HTTP request
    struct timeval rtv{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rtv, sizeof(rtv));
    // 2-second timeout for sending (protects against slow clients blocking stream threads)
    struct timeval stv{2, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));

    // Parse request line
    std::string request_line;
    if (!read_line(fd, request_line)) { ::close(fd); return; }

    std::string method, raw_path;
    {
        std::istringstream ss(request_line);
        ss >> method >> raw_path;
    }

    // Drain remaining headers
    std::string hdr;
    while (read_line(fd, hdr) && !hdr.empty()) {}

    // URL-decode and strip query string
    std::string path = url_decode(raw_path);
    auto qpos = path.find('?');
    if (qpos != std::string::npos) path.resize(qpos);

    // Route
    if (method == "GET" && path == "/") {
        send_http(fd, 200, "text/html; charset=utf-8",
                  kWebUiHtml, sizeof(kWebUiHtml) - 1);

    } else if (method == "GET" && path == "/stream") {
        // Remove recv timeout for long-lived stream connection
        struct timeval zero{0, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &zero, sizeof(zero));
        stream_loop(fd);

    } else if (method == "GET" && path == "/api/status") {
        std::string json;
        {
            std::lock_guard<std::mutex> lk(status_mtx_);
            json = status_json_;
        }
        send_http(fd, 200, "application/json", json.c_str(), json.size());

    } else if (method == "POST" && path.rfind("/api/", 0) == 0) {
        std::string cmd_str = path.substr(5); // strip "/api/"
        if (cmd_str.empty()) {
            const char kErr[] = "ERR empty command";
            send_http(fd, 200, "text/plain", kErr, sizeof(kErr) - 1);
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
            send_http(fd, 200, "text/plain", result.c_str(), result.size());
        }

    } else {
        const char kNotFound[] = "Not Found";
        send_http(fd, 404, "text/plain", kNotFound, sizeof(kNotFound) - 1);
    }

    ::close(fd);
}

void MjpegServer::stream_loop(int fd) {
    ++stream_count_;

    static const char kMjpegHeader[] =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: multipart/x-mixed-replace;boundary=mjpegframe\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";

    if (!send_all(fd, kMjpegHeader, sizeof(kMjpegHeader) - 1)) {
        --stream_count_;
        return;
    }

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

        // Multipart boundary
        char boundary[128];
        int blen = std::snprintf(boundary, sizeof(boundary),
            "--mjpegframe\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n",
            jpeg->size());
        if (!send_all(fd, boundary, static_cast<size_t>(blen))) break;
        if (!send_all(fd, jpeg->data(), jpeg->size())) break;
    }

    --stream_count_;
}
