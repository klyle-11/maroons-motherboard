// mm26-nomad : Offline media captive portal with custom domain, security modal, and light theme
// LILYGO TTGO LoRa32 T3 v1.6.1

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerifItalic9pt7b.h>
#include <Fonts/TomThumb.h>
#include <ESPAsyncWebServer.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <memory>
#include "esp_wifi.h"
#include "esp_netif.h"

// ---------- Pin map: TTGO LoRa32 T3 v1.6.1 ----------
#define SD_CS         13
#define SD_MOSI       15
#define SD_MISO        2
#define SD_SCK        14

#define OLED_SDA      21
#define OLED_SCL      22
#define OLED_RST      -1
#define OLED_ADDR     0x3C

#define BOOT_BUTTON    0
#define LORA_CS       18

// ---------- Config ----------
static const char *AP_SSID     = "MemoryWorkersGuild-board";
static const char *AP_PASSWORD = "Knkrumah$22";       // open AP for one-tap join
static const uint8_t AP_MAX_CLIENTS = 4;  
static const char *DOMAIN_NAME = "maroons.library";       // RAM-bound on plain ESP32

static const char *DISPLAY_TITLE = "Maroon's Motherboard";
static const char *ROTATING_MSGS[] = {
  "Browse the library",
  "EPUB & PDF books",
  "Connect to read",
  "Offline archive",
  "No internet needed",
  "Tap a book to open"
};
static const uint8_t ROTATING_MSG_COUNT = sizeof(ROTATING_MSGS) / sizeof(ROTATING_MSGS[0]);

static const uint32_t INTRO_DURATION_MS    = 4500;
static const uint32_t SCREENSAVER_AFTER_MS = 30000;
static const uint32_t MSG_ROTATE_MS        = 3500;

// ---------- Globals ----------
static Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
static AsyncWebServer   server(80);
static DNSServer        dns;
static Preferences      prefs;

static volatile bool    sdReady = false;
static uint64_t         sdCardSizeBytes = 0;
static uint32_t         bootMillis = 0;
static uint32_t         lastInteractionMs = 0;
static uint32_t         lastMsgRotateMs = 0;
static uint8_t          currentMsg = 0;
static bool             buttonHeldAtBoot = false;
static uint32_t         buttonHeldSince = 0;

enum DisplayState { ST_INTRO, ST_DASHBOARD, ST_SCREENSAVER };
static DisplayState dispState = ST_INTRO;

// ---------- Mode switching ----------
static bool switchToMeshtasticAndReboot() {
  const esp_partition_t *ota0 = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
  if (!ota0) return false;
  if (esp_ota_set_boot_partition(ota0) != ESP_OK) return false;
  prefs.putString("last_mode", "meshtastic");
  delay(50);
  esp_restart();
  return true;
}

// ---------- SD helpers ----------
static bool initSD() {
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 20000000)) return false;
  sdCardSizeBytes = SD.cardSize();
  return true;
}

static String mimeFor(const String &path) {
  String p = path; p.toLowerCase();
  if (p.endsWith(".html")) return "text/html; charset=utf-8";
  if (p.endsWith(".css")) return "text/css";
  if (p.endsWith(".js")) return "application/javascript";
  if (p.endsWith(".json")) return "application/json";
  if (p.endsWith(".epub")) return "application/epub+zip";
  if (p.endsWith(".pdf")) return "application/pdf";
  if (p.endsWith(".jpg") || p.endsWith(".jpeg")) return "image/jpeg";
  if (p.endsWith(".png")) return "image/png";
  return "application/octet-stream";
}

static String normalizePath(const String &raw) {
  String p = raw;
  if (!p.startsWith("/")) p = "/" + p;
  while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  if (p.indexOf("..") >= 0) return String("/");
  return p;
}

// ---------- Directory listing API ----------
// Escape straight into the response (no temporary String) to avoid heap churn.
static void jsonEscapeTo(Print &out, const String &in) {
  for (size_t i = 0; i < in.length(); ++i) {
    uint8_t c = (uint8_t)in[i];
    switch (c) {
      case '"':  out.print("\\\""); break;
      case '\\': out.print("\\\\"); break;
      case '\n': out.print("\\n");  break;
      case '\r': out.print("\\r");  break;
      case '\t': out.print("\\t");  break;
      default:
        if (c < 0x20) { char u[7]; snprintf(u, sizeof(u), "\\u%04x", c); out.print(u); }
        else out.write(c);
    }
  }
}

static void handleList(AsyncWebServerRequest *req) {
  String path = "/";
  if (req->hasParam("path")) path = normalizePath(req->getParam("path")->value());
  if (!sdReady) { req->send(503, "application/json", "{\"error\":\"sd not ready\"}"); return; }
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    req->send(404, "application/json", "{\"error\":\"not a directory\"}");
    return;
  }
  // Stream the listing instead of materializing one big String in heap.
  AsyncResponseStream *resp = req->beginResponseStream("application/json");
  resp->print("{\"path\":\"");
  jsonEscapeTo(*resp, path);
  resp->print("\",\"entries\":[");
  const bool rootDir = path.endsWith("/");
  bool first = true;
  File entry;
  while ((entry = dir.openNextFile())) {
    String name = String(entry.name());
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.startsWith(".")) { entry.close(); continue; }
    if (!first) resp->print(',');
    first = false;
    resp->print("{\"n\":\"");
    jsonEscapeTo(*resp, name);
    resp->print("\",\"p\":\"");
    jsonEscapeTo(*resp, path);
    if (!rootDir) resp->print('/');
    jsonEscapeTo(*resp, name);
    if (entry.isDirectory()) {
      resp->print("\",\"d\":true}");
    } else {
      resp->print("\",\"d\":false,\"s\":");
      resp->print((uint32_t)entry.size());
      resp->print('}');
    }
    entry.close();
  }
  dir.close();
  resp->print("]}");
  req->send(resp);
}

static void handleFile(AsyncWebServerRequest *req) {
  if (!sdReady) { req->send(503, "text/plain", "sd not ready"); return; }
  if (!req->hasParam("path")) { req->send(400, "text/plain", "missing path"); return; }
  String path = normalizePath(req->getParam("path")->value());
  if (!SD.exists(path)) { req->send(404, "text/plain", "not found"); return; }
  File f = SD.open(path);
  if (!f || f.isDirectory()) {
    if (f) f.close();
    req->send(404, "text/plain", "not a file");
    return;
  }
  String mime = mimeFor(path);
  f.close();
  AsyncWebServerResponse *resp = req->beginResponse(SD, path, mime.c_str());
  resp->addHeader("Accept-Ranges", "bytes");
  req->send(resp);
}

static void handleStatus(AsyncWebServerRequest *req) {
  char buf[200];
  snprintf(buf, sizeof(buf),
    "{\"name\":\"mm26-nomad\",\"version\":\"%s\",\"uptime\":%lu,\"clients\":%u,"
    "\"heap\":%lu,\"sd_ok\":%s,\"sd_size\":%lu}",
    NOMAD_VERSION,
    (unsigned long)(millis() / 1000),
    (unsigned)WiFi.softAPgetStationNum(),
    (unsigned long)ESP.getFreeHeap(),
    sdReady ? "true" : "false",
    (unsigned long)(sdCardSizeBytes / (1024ULL * 1024ULL)));
  req->send(200, "application/json", buf);
}

// ---------- HTTP: main page with embedded HTML ----------
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Maroon's Motherboard – Offline Library</title>
  <style>
    :root {
      --bg: #faf0e1;
      --surface: #fff8ef;
      --text: #111111;
      --text-muted: #6b5c45;
      --acc: #e8503a;
      --border: #111111;
      --card-bg: #fff8ef;
    }
    body[data-theme="dark"] {
      --bg: #3a434a;
      --surface: #3a434a;
      --text: #ffffff;
      --text-muted: #c2cace;
      --acc: #e8503a;
      --border: #000000;
      --card-bg: #3a434a;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: var(--bg);
      color: var(--text);
      font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif;
      line-height: 1.4;
      overflow-x: hidden;
      -webkit-tap-highlight-color: transparent;
    }
    .masthead {
      position: relative;
      padding: 1.25rem 1rem 1.5rem 2.75rem;
      border-bottom: 3px solid var(--border);
      background-image: linear-gradient(rgba(250,240,225,0.72), rgba(250,240,225,0.82)), url('/assets/banner.png');
      background-size: cover;
      background-position: center;
    }
    body[data-theme="dark"] .masthead {
      background-image: linear-gradient(rgba(24,28,32,0.62), rgba(24,28,32,0.74)), url('/assets/banner.png');
    }
    .rail {
      position: absolute;
      left: 0; top: 0; bottom: 0;
      width: 2.25rem;
      background: var(--acc);
      color: #fff;
      text-decoration: none;
      writing-mode: vertical-rl;
      transform: rotate(180deg);
      display: flex;
      align-items: center;
      justify-content: center;
      font-weight: 800;
      letter-spacing: 0.18em;
      font-size: 0.7rem;
      text-transform: uppercase;
    }
    .masthead-top { display: flex; justify-content: flex-end; margin-bottom: 0.75rem; }
    .masthead h4 {
      margin-top: 0.6rem;
      max-width: 40ch;
      font-size: 0.78rem;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0.04em;
      line-height: 1.35;
      color: var(--text-muted);
    }
    h1 {
      font-weight: 900;
      font-size: clamp(2.3rem, 13vw, 4.5rem);
      line-height: 0.88;
      letter-spacing: -0.035em;
      text-transform: uppercase;
      color: var(--text);
    }
    .theme-buttons { display: flex; gap: 0; }
    .theme-btn {
      background: var(--bg);
      border: 2px solid var(--border);
      border-left-width: 0;
      padding: 0.45rem 0.8rem;
      font-size: 0.68rem;
      font-weight: 800;
      text-transform: uppercase;
      letter-spacing: 0.06em;
      cursor: pointer;
      color: var(--text);
    }
    .theme-btn:first-child { border-left-width: 2px; }
    .theme-btn.active { background: var(--acc); border-color: var(--acc); color: #fff; }
    .controls-row {
      max-width: 1200px;
      margin: 0 auto;
      padding: 0.75rem 1rem;
      display: flex;
      align-items: center;
      justify-content: space-between;
      flex-wrap: wrap;
      gap: 0.75rem;
    }
    .nav-group {
      display: flex;
      align-items: center;
      gap: 0.75rem;
      flex-wrap: wrap;
    }
    .back-btn, .view-btn {
      background: var(--bg);
      border: 2px solid var(--border);
      padding: 0.4rem 0.8rem;
      font-size: 0.72rem;
      font-weight: 800;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      cursor: pointer;
    }
    .back-btn { background: var(--acc); color: #fff; }
    .view-btn.active { background: var(--acc); color: #fff; border-color: var(--acc); }
    .breadcrumb {
      color: var(--text-muted);
      font-size: 0.75rem;
      font-weight: 800;
      text-transform: uppercase;
      letter-spacing: 0.03em;
      overflow-x: auto;
      white-space: nowrap;
      padding: 0.5rem 0;
    }
    .breadcrumb a {
      color: var(--text);
      text-decoration: none;
    }
    .breadcrumb a:hover { color: var(--acc); }
    .layout {
      max-width: 1200px;
      margin: 0 auto;
      padding: 0 1rem;
      display: flex;
      gap: 1.5rem;
      flex-wrap: wrap;
    }
    .col-left {
      flex: 2;
      min-width: 0;
    }
    .record {
      flex: 1;
      min-width: 220px;
      background: var(--surface);
      border: 3px solid var(--border);
      padding: 1rem;
      height: fit-content;
    }
    .record-title {
      font-weight: 900;
      text-transform: uppercase;
      font-size: 1.05rem;
      letter-spacing: -0.01em;
      color: var(--acc);
      margin-bottom: 1rem;
    }
    .record-dl {
      display: grid;
      grid-template-columns: minmax(86px, 36%) 1fr;
      gap: 0.55rem 0.9rem;
      font-size: 0.82rem;
    }
    .record-dl dt { font-weight: 800; text-transform: uppercase; text-align: right; }
    .record-dl dd { margin: 0; line-height: 1.45; }
    .grid-view {
      display: grid;
      grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
      gap: 0.75rem;
      margin-top: 0.5rem;
    }
    .table-view {
      width: 100%;
      border-collapse: collapse;
      font-size: 0.8rem;
      margin-top: 0.5rem;
    }
    .table-view th, .table-view td {
      padding: 0.75rem 0.8rem;
      text-align: left;
      border-bottom: 2px solid var(--border);
      white-space: nowrap;
    }
    .table-view th {
      background: var(--acc);
      color: #fff;
      font-weight: 900;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      position: sticky;
      top: 0;
    }
    .table-view tbody tr { cursor: pointer; }
    .table-view tbody tr:hover { background: var(--acc); color: #fff; }
    .card {
      background: var(--card-bg);
      border: 2px solid var(--border);
      overflow: hidden;
      cursor: pointer;
      transition: background .12s, color .12s;
    }
    .card:hover { background: var(--acc); color: #fff; }
    .cover {
      width: 100%;
      aspect-ratio: 2/3;
      background: var(--bg);
      border-bottom: 2px solid var(--border);
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 2.5rem;
    }
    .info { padding: 0.5rem 0.5rem 0.7rem; }
    .title {
      font-weight: 800;
      font-size: 0.75rem;
      text-transform: uppercase;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .subtitle {
      font-size: 0.6rem;
      font-weight: 700;
      letter-spacing: 0.05em;
      text-transform: uppercase;
      color: var(--text-muted);
    }
    .music-meta {
      font-size: 0.6rem;
      margin-top: 0.2rem;
      color: var(--text-muted);
    }
    footer {
      text-align: left;
      padding: 1.25rem 1rem 2rem 2.75rem;
      font-size: 0.7rem;
      font-weight: 800;
      letter-spacing: 0.05em;
      text-transform: uppercase;
      color: var(--text);
      border-top: 3px solid var(--border);
      margin-top: 1rem;
    }
    footer strong { color: var(--acc); }
    /* Modal */
    .modal {
      display: none;
      position: fixed;
      inset: 0;
      background: rgba(0,0,0,0.85);
      z-index: 2000;
      justify-content: center;
      align-items: center;
    }
    .modal-content {
      background: var(--surface);
      width: 100%;
      height: auto;
      max-width: 600px;
      max-height: 80%;
      display: flex;
      flex-direction: column;
      border: 3px solid var(--border);
      overflow: hidden;
    }
    .modal-header {
      padding: 0.9rem 1rem;
      background: var(--acc);
      display: flex;
      justify-content: space-between;
      align-items: center;
    }
    .modal-header h3 {
      margin: 0;
      font-weight: 900;
      font-size: 1.2rem;
      text-transform: uppercase;
      letter-spacing: -0.01em;
      color: #fff;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .close-modal { background: transparent; border: none; font-size: 2rem; line-height: 1; cursor: pointer; color: #fff; }
    .modal-actions {
      display: flex;
      border-bottom: 3px solid var(--border);
    }
    .modal-actions button, .modal-actions a {
      background: var(--bg);
      border: none;
      border-right: 2px solid var(--border);
      padding: 0.85rem 1.25rem;
      font-weight: 800;
      cursor: pointer;
      text-decoration: none;
      color: var(--text);
      font-size: 0.8rem;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      flex: 1;
      text-align: center;
    }
    .modal-actions button:hover, .modal-actions a:hover { background: var(--acc); color: #fff; }
    .modal-body { flex: 1; overflow: auto; padding: 1.25rem; }
    .metadata {
      background: var(--bg);
      padding: 1.1rem 1.2rem;
      border: 2px solid var(--border);
      font-size: 0.9rem;
      line-height: 1.8;
    }
    .metadata strong { text-transform: uppercase; letter-spacing: 0.03em; }
    .loading { padding: 0.25rem 0 1.25rem; }
    .loading-track { width: 100%; height: 10px; background: var(--bg); border: 2px solid var(--border); overflow: hidden; position: relative; }
    .loading-fill { height: 100%; width: 0%; background: var(--acc); transition: width .15s ease; }
    .loading-track.indeterminate .loading-fill { width: 40%; position: absolute; left: -40%; animation: loadSlide 1.1s infinite ease-in-out; }
    @keyframes loadSlide { 0% { left: -40%; } 100% { left: 100%; } }
    .loading-label { margin-top: 0.5rem; font-size: 0.75rem; font-weight: 800; text-align: center; }
    .cancel-btn { display: block; margin: 0.7rem auto 0; background: #111; color: #fff; border: 2px solid var(--border); padding: 0.5rem 1.2rem; font-size: 0.72rem; font-weight: 800; cursor: pointer; }
    /* Reader container for modal */
    .reader-container {
      width: 100%;
      height: 70vh;
      background: #fff;
      margin-top: 1rem;
    }
    .reader-container iframe, .reader-container embed {
      width: 100%;
      height: 100%;
      border: none;
    }
    @media (min-width: 600px) {
      .masthead { padding-left: 3.25rem; min-height: 260px; }
      .rail { width: 2.75rem; font-size: 0.8rem; }
      .grid-view { gap: 1rem; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); }
      .modal-content { width: 90%; }
    }
  </style>
  <script src="/assets/epub.js"></script>
</head>
<body data-theme="light">
  <header class="masthead">
    <a class="rail" href="https://linktr.ee/memoryworkersguild" target="_blank" rel="noopener">Memory Workers Guild</a>
    <div class="masthead-top">
      <div class="theme-buttons">
        <button class="theme-btn" data-theme="light">Light</button>
        <button class="theme-btn" data-theme="dark">Night</button>
      </div>
    </div>
    <h1>Maroon's<br>Motherboard</h1>
    <h4>Offline Library – PDF/EPUB reader for /library, music metadata for other folders</h4>
  </header>
  <div class="controls-row">
    <div class="nav-group">
      <button id="backBtn" class="back-btn" style="display:none;">← Back</button>
      <div class="breadcrumb" id="breadcrumb"></div>
    </div>
    <div class="nav-group">
      <button id="gridViewBtn" class="view-btn active">Grid</button>
      <button id="tableViewBtn" class="view-btn">Table</button>
    </div>
  </div>
  <div class="layout">
    <div class="col-left">
      <div id="content"></div>
    </div>
    <aside id="record" class="record" style="display:none;"></aside>
  </div>
  <footer>Offline archive · Connect to <strong>MM-2026</strong> · No internet required</footer>

  <!-- Security modal (first visit) -->
  <div id="securityModal" class="modal" style="background:rgba(0,0,0,0.8);z-index:3000;">
    <div class="security-card" style="background: var(--acc); color:#fff; border:3px solid #111; max-width:440px; padding:2rem;">
      <h2 style="font-size:1.7rem;">Maroon's Motherboard</h2>
      <p>This is an offline local network library. The connection is secure, WPA2 password protected, encrypted and private.</p>
      <button id="enterLibraryBtn" style="background:#111; color:#fff; border:none; padding:0.8rem 1.7rem; font-weight:800;">Enter library</button>
    </div>
  </div>

  <!-- File modal -->
  <div id="fileModal" class="modal">
    <div class="modal-content">
      <div class="modal-header">
        <h3 id="modalTitle">File details</h3>
        <button class="close-modal" id="closeModalBtn">&times;</button>
      </div>
      <div class="modal-actions" id="modalActions">
        <a id="downloadBtn" href="#" target="_blank">Download</a>
      </div>
      <div class="modal-body">
        <div id="loadingBar" class="loading" style="display:none;">
          <div class="loading-track"><div class="loading-fill" id="loadingFill"></div></div>
          <div class="loading-label" id="loadingLabel">Downloading…</div>
          <button id="cancelDownload" class="cancel-btn" style="display:none;">Cancel</button>
        </div>
        <div class="metadata" id="metadataDiv"></div>
        <div id="readerContainer" style="display:none;" class="reader-container"></div>
      </div>
    </div>
  </div>

  <script>
    // Security modal once
    (function() {
      const sec = document.getElementById('securityModal');
      const btn = document.getElementById('enterLibraryBtn');
      if (!localStorage.getItem('libraryWelcomeSeen')) sec.style.display = 'flex';
      btn.addEventListener('click', () => {
        localStorage.setItem('libraryWelcomeSeen', '1');
        sec.style.display = 'none';
      });
    })();

    // Theme handling
    const setTheme = (theme) => {
      document.body.setAttribute('data-theme', theme);
      localStorage.setItem('libraryTheme', theme);
      document.querySelectorAll('.theme-btn').forEach(btn => {
        if (btn.dataset.theme === theme) btn.classList.add('active');
        else btn.classList.remove('active');
      });
    };
    const savedTheme = localStorage.getItem('libraryTheme');
    if (savedTheme && ['light','dark'].includes(savedTheme)) setTheme(savedTheme);
    else setTheme('light');
    document.querySelectorAll('.theme-btn').forEach(btn => {
      btn.addEventListener('click', () => setTheme(btn.dataset.theme));
    });

    // Global state
    let currentPath = '/library';
    let viewMode = 'grid';
    let currentFile = null;
    let currentMeta = null;
    let activeXhr = null;
    let isLibraryMode = true;  // true when path starts with /library

    const contentDiv = document.getElementById('content');
    const backBtn = document.getElementById('backBtn');
    const breadcrumbDiv = document.getElementById('breadcrumb');
    const gridViewBtn = document.getElementById('gridViewBtn');
    const tableViewBtn = document.getElementById('tableViewBtn');
    const recordAside = document.getElementById('record');
    const modal = document.getElementById('fileModal');
    const modalTitle = document.getElementById('modalTitle');
    const metadataDiv = document.getElementById('metadataDiv');
    const downloadBtn = document.getElementById('downloadBtn');
    const closeModalBtn = document.getElementById('closeModalBtn');
    const modalActions = document.getElementById('modalActions');
    const readerContainer = document.getElementById('readerContainer');
    const loadingBar = document.getElementById('loadingBar');
    const loadingFill = document.getElementById('loadingFill');
    const loadingLabel = document.getElementById('loadingLabel');
    const cancelDownloadBtn = document.getElementById('cancelDownload');

    function formatBytes(b) {
      if (b < 1024) return b + " B";
      const u = ["KB","MB","GB","TB"];
      let i = -1;
      do { b /= 1024; i++; } while (b >= 1024 && i < u.length-1);
      return b.toFixed(b<10?2:1) + " " + u[i];
    }

    function escapeHtml(str) {
      if (!str) return '';
      return str.replace(/[&<>]/g, m => ({ '&':'&amp;', '<':'&lt;', '>':'&gt;' }[m]));
    }

    function getFileIcon(ext) {
      const icons = { pdf:'📄', epub:'📖', mp3:'🎵', flac:'🎵', ogg:'🎵', wav:'🎵', m4a:'🎵',
                      mp4:'🎬', mov:'🎬', jpg:'🖼️', jpeg:'🖼️', png:'🖼️', gif:'🖼️',
                      txt:'📃', html:'🌐', css:'🎨', js:'⚙️', zip:'🗜️' };
      return icons[ext.toLowerCase()] || '📁';
    }

    function isMusicFile(ext) { return ['mp3','flac','ogg','wav','m4a'].includes(ext.toLowerCase()); }
    function isBookFile(ext) { return ['pdf','epub'].includes(ext.toLowerCase()); }

    // Load meta.json from current path
    async function loadMetaForPath(path) {
      try {
        const resp = await fetch(`/file?path=${encodeURIComponent(path + '/meta.json')}`);
        if (!resp.ok) throw new Error();
        const meta = await resp.json();
        currentMeta = meta;
        if (meta.record && typeof meta.record === 'object') {
          let html = '';
          if (meta.heading) html += `<div class="record-title">${escapeHtml(meta.heading)}</div>`;
          html += '<dl class="record-dl">';
          for (const [label, value] of Object.entries(meta.record)) {
            if (value == null || value === '') continue;
            html += `<dt>${escapeHtml(label)}</dt><dd>${escapeHtml(String(value))}</dd>`;
          }
          html += '</dl>';
          recordAside.innerHTML = html;
          recordAside.style.display = 'block';
        } else {
          recordAside.style.display = 'none';
        }
      } catch(e) {
        currentMeta = null;
        recordAside.style.display = 'none';
      }
    }

    // Inline reader (PDF/EPUB) inside modal
    function showReader(file) {
      readerContainer.style.display = 'block';
      readerContainer.innerHTML = ''; // clear
      const url = `/file?path=${encodeURIComponent(file.path)}`;
      if (file.type === 'pdf') {
        const iframe = document.createElement('iframe');
        iframe.src = url;
        iframe.style.width = '100%';
        iframe.style.height = '100%';
        iframe.style.border = 'none';
        readerContainer.appendChild(iframe);
      } else if (file.type === 'epub' && typeof ePub !== 'undefined') {
        const epubDiv = document.createElement('div');
        epubDiv.id = 'epubViewerTemp';
        epubDiv.style.width = '100%';
        epubDiv.style.height = '100%';
        epubDiv.style.overflow = 'auto';
        readerContainer.appendChild(epubDiv);
        const book = ePub(url);
        book.renderTo('epubViewerTemp', { width: '100%', height: '100%' }).display();
      } else {
        readerContainer.innerHTML = '<div style="padding:1rem;">Unsupported format for inline reading. Use Download.</div>';
      }
    }

    // Show modal: if library mode and book file, add Read button; else only Download
    function showFileModal(file, isLibrary) {
      currentFile = file;
      modalTitle.innerText = file.name;
      let extra = '';
      if (file.pages) extra += `<br><strong>Pages:</strong> ${escapeHtml(String(file.pages))}`;
      if (file.description) extra += `<br><strong>Description:</strong> ${escapeHtml(file.description)}`;
      if (file.artist) extra += `<br><strong>Artist:</strong> ${escapeHtml(file.artist)}`;
      if (file.album) extra += `<br><strong>Album:</strong> ${escapeHtml(file.album)}`;
      if (file.year) extra += `<br><strong>Year:</strong> ${escapeHtml(file.year)}`;
      metadataDiv.innerHTML = `
        <strong>File:</strong> ${escapeHtml(file.name)}<br>
        <strong>Size:</strong> ${formatBytes(file.size)}<br>
        <strong>Type:</strong> ${file.type.toUpperCase()}${extra}<br>
        <strong>Path:</strong> ${escapeHtml(file.path)}
      `;
      downloadBtn.href = `/file?path=${encodeURIComponent(file.path)}`;
      // Remove existing Read button if present
      let readBtn = document.getElementById('readBtnModal');
      if (readBtn) readBtn.remove();
      readerContainer.style.display = 'none';
      readerContainer.innerHTML = '';
      // If library mode and book file, add Read button
      if (isLibrary && isBookFile(file.type)) {
        readBtn = document.createElement('button');
        readBtn.id = 'readBtnModal';
        readBtn.textContent = 'Read';
        readBtn.onclick = () => showReader(file);
        modalActions.insertBefore(readBtn, downloadBtn);
      }
      modal.style.display = 'flex';
    }

    // Download with progress (same as before)
    function downloadFile(file) {
      if (!file) return;
      if (activeXhr) activeXhr.abort();
      showLoading(`Downloading ${file.name}… 0%`, false);
      cancelDownloadBtn.style.display = 'block';
      const xhr = new XMLHttpRequest();
      activeXhr = xhr;
      xhr.open('GET', `/file?path=${encodeURIComponent(file.path)}`, true);
      xhr.responseType = 'blob';
      xhr.onprogress = (e) => {
        const total = e.lengthComputable ? e.total : file.size;
        if (total) {
          const pct = Math.round(e.loaded / total * 100);
          setProgress(pct);
          loadingLabel.textContent = `Downloading… ${pct}% (${formatBytes(e.loaded)} / ${formatBytes(total)})`;
        } else {
          loadingLabel.textContent = `Downloading… ${formatBytes(e.loaded)}`;
        }
      };
      xhr.onload = () => {
        activeXhr = null;
        cancelDownloadBtn.style.display = 'none';
        if (xhr.status === 200) {
          setProgress(100);
          loadingLabel.textContent = 'Saving…';
          const url = URL.createObjectURL(xhr.response);
          const a = document.createElement('a');
          a.href = url; a.download = file.name;
          document.body.appendChild(a); a.click(); a.remove();
          setTimeout(() => URL.revokeObjectURL(url), 1000);
          setTimeout(hideLoading, 700);
        } else loadingLabel.textContent = '⚠️ Download failed.';
      };
      xhr.onerror = () => { activeXhr = null; cancelDownloadBtn.style.display = 'none'; loadingLabel.textContent = '⚠️ Download failed.'; };
      xhr.send();
    }

    function showLoading(label, indeterminate) {
      loadingLabel.textContent = label || 'Loading…';
      const track = document.querySelector('#loadingBar .loading-track');
      track.classList.toggle('indeterminate', !!indeterminate);
      loadingFill.style.width = indeterminate ? '' : '0%';
      loadingBar.style.display = 'block';
    }
    function setProgress(pct) {
      const track = document.querySelector('#loadingBar .loading-track');
      track.classList.remove('indeterminate');
      loadingFill.style.width = Math.max(0, Math.min(100, pct)) + '%';
    }
    function hideLoading() {
      loadingBar.style.display = 'none';
      document.querySelector('#loadingBar .loading-track').classList.remove('indeterminate');
      loadingFill.style.width = '0%';
      cancelDownloadBtn.style.display = 'none';
    }

    cancelDownloadBtn.onclick = () => {
      if (activeXhr) { activeXhr.abort(); activeXhr = null; }
      cancelDownloadBtn.style.display = 'none';
      loadingLabel.textContent = 'Download canceled.';
      setTimeout(hideLoading, 800);
    };
    closeModalBtn.onclick = () => {
      if (activeXhr) { activeXhr.abort(); activeXhr = null; }
      modal.style.display = 'none';
      hideLoading();
      readerContainer.style.display = 'none';
      readerContainer.innerHTML = '';
    };
    window.onclick = (e) => { if (e.target === modal) closeModalBtn.click(); };
    downloadBtn.onclick = (e) => { e.preventDefault(); downloadFile(currentFile); };

    // Render directory (grid or table)
    function renderDirectory(entries, isLibrary) {
      const dirs = entries.filter(e => e.d).map(e => ({ name: e.n, path: e.p, isDir: true }));
      let files = entries.filter(e => !e.d).map(e => ({
        name: e.n, path: e.p, size: e.s, isDir: false,
        type: e.n.split('.').pop().toLowerCase(),
        ext: e.n.split('.').pop().toLowerCase()
      }));
      // Enrich with meta.json if available
      if (currentMeta && currentMeta.files) {
        for (const f of files) {
          const md = currentMeta.files[f.name];
          if (md) {
            if (md.pages) f.pages = md.pages;
            if (md.description) f.description = md.description;
            if (md.artist) f.artist = md.artist;
            if (md.album) f.album = md.album;
            if (md.year) f.year = md.year;
          }
        }
      }

      if (viewMode === 'grid') {
        contentDiv.innerHTML = '<div class="grid-view" id="dynamicGrid"></div>';
        const grid = document.getElementById('dynamicGrid');
        const all = [...dirs, ...files];
        for (const item of all) {
          const card = document.createElement('div');
          card.className = 'card';
          const icon = item.isDir ? '📁' : getFileIcon(item.type);
          let subtitle = item.isDir ? 'Folder' : item.type.toUpperCase();
          let extraHtml = '';
          // Only show music metadata if NOT in library mode AND it's a music file
          if (!isLibrary && !item.isDir && isMusicFile(item.ext)) {
            if (item.artist || item.album || item.year) {
              extraHtml = `<div class="music-meta">${item.artist || ''} ${item.album ? '• '+item.album : ''} ${item.year ? '• '+item.year : ''}</div>`;
            }
          }
          card.innerHTML = `<div class="cover">${icon}</div><div class="info"><div class="title">${escapeHtml(item.name)}</div><div class="subtitle">${subtitle}</div>${extraHtml}</div>`;
          card.addEventListener('click', () => {
            if (item.isDir) loadDirectory(item.path);
            else showFileModal(item, isLibrary);
          });
          grid.appendChild(card);
        }
        if (!all.length) contentDiv.innerHTML = '<div style="text-align:center; padding:2rem;">Empty folder</div>';
      } else { // table view
        let html = '<table class="table-view"><thead><tr>';
        if (isLibrary) {
          html += '<th>Name</th><th>Size</th><th>Type</th><th>Path</th>';
        } else {
          html += '<th>Name</th><th>Artist</th><th>Album</th><th>Year</th><th>Size</th><th>Type</th><th>Path</th>';
        }
        html += '</tr></thead><tbody>';
        for (const dir of dirs) {
          html += `<tr data-path="${escapeHtml(dir.path)}" data-is-dir="true"><td>${escapeHtml(dir.name)}/</td>`;
          if (isLibrary) html += '<td>—</td><td>Folder</td>';
          else html += '<td>—</td><td>—</td><td>—</td><td>—</td><td>Folder</td>';
          html += `<td>${escapeHtml(dir.path)}</td></tr>`;
        }
        for (const file of files) {
          const artist = file.artist ? escapeHtml(file.artist) : '—';
          const album = file.album ? escapeHtml(file.album) : '—';
          const year = file.year ? escapeHtml(file.year) : '—';
          html += `<tr data-path="${escapeHtml(file.path)}" data-name="${escapeHtml(file.name)}" data-size="${file.size}" data-type="${escapeHtml(file.type)}" data-artist="${artist}" data-album="${album}" data-year="${year}" data-pages="${file.pages||''}" data-desc="${escapeHtml(file.description||'')}" data-is-dir="false">`;
          html += `<td>${escapeHtml(file.name)}</td>`;
          if (isLibrary) {
            html += `<td>${formatBytes(file.size)}</td><td>${file.type.toUpperCase()}</td><td>${escapeHtml(file.path)}</td>`;
          } else {
            html += `<td>${artist}</td><td>${album}</td><td>${year}</td><td>${formatBytes(file.size)}</td><td>${file.type.toUpperCase()}</td><td>${escapeHtml(file.path)}</td>`;
          }
          html += '</tr>';
        }
        html += '</tbody></table>';
        contentDiv.innerHTML = html;
        document.querySelectorAll('.table-view tbody tr').forEach(row => {
          row.addEventListener('click', () => {
            const isDir = row.dataset.isDir === 'true';
            if (isDir) loadDirectory(row.dataset.path);
            else {
              const file = {
                name: row.dataset.name,
                path: row.dataset.path,
                size: parseInt(row.dataset.size),
                type: row.dataset.type,
                artist: row.dataset.artist !== '—' ? row.dataset.artist : null,
                album: row.dataset.album !== '—' ? row.dataset.album : null,
                year: row.dataset.year !== '—' ? row.dataset.year : null,
                pages: row.dataset.pages || null,
                description: row.dataset.desc || null
              };
              showFileModal(file, isLibrary);
            }
          });
        });
      }
    }

    async function loadDirectory(path) {
      currentPath = path;
      isLibraryMode = path.startsWith('/library');
      backBtn.style.display = (path === '/library' || path === '/') ? 'none' : 'inline-block';
      // breadcrumb
      const parts = path.split('/').filter(p => p);
      let acc = '';
      const crumbs = ['<a href="#" data-path="/">root</a>'];
      for (const p of parts) {
        acc += '/' + p;
        crumbs.push(` / <a href="#" data-path="${acc}">${escapeHtml(p)}</a>`);
      }
      breadcrumbDiv.innerHTML = crumbs.join('');
      document.querySelectorAll('.breadcrumb a').forEach(link => {
        link.addEventListener('click', (e) => {
          e.preventDefault();
          loadDirectory(link.dataset.path);
        });
      });
      // load meta.json only for library mode (optional)
      if (isLibraryMode) {
        await loadMetaForPath(path);
      } else {
        recordAside.style.display = 'none';
        currentMeta = null;
      }
      contentDiv.innerHTML = '<div style="text-align:center; padding:2rem;">Loading files…</div>';
      try {
        const resp = await fetch(`/api/list?path=${encodeURIComponent(path)}`);
        if (!resp.ok) throw new Error();
        const data = await resp.json();
        renderDirectory(data.entries, isLibraryMode);
      } catch(e) {
        contentDiv.innerHTML = '<div style="text-align:center; padding:2rem; color: var(--acc);">⚠️ Failed to load directory.</div>';
      }
    }

    function setView(mode) {
      viewMode = mode;
      if (mode === 'grid') {
        gridViewBtn.classList.add('active');
        tableViewBtn.classList.remove('active');
      } else {
        tableViewBtn.classList.add('active');
        gridViewBtn.classList.remove('active');
      }
      loadDirectory(currentPath);
    }
    gridViewBtn.onclick = () => setView('grid');
    tableViewBtn.onclick = () => setView('table');

    backBtn.onclick = () => {
      const parts = currentPath.split('/').filter(Boolean);
      parts.pop();
      const parent = parts.length ? '/' + parts.join('/') : '/library';
      loadDirectory(parent);
    };

    // Initial load: check /library existence, fallback to root
    fetch('/api/list?path=%2Flibrary').then(r => {
      if (!r.ok) {
        currentPath = '/';
        loadDirectory('/');
      } else {
        loadDirectory('/library');
      }
    }).catch(() => {
      currentPath = '/';
      loadDirectory('/');
    });
  </script>
</body>
</html>)HTML";

static void sendCaptiveRedirect(AsyncWebServerRequest *req) {
  AsyncWebServerResponse *r = req->beginResponse(302, "text/plain", "");
  r->addHeader("Location", "http://" + String(DOMAIN_NAME) + "/");
  req->send(r);
}

static void setupHttp() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){
    r->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/list",   HTTP_GET, handleList);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/file",       HTTP_GET, handleFile);
  // Static files (CSS, JS, banner.png, etc.) from SD card
  server.onNotFound([](AsyncWebServerRequest *req){
    String url = req->url();
    if (url.endsWith("/")) url += "index.html";
    String path = normalizePath(url);
    if (sdReady && SD.exists(path)) {
      String mime = mimeFor(path);
      req->send(SD, path, mime);
    } else {
      sendCaptiveRedirect(req);
    }
  });

  // Captive portal probes
  server.on("/generate_204",       HTTP_GET, sendCaptiveRedirect);
  server.on("/gen_204",            HTTP_GET, sendCaptiveRedirect);
  server.on("/hotspot-detect.html",HTTP_GET, sendCaptiveRedirect);
  server.on("/library/test/success.html", HTTP_GET, sendCaptiveRedirect);
  server.on("/ncsi.txt",           HTTP_GET, sendCaptiveRedirect);
  server.on("/connecttest.txt",    HTTP_GET, sendCaptiveRedirect);
  server.on("/redirect",           HTTP_GET, sendCaptiveRedirect);
  server.on("/canonical.html",     HTTP_GET, sendCaptiveRedirect);
  server.begin();
}

// ============================================================================
// OLED display (unchanged from your working version – includes intro, dashboard, flag)
// ============================================================================
static void noteInteraction() { lastInteractionMs = millis(); }

static void drawIntroFrame(uint32_t elapsed) {
  display.clearDisplay();
  const int dialY = 36;
  uint32_t sweepEnd = 1600;
  int reach = elapsed >= sweepEnd ? 128 : (int)((elapsed * 128) / sweepEnd);
  display.drawFastHLine(0, dialY, reach, SSD1306_WHITE);
  for (int x = 0; x < reach; x += 4) {
    int h = (x % 16 == 0) ? 5 : ((x % 8 == 0) ? 3 : 2);
    display.drawFastVLine(x, dialY - h, h, SSD1306_WHITE);
  }
  if (elapsed < sweepEnd) {
    int nx = reach;
    display.drawFastVLine(nx, dialY - 10, 14, SSD1306_WHITE);
    display.fillTriangle(nx - 2, dialY - 12, nx + 2, dialY - 12, nx, dialY - 9, SSD1306_WHITE);
  }
  if (elapsed >= 1200) {
    display.setFont(&FreeSerifItalic9pt7b);
    display.setTextColor(SSD1306_WHITE);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(DISPLAY_TITLE, 0, 0, &x1, &y1, &tw, &th);
    int targetX = (128 - (int)tw) / 2;
    int targetY = 18;
    uint32_t lockStart = 1200, lockEnd = 2800;
    int x;
    if (elapsed >= lockEnd) x = targetX;
    else {
      float t = (float)(elapsed - lockStart) / (float)(lockEnd - lockStart);
      t = 1.0f - powf(1.0f - t, 3.0f);
      x = (int)(128 + (targetX - 128) * t);
    }
    display.setCursor(x, targetY);
    display.print(DISPLAY_TITLE);
    display.setFont();
  }
  if (elapsed >= 2800) {
    bool show = ((elapsed - 2800) / 180) % 2 == 0 || elapsed >= 3700;
    if (show) {
      const char *t = "TUNED.  STAND BY.";
      display.setTextSize(1);
      int16_t x1, y1; uint16_t tw, th;
      display.getTextBounds(t, 0, 0, &x1, &y1, &tw, &th);
      display.setCursor((128 - tw) / 2, 52);
      display.print(t);
    }
  }
  display.display();
}

static void drawDashboardFrame() {
  display.clearDisplay();
  display.setFont();                 // compact built-in font: title fits on one line
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Title + underline rule
  display.setCursor(0, 0);
  display.print(DISPLAY_TITLE);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // Tuner dial doubles as a connected-clients signal meter
  const int dialY = 28;
  display.drawFastHLine(0, dialY, 128, SSD1306_WHITE);
  for (int x = 0; x < 128; x += 4) {
    int h = (x % 16 == 0) ? 5 : ((x % 8 == 0) ? 3 : 2);
    display.drawFastVLine(x, dialY - h, h, SSD1306_WHITE);
  }
  uint8_t clients = WiFi.softAPgetStationNum();
  if (clients > AP_MAX_CLIENTS) clients = AP_MAX_CLIENTS;
  int needleX = (clients == 0) ? 2 : (int)((float)clients / (float)AP_MAX_CLIENTS * 124.0f);
  display.drawFastVLine(needleX, dialY - 9, 12, SSD1306_WHITE);
  display.fillTriangle(needleX - 3, dialY - 11, needleX + 3, dialY - 11, needleX, dialY - 8, SSD1306_WHITE);

  // Rotating hint
  if (millis() - lastMsgRotateMs > MSG_ROTATE_MS) {
    lastMsgRotateMs = millis();
    currentMsg = (currentMsg + 1) % ROTATING_MSG_COUNT;
  }
  display.setCursor(0, 40);
  display.print("> ");
  display.print(ROTATING_MSGS[currentMsg]);

  // Footer: network name (left) + connected count (right)
  display.drawFastHLine(0, 52, 128, SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print(AP_SSID);
  char rhs[16];
  snprintf(rhs, sizeof(rhs), "%u/%u", (unsigned)WiFi.softAPgetStationNum(), (unsigned)AP_MAX_CLIENTS);
  int16_t x1, y1; uint16_t tw, th;
  display.getTextBounds(rhs, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(128 - tw, 56);
  display.print(rhs);
  display.display();
}

static void drawScreensaverOverlay() {
  static const char *OVERLAY_TEXT = "MM-2026  192.168.4.1";
  display.setFont(&TomThumb);
  display.setTextSize(1);
  display.fillRect(0, 56, 92, 8, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 62);
  display.print(OVERLAY_TEXT);
  display.setFont(NULL);
}

static const int FLAG_W = 108, FLAG_H = 54;
static const int FLAG_X = (128 - FLAG_W) / 2, FLAG_Y = (64 - FLAG_H) / 2;
static const int SALTIRE_HALF = 4;
static uint8_t flagBuf[FLAG_W * FLAG_H / 8];
static bool flagReady = false;

static inline void flagSet(int x, int y, bool on) {
  if (x < 0 || y < 0 || x >= FLAG_W || y >= FLAG_H) return;
  int idx = (y * FLAG_W + x) / 8;
  uint8_t bit = 1 << (7 - ((y * FLAG_W + x) % 8));
  if (on) flagBuf[idx] |= bit;
  else    flagBuf[idx] &= ~bit;
}
static inline bool flagGet(int x, int y) {
  if (x < 0 || y < 0 || x >= FLAG_W || y >= FLAG_H) return false;
  int idx = (y * FLAG_W + x) / 8;
  uint8_t bit = 1 << (7 - ((y * FLAG_W + x) % 8));
  return (flagBuf[idx] & bit) != 0;
}
static void buildFlag() {
  memset(flagBuf, 0, sizeof(flagBuf));
  for (int y = 0; y < FLAG_H; ++y) {
    for (int x = 0; x < FLAG_W; ++x) {
      float d1 = (float)y - ((float)x * FLAG_H / FLAG_W);
      float d2 = (float)y - ((float)(FLAG_W - x) * FLAG_H / FLAG_W);
      bool onSaltire = (fabsf(d1) < SALTIRE_HALF * 1.4f) || (fabsf(d2) < SALTIRE_HALF * 1.4f);
      if (onSaltire) { flagSet(x, y, true); continue; }
      float dy = (float)FLAG_H / 2.0f - fabsf((float)x - (float)FLAG_W / 2.0f) * ((float)FLAG_H / (float)FLAG_W);
      bool top = (float)y < dy, bottom = (float)y > (float)FLAG_H - dy;
      if (top || bottom) { if ((y % 3) == 0) flagSet(x, y, true); }
    }
  }
  for (int x = 0; x < FLAG_W; ++x) { flagSet(x, 0, true); flagSet(x, FLAG_H - 1, true); }
  for (int y = 0; y < FLAG_H; ++y) { flagSet(0, y, true); flagSet(FLAG_W - 1, y, true); }
  flagReady = true;
}
static void drawFlagFrame() {
  if (!flagReady) buildFlag();
  display.clearDisplay();
  display.drawFastVLine(FLAG_X - 4, FLAG_Y - 4, FLAG_H + 8, SSD1306_WHITE);
  float phase = (float)millis() / 520.0f;
  float spatialFreq = 0.065f;
  float maxAmp = 3.5f;
  for (int x = 0; x < FLAG_W; ++x) {
    float amp = 2.0f + (float)x / (float)FLAG_W * maxAmp;
    int dy = (int)(sinf(phase + (float)x * spatialFreq) * amp);
    for (int y = 0; y < FLAG_H; ++y) {
      if (flagGet(x, y)) {
        int px = FLAG_X + x, py = FLAG_Y + y + dy;
        if (px >= 0 && px < 128 && py >= 0 && py < 64) display.drawPixel(px, py, SSD1306_WHITE);
      }
    }
  }
  drawScreensaverOverlay();
  display.display();
}
static void drawScreensaverFrame() { drawFlagFrame(); }

static void pollButton() {
  bool pressed = (digitalRead(BOOT_BUTTON) == LOW);
  uint32_t now = millis();
  if (pressed) {
    if (buttonHeldSince == 0) buttonHeldSince = now;
    if (now - buttonHeldSince >= 1500 && !buttonHeldAtBoot) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 28);
      display.print("Switching to");
      display.setCursor(0, 40);
      display.print("Meshtastic...");
      display.display();
      buttonHeldAtBoot = true;
      delay(400);
      if (!switchToMeshtasticAndReboot()) {
        display.clearDisplay();
        display.setCursor(0, 24);
        display.print("Meshtastic not");
        display.setCursor(0, 36);
        display.print("found in OTA0.");
        display.display();
        delay(1500);
      }
    }
    noteInteraction();
  } else buttonHeldSince = 0;
}

static void dnsTask(void *) {
  for (;;) { dns.processNextRequest(); vTaskDelay(pdMS_TO_TICKS(20)); }
}

static void i2cBusRecover(uint8_t sda, uint8_t scl) {
  pinMode(scl, OUTPUT);
  pinMode(sda, INPUT_PULLUP);
  for (int i = 0; i < 9 && digitalRead(sda) == LOW; i++) {
    digitalWrite(scl, LOW); delayMicroseconds(5);
    digitalWrite(scl, HIGH); delayMicroseconds(5);
  }
}
static bool i2cProbe(uint8_t sda, uint8_t scl, uint8_t addr) {
  i2cBusRecover(sda, scl);
  Wire.end();
  if (!Wire.begin(sda, scl, 100000)) return false;
  Wire.setTimeOut(50);
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(80);
  Serial.printf("[boot] %s\n", DISPLAY_TITLE);
  // Log WHY we just (re)started — distinguishes power/cable brownouts from firmware crashes.
  const char *rrs;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   rrs = "power-on (normal)"; break;
    case ESP_RST_SW:        rrs = "software restart"; break;
    case ESP_RST_PANIC:     rrs = "PANIC / crash (firmware bug)"; break;
    case ESP_RST_INT_WDT:   rrs = "interrupt watchdog"; break;
    case ESP_RST_TASK_WDT:  rrs = "task watchdog (a task hung)"; break;
    case ESP_RST_WDT:       rrs = "watchdog"; break;
    case ESP_RST_BROWNOUT:  rrs = "BROWNOUT — power/cable too weak"; break;
    case ESP_RST_DEEPSLEEP: rrs = "wake from deep sleep"; break;
    case ESP_RST_EXT:       rrs = "external reset"; break;
    default:                rrs = "unknown"; break;
  }
  Serial.printf("[boot] reset reason: %s\n", rrs);
  bootMillis = millis();
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  struct { uint8_t sda, scl; } cand[] = { { OLED_SDA, OLED_SCL }, { 4, 15 } };
  bool oledOk = false;
  for (auto &c : cand) {
    Serial.printf("[oled] probe sda=%u scl=%u ... ", c.sda, c.scl);
    if (i2cProbe(c.sda, c.scl, OLED_ADDR)) {
      Serial.println("found");
      if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false)) {
        display.clearDisplay(); display.display();
        oledOk = true;
        Serial.printf("[oled] up on sda=%u scl=%u\n", c.sda, c.scl);
      } else Serial.println("[oled] begin failed");
      break;
    }
    Serial.println("no ack");
  }
  if (!oledOk) Serial.println("[oled] not found");
  prefs.begin("nomad", false);
  prefs.putString("last_mode", "nomad");
  sdReady = initSD();
  Serial.printf("[sd] %s\n", sdReady ? "ok" : "FAIL");
  // WiFi AP – force clean start to avoid IP conflicts
  WiFi.mode(WIFI_AP);
  WiFi.softAPdisconnect(true);
  delay(100);

  // Deauthenticate all stations (clear ghost clients)
  esp_wifi_deauth_sta(0);
  delay(100);

  // Re‑start the access point with a deterministic IP + clean DHCP lease pool
  WiFi.mode(WIFI_AP);
  delay(100);
  IPAddress apIP(192, 168, 4, 1), apMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, apMask);    // initialise the DHCP pool cleanly
  WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, AP_MAX_CLIENTS);
  WiFi.setSleep(false);                     // keep connection stable
  delay(100);

  // Hand out long DHCP leases so a reconnecting / second client keeps a distinct
  // address instead of the server re-issuing 192.168.4.2 (the IP conflict you saw).
  esp_netif_t *apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (apNetif) {
    esp_netif_dhcps_stop(apNetif);
    uint32_t leaseMinutes = 720;            // 12 hours
    esp_netif_dhcps_option(apNetif, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME,
                           &leaseMinutes, sizeof(leaseMinutes));
    esp_netif_dhcps_start(apNetif);
  }

  IPAddress ip = WiFi.softAPIP();
  Serial.printf("[ap] %s @ %s, max clients=%u, sleep disabled\n", AP_SSID, ip.toString().c_str(), AP_MAX_CLIENTS);
  dns.start(53, "*", ip);
  setupHttp();
  xTaskCreatePinnedToCore(dnsTask, "dns", 2048, nullptr, 1, nullptr, 0);
  lastInteractionMs = millis();
  lastMsgRotateMs = millis();
}

void loop() {
  pollButton();
  uint32_t now = millis(), sinceBoot = now - bootMillis;
  if (dispState == ST_INTRO) {
    drawIntroFrame(sinceBoot);
    if (sinceBoot >= INTRO_DURATION_MS) { dispState = ST_DASHBOARD; noteInteraction(); }
  } else {
    static uint8_t lastClients = 0;
    uint8_t c = WiFi.softAPgetStationNum();
    if (c != lastClients) { noteInteraction(); lastClients = c; }
    bool idle = (now - lastInteractionMs) > SCREENSAVER_AFTER_MS;
    if (idle) { dispState = ST_SCREENSAVER; drawScreensaverFrame(); }
    else { dispState = ST_DASHBOARD; drawDashboardFrame(); }
  }
  delay(40);
}