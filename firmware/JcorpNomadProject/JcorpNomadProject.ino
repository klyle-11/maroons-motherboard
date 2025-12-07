//Jcorp Nomad Project
#include "Arduino.h"
#define FF_USE_FASTSEEK 1
#define SD_FREQ_KHZ 10000
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "FS.h"
#include "SD_MMC.h"
#include "DNSServer.h"
#include <ArduinoJson.h>
#include <map>
#include "Display_ST7789.h"
#include "LVGL_Driver.h"
#include "ui.h"
#include "RGB_lamp.h"
#include <SPIFFS.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "usb_mode.h"
#include "boot_mode.h" // custom library for firmware switching
#include <map>
void launch_usb_mode() {
extern void usb_setup();
extern void usb_loop();
  // Initialize only the USB‑MSC path
  usb_setup();

  // Then run only that path forever
  for (;;) {
    usb_loop();
    // optionally: delay(0); yield();
  }
}
#define BOOT_BUTTON_PIN 0  
int screenBrightness = 100; // 0-100, default full brightness
void handleConnector(AsyncWebServerRequest *request);
unsigned long lastTempReading = 0;
float currentTempC = 0.0;
static bool sdScanned = false;
const uint32_t SD_SCAN_DELAY = 5000;  // milliseconds after boot
static TaskHandle_t indexTaskHandle = NULL;
static std::map<AsyncWebServerRequest*, String> gBodyBuf;
std::map<AsyncWebServerRequest*, String> renameBodies;
std::map<AsyncWebServerRequest*, String> renameBodyBuffer;
// ==================== CONFIGURATION ====================

// Max number of devices that can connect at once
#define MAX_CLIENTS 4 // I recomend 4, If you want to try more knock yourself out!

struct AdminSettings {
  String rgbMode = "off";
  String rgbColor = "#ff0000";
  String adminPassword = "";
  String wifiSSID = "Jcorp_Nomad";
  String wifiPassword = "password";
  int brightness = 230;
  bool autoGenerateMedia = false;
};

AdminSettings settings;
const char* SETTINGS_PATH = "/config/settings.json";
// =================== scary config ====================
// SD card pinout for Waveshare ESP32-S3
#define SD_CLK_PIN 14
#define SD_CMD_PIN 15
#define SD_D0_PIN 16
#define SD_D1_PIN 18
#define SD_D2_PIN 17
#define SD_D3_PIN 21
// ---------------URL Encode -------------
String urlencode(String str) {
  String encoded = "";
  char c;
  char code0, code1;
  char code[] = "0123456789ABCDEF";

  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      code0 = code[(c >> 4) & 0xF];
      code1 = code[c & 0xF];
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}
// Settings Setup:
bool loadSettings() {
  if (!SD_MMC.exists(SETTINGS_PATH)) {
    Serial.println("Settings file not found. Generating default.");
    return saveSettings();  // Save defaults
  }

  File file = SD_MMC.open(SETTINGS_PATH);
  if (!file || file.isDirectory()) {
    Serial.println("Failed to open settings file.");
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Failed to parse settings JSON.");
    return false;
  }

  settings.rgbMode = doc["rgbMode"] | "off";
  settings.rgbColor = doc["rgbColor"] | "#ff0000";
  settings.adminPassword = doc["adminPassword"] | "";
  settings.wifiSSID = doc["wifiSSID"] | "Jcorp_Nomad";
  settings.wifiPassword = doc["wifiPassword"] | "password";
  settings.brightness = doc["brightness"] | 230;
  settings.autoGenerateMedia = doc["autoGenerateMedia"] | false;

  return true;
}
bool saveSettings() {
  SD_MMC.mkdir("/config"); // Ensure directory exists

  File file = SD_MMC.open(SETTINGS_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open settings file for writing.");
    return false;
  }

  StaticJsonDocument<512> doc;
  doc["rgbMode"] = settings.rgbMode;
  doc["rgbColor"] = settings.rgbColor;
  doc["adminPassword"] = settings.adminPassword;
  doc["wifiSSID"] = settings.wifiSSID;
  doc["wifiPassword"] = settings.wifiPassword;
  doc["brightness"] = settings.brightness;
  doc["autoGenerateMedia"] = settings.autoGenerateMedia;

  bool success = serializeJson(doc, file) > 0;
  file.close();
  return success;
}

// --------------- Media Generation Stuff ------------
bool isAlwaysGenerateEnabled() {
    return SD_MMC.exists("/always_generate.flag");
}

void enableAlwaysGenerate() {
    File f = SD_MMC.open("/always_generate.flag", FILE_WRITE);
    if (f) {
        f.print("1");
        f.close();
    }
}

void disableAlwaysGenerate() {
    SD_MMC.remove("/always_generate.flag");
}

bool isOneTimeGenerateRequested() {
    return SD_MMC.exists("/generate_once.flag");
}

void requestOneTimeGenerate() {
    File f = SD_MMC.open("/generate_once.flag", FILE_WRITE);
    if (f) {
        f.print("1");
        f.close();
    }
}

void clearOneTimeGenerate() {
    SD_MMC.remove("/generate_once.flag");
}
bool deleteRecursive(String path) {
  path = sanitizePath(path);
  if (!path.startsWith("/")) path = "/" + path;

  std::vector<String> dirsToRemove;
  std::vector<String> stack;
  stack.push_back(path);

  bool anyFailures = false;

  while (!stack.empty()) {
    String p = stack.back();
    stack.pop_back();

    File entry = SD_MMC.open(p);
    if (!entry) {
      // might be already removed, continue
      continue;
    }

    if (!entry.isDirectory()) {
      entry.close();
      if (!SD_MMC.remove(p)) {
        Serial.println("[DEL] Failed to remove file: " + p);
        anyFailures = true;
      } else {
        // enqueue parent to refresh index
        String parent = "/";
        int s = p.lastIndexOf('/');
        if (s > 0) parent = p.substring(0, s);
        enqueueDir(parent);
      }
      yield();
      continue;
    }

    // It's a directory: list children, push them to stack, then schedule dir for removal
    File child;
    entry.rewindDirectory(); // if supported
    while ((child = entry.openNextFile())) {
      String childPath = p + "/" + child.name();
      // push child for deletion
      stack.push_back(childPath);
      child.close();
      yield();
    }
    entry.close();

    // schedule this dir for removal after children removed
    dirsToRemove.push_back(p);
    yield();
  }

  // remove directories in reverse order (deepest first)
  for (int i = (int)dirsToRemove.size() - 1; i >= 0; --i) {
    String d = dirsToRemove[i];
    if (!SD_MMC.rmdir(d)) {
      Serial.println("[DEL] Failed to rmdir: " + d);
      anyFailures = true;
    } else {
      String parent = "/";
      int s = d.lastIndexOf('/');
      if (s > 0) parent = d.substring(0, s);
      enqueueDir(parent);
    }
    yield();
  }

  return !anyFailures;
}

static String sanitizePath(const String &in) {
  if (in.length() == 0) return "/";
  String s = in;

  // Canonicalize slashes
  s.replace('\\', '/');

  // Trim whitespace
  s.trim();

  // Remove duplicate slashes
  while (s.indexOf("//") >= 0) s.replace("//", "/");

  // Prepend leading slash
  if (!s.startsWith("/")) s = "/" + s;

  // Strip trailing slashes (keep root)
  while (s.length() > 1 && s.endsWith("/")) s.remove(s.length() - 1);

  // Resolve "." and ".."
  std::vector<String> parts;
  int start = 1; // skip leading '/'
  while (start <= s.length()) {
    int slash = s.indexOf('/', start);
    String seg = (slash >= 0) ? s.substring(start, slash)
                              : s.substring(start);
    if (seg.length() > 0 && seg != ".") {
      if (seg == "..") {
        if (!parts.empty()) parts.pop_back();
      } else {
        parts.push_back(seg);
      }
    }
    if (slash < 0) break;
    start = slash + 1;
  }

  String out = "/";
  for (size_t i = 0; i < parts.size(); ++i) {
    out += parts[i];
    if (i + 1 < parts.size()) out += "/";
  }
  if (out.length() == 0) out = "/";
  return out;
}


// ───────────────── SD‑recovery globals ───────────────
volatile bool sdErrorFlag            = false;      
unsigned long sdErrorCooldownUntil   = 0;          

bool tryRecoverSDCard() {                          
    Serial.println("[SD] Attempting recovery…");
    SD_MMC.end();          // unmount
    delay(1000);           // give hardware a breather

    if (!SD_MMC.begin("/sdcard", true, false, SD_FREQ_KHZ, 12)) {
        Serial.println("[SD] Recovery failed.");
        return false;
    }
    Serial.println("[SD] Recovery OK.");
    return true;
}

String rfc3339Now() {
  return "2025-07-12T12:00:00Z";  // Hard-coded UTC timestamp, or it gets mad
}

// Captive portal DNS setup
const byte DNS_PORT = 53;
DNSServer dnsServer;
AsyncWebServer server(80); // Web server on port 80
// ----------------------- System Index (per-dir NDJSON) -----------------------
// New: small, low-memory indexing system using per-directory NDJSON files
// Index directory: "/.system-index/"
// Index filename mapping: "/Movies/Kids" -> "/.system-index/Movies__Kids.index.ndjson"

#include <time.h> // for timestamps used in manifest

// --- Simple FNV-1a 64-bit for a compact rolling directory signature ---
static uint64_t fnv1a64_update(const void *data, size_t len, uint64_t h = 14695981039346656037ULL) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}
static String hashToHex(uint64_t h) {
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return String(buf);
}

// --- Safe index path encoding: replace '/' with "__" and drop leading '/' ---
static String encodeIndexPath(const String &dirPath) {
    String p = dirPath;
    if (!p.startsWith("/")) p = "/" + p;
    // remove trailing slash except for root
    while (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
    String enc = p.substring(1); // drop leading slash
    enc.replace("/", "__");
    if (enc.length() == 0) enc = "_root";
    return String("/.system-index/") + enc + ".index.ndjson";
}
static String encodeTmpPath(const String &dirPath) {
    return encodeIndexPath(dirPath) + ".tmp";
}

// --- Minimal escape for JSON strings (name field) ---
static String jsonEscaped(const String &s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

// --- Small ring queue for directories to index (de-dupes on enqueue) ---
#define INDEX_QUEUE_SIZE 256
static String indexQueueArr[INDEX_QUEUE_SIZE];
static volatile uint16_t indexQueueHead = 0;
static volatile uint16_t indexQueueTail = 0;
static portMUX_TYPE indexQueueMux = portMUX_INITIALIZER_UNLOCKED;

static void enqueueDir(const String &dir) {
    String d = dir;
    if (!d.startsWith("/")) d = "/" + d;
    // normalize: remove trailing slash unless root
    while (d.length() > 1 && d.endsWith("/")) d.remove(d.length() - 1);

    // de-dup linear scan (small overhead; queue small)
    portENTER_CRITICAL(&indexQueueMux);
    for (uint16_t i = indexQueueHead; i != indexQueueTail; i = (i + 1) % INDEX_QUEUE_SIZE) {
        if (indexQueueArr[i] == d) { portEXIT_CRITICAL(&indexQueueMux); return; }
    }
    uint16_t next = (indexQueueTail + 1) % INDEX_QUEUE_SIZE;
    if (next == indexQueueHead) {
        // queue full — drop oldest (policy: remove head)
        indexQueueHead = (indexQueueHead + 1) % INDEX_QUEUE_SIZE;
    }
    indexQueueArr[indexQueueTail] = d;
    indexQueueTail = next;
    portEXIT_CRITICAL(&indexQueueMux);
}

static bool dequeueDir(String &out) {
    portENTER_CRITICAL(&indexQueueMux);
    if (indexQueueHead == indexQueueTail) { portEXIT_CRITICAL(&indexQueueMux); return false; }
    out = indexQueueArr[indexQueueHead];
    indexQueueHead = (indexQueueHead + 1) % INDEX_QUEUE_SIZE;
    portEXIT_CRITICAL(&indexQueueMux);
    return true;
}
// ---------------------- Nested index writer (3-level) ----------------------
// Writes a nested JSON index for directories like /Music or /Shows
// Streaming writer: writes to tmp file then patches header like indexDirectory()
static void writeDirRecursive(const String &path, File &out, int depth, int maxDepth,
                              uint64_t &rollingHash, uint32_t &count) {
  // Open directory
  File d = SD_MMC.open(path);
  if (!d || !d.isDirectory()) {
    if (d) d.close();
    return;
  }

  d.rewindDirectory();

  bool firstChild = true;
  while (true) {
    File e = d.openNextFile();
    if (!e) break;
    String name = String(e.name());

    // comma between siblings
    if (!firstChild) out.print(",");
    firstChild = false;

    if (e.isDirectory()) {
      // Write directory object start
      out.print("{\"n\":\""); out.print(jsonEscaped(name)); out.print("\",\"t\":\"d\"");
      // Update rolling hash (dir marker) and count
      String key = name + String("|d|0|0\n");
      rollingHash = fnv1a64_update((const void*)key.c_str(), key.length(), rollingHash);
      ++count;

      // If depth not exceeded, write children array
      if (depth + 1 <= maxDepth) {
        out.print(",\"children\":[");
        // recurse into child (path + "/" + name)
        String childPath = path;
        if (!childPath.endsWith("/")) childPath += "/";
        childPath += name;
        // recurse (this will write the whole child array inline)
        writeDirRecursive(childPath, out, depth + 1, maxDepth, rollingHash, count);
        out.print("]");
      }
      out.print("}");
    } else {
      // file object
      uint64_t fsz = e.size();
      out.print("{\"n\":\""); out.print(jsonEscaped(name)); out.print("\",\"t\":\"f\",\"sz\":");
      out.print(String(fsz));
      out.print("}");
      // update rolling hash for file
      String key = name + String("|f|") + String(fsz) + String("|0\n");
      rollingHash = fnv1a64_update((const void*)key.c_str(), key.length(), rollingHash);
      ++count;
    }

    e.close();
    // throttle occasionally so tasks stay responsive
    if ((count & 0x3F) == 0) vTaskDelay(pdMS_TO_TICKS(1));
  }
  d.close();
}

// Top-level nested index writer. maxDepth: 2 -> base + 2 = 3 levels (Artist/Album/Track)
// Flattening nested index writer (3-level flattened -> NDJSON compatible with indexDirectory)
// This writes a single NDJSON index where the first line is a header JSON and every
// subsequent line is a JSON entry for a single file/dir: {"n":"name","p":"/full/path","t":"d"|"f","sz":size}
static void indexDirectoryNested(const String &dir, int maxDepth = 2) {
  if (!SD_MMC.exists("/.system-index")) SD_MMC.mkdir("/.system-index");

  String tmpPath = encodeTmpPath(dir);
  String finalPath = encodeIndexPath(dir);

  File tmp = SD_MMC.open(tmpPath, FILE_WRITE);
  if (!tmp) {
    Serial.println("[IndexNested] ERROR: cannot open tmp index file: " + tmpPath);
    return;
  }

  // Write a placeholder header (we'll patch it later with actual sig/count)
  tmp.println("{\"_type\":\"dir\",\"path\":\"" + dir + "\",\"generated\":0,\"sig\":\"\",\"count\":0}");

  // We'll create a flat list of entries by recursively walking up to maxDepth
  uint64_t rollingHash = 14695981039346656037ULL; // FNV-1a seed (same as fnv1a64_update default)
  uint32_t count = 0;

  // Helper recursion: write every entry in flat form
  std::function<void(const String&, int)> walk;
  walk = [&](const String &path, int depth) {
    File d = SD_MMC.open(path);
    if (!d || !d.isDirectory()) {
      if (d) d.close();
      return;
    }
    d.rewindDirectory();

    while (true) {
      File e = d.openNextFile();
      if (!e) break;

      String name = String(e.name());
      // build normalized full path
      String full = path;
      if (!full.endsWith("/")) full += "/";
      full += name;

      if (e.isDirectory()) {
        // Emit directory entry (flat)
        tmp.print("{\"n\":\""); tmp.print(jsonEscaped(name));
        tmp.print("\",\"p\":\""); tmp.print(full);
        tmp.print("\",\"t\":\"d\"}\n");

        // update rolling hash and count (directory marker)
        String key = full + String("|d|0|0\n");
        rollingHash = fnv1a64_update((const void *)key.c_str(), key.length(), rollingHash);
        ++count;

        // Recurse if depth allows
        if (depth + 1 <= maxDepth) {
          walk(full, depth + 1);
        }
      } else {
        // File entry
        uint32_t fsz = (uint32_t)e.size();
        tmp.print("{\"n\":\""); tmp.print(jsonEscaped(name));
        tmp.print("\",\"p\":\""); tmp.print(full);
        tmp.print("\",\"t\":\"f\",\"sz\":"); tmp.print(String(fsz));
        tmp.print("}\n");

        String key = full + String("|f|") + String(fsz) + String("|0\n");
        rollingHash = fnv1a64_update((const void *)key.c_str(), key.length(), rollingHash);
        ++count;
      }

      e.close();

      // throttle: keep system responsive on very large trees
      if ((count & 0x3F) == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }

    d.close();
  };

  // Kick off recursion from the base dir
  walk(dir, 0);

  // Final signature
  String sig = hashToHex(rollingHash);

  // Seek back to beginning and patch header with sig/count and generated timestamp
  tmp.seek(0);
  String header = "{\"_type\":\"dir\",\"path\":\"" + dir + "\",\"generated\":" + String((uint32_t)time(NULL)) + ",\"sig\":\"" + sig + "\",\"count\":" + String(count) + "}\n";
  tmp.print(header);
  tmp.close();

  // Atomically replace final index file
  if (SD_MMC.exists(finalPath)) SD_MMC.remove(finalPath);
  SD_MMC.rename(tmpPath, finalPath);

  // Update the manifest/lookup so other logic can see this directory's sig/count
  updateManifestEntry(dir, sig, count);

  Serial.printf("[Index] Wrote nested-flat index for %s (count=%u sig=%s)\n", dir.c_str(), count, sig.c_str());
}


// Forward declarations
static void indexDirectory(const String &dir);

// --- Index task ---
static void IndexTask(void *pvParameters) {
    (void)pvParameters;
    for (;;) {
        String path;
        if (!dequeueDir(path)) {
            vTaskDelay(pdMS_TO_TICKS(250)); // idle wait when nothing to do
            continue;
        }
        // Small delay to batch multiple enqueues that may follow quickly
        vTaskDelay(pdMS_TO_TICKS(10));
        indexDirectory(path);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// Helper: write/update manifest.json atomically
static void updateManifestEntry(const String &fileName, const String &filePath, uint32_t fileSize) {
    File file = SD_MMC.open("/media.json", FILE_READ);
    DynamicJsonDocument doc(32768);  
    if (file) {
        DeserializationError err = deserializeJson(doc, file);
        file.close();
    }
    if (!doc.containsKey("roots")) {
        doc.createNestedArray("roots");
    }

    JsonArray roots = doc["roots"].as<JsonArray>();

    // add or update entry
    JsonObject entry = roots.createNestedObject();
    entry["name"] = fileName;
    entry["path"] = filePath;
    entry["size"] = fileSize;

    // save back
    file = SD_MMC.open("/media.json", FILE_WRITE);
    if (file) {
        serializeJson(doc, file);
        file.close();
    }
}

// --- indexDirectory: stream index -> tmp -> rename; compute signature and count ---
static void indexDirectory(const String &dirPath) {
    // Ensure index dir exists
    if (!SD_MMC.exists("/.system-index")) SD_MMC.mkdir("/.system-index");

    // Normalize directory path
    String dir = dirPath;
    if (!dir.startsWith("/")) dir = "/" + dir;
    while (dir.length() > 1 && dir.endsWith("/")) dir.remove(dir.length() - 1);

    // --- Special-case three-level nested indexing for Music and Shows ---
    String dirLower = dir;
    dirLower.toLowerCase();
    if (dirLower == "/music" || dirLower == "/shows") {
        Serial.println("[Index] Nested indexing for " + dir);
        indexDirectoryNested(dir, 2);
        return;
    }

    // Try to open the dir on SD
    File d = SD_MMC.open(dir);
    if (!d) {
        // nothing to do, maybe removed — ensure index removed
        String finalPath = encodeIndexPath(dir);
        if (SD_MMC.exists(finalPath)) SD_MMC.remove(finalPath);
        updateManifestEntry(dir, "", 0);
        return;
    }

    String tmpPath = encodeTmpPath(dir);
    String finalPath = encodeIndexPath(dir);

    // Open tmp file for streaming
    File tmp = SD_MMC.open(tmpPath, FILE_WRITE);
    if (!tmp) {
        Serial.println("[Index] ERROR: cannot open tmp index file: " + tmpPath);
        return;
    }

    // Placeholder header line (we'll seek back and rewrite)
    tmp.println("{\"_type\":\"dir\",\"path\":\"" + dir + "\",\"version\":1,\"generated\":0,\"sig\":\"\",\"count\":0}");
    uint64_t rollingHash = 14695981039346656037ULL;
    uint32_t count = 0;

    // Iterate entries — write NDJSON lines
    d.rewindDirectory(); // ensure starting at beginning
    while (true) {
        File e = d.openNextFile();
        if (!e) break;
        yield();

        String name = String(e.name());
        String type = e.isDirectory() ? "d" : "f";
        uint64_t size = e.size();
        uint32_t mtime = 0;
        // If mtime is available elsewhere, fill it. Placeholder 0 if not.
        // (If you have proper mtime via FatFs/SD_MMC, plug it in here.)

        // Compose simple row and write
        String line = "{\"n\":\"" + jsonEscaped(name) + "\",\"t\":\"" + type + "\",\"sz\":" + String(size) + ",\"mt\":" + String(mtime) + "}";
        tmp.println(line);

        // Update rolling hash with name|type|size|mtime (text)
        String key = name + "|" + type + "|" + String(size) + "|" + String(mtime) + "\n";
        rollingHash = fnv1a64_update((const void *)key.c_str(), key.length(), rollingHash);

        ++count;

        // Throttle a little so WiFi remains responsive on big dirs
        if ((count & 0x3F) == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    tmp.flush();

    // Final signature
    String sig = hashToHex(rollingHash);

    // Patch header: seek to beginning and write final header line
    tmp.seek(0);
    String header = "{\"_type\":\"dir\",\"path\":\"" + dir + "\",\"version\":1,\"generated\":" + String((uint32_t)time(NULL)) + ",\"sig\":\"" + sig + "\",\"count\":" + String(count) + "}\n";
    tmp.print(header);
    tmp.close();

    // Atomically replace
    if (SD_MMC.exists(finalPath)) SD_MMC.remove(finalPath);
    SD_MMC.rename(tmpPath, finalPath);

    // Update manifest
    updateManifestEntry(dir, sig, count);

    Serial.printf("[Index] Wrote index for %s (count=%u sig=%s)\n", dir.c_str(), count, sig.c_str());
}

std::map<AsyncWebServerRequest*, File> activeUploads;
int connectedClients = 0;
// LED Mode and Color Helper Wrappers
uint8_t currentLEDMode = 0;  // 0=off, 1=rainbow, 2=solid color
uint8_t solidR = 0, solidG = 0, solidB = 0;
void RGB_SetColor(uint8_t r, uint8_t g, uint8_t b) {
    solidR = r;
    solidG = g;
    solidB = b;
    currentLEDMode = 2; 
    Set_Color(g, r, b);
}
extern lv_obj_t *ui_wifi;
extern lv_obj_t *ui_SDcard;
bool lastWifiStatus = false;
bool lastSDStatus = false;
//Globals for SD scan
unsigned long lastUpdateTime = 0;
unsigned long lastSDScanTime = 0;
const unsigned long SD_SCAN_INTERVAL = 60000; // 60 seconds
uint64_t cachedUsedBytes = 0;
uint64_t cachedTotalBytes = 0;
unsigned long lastScanTime = 0;
// Update the UI with the number of connected users
void updateUI(int userCount) {
    char buffer[10];
    snprintf(buffer, sizeof(buffer), "%d", userCount);
    lv_label_set_text(ui_userlabel, buffer);
}
void updateToggleStatus() {
    bool currentWifiStatus = WiFi.softAPIP();
    if (currentWifiStatus != lastWifiStatus) {
        if (currentWifiStatus) {
            lv_obj_add_state(ui_wifi, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_wifi, LV_STATE_CHECKED);
            Serial.println("[Status] WiFi AP failure detected.");
        }
        lastWifiStatus = currentWifiStatus;
    }

bool currentSDStatus = SD_MMC.cardType() != CARD_NONE;   // ✱✱ NEW / CHANGED ✱✱
    if (currentSDStatus != lastSDStatus) {
        if (currentSDStatus) {
            lv_obj_add_state(ui_SDcard, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_SDcard, LV_STATE_CHECKED);
            Serial.println("[Status] SD card failure detected.");
        }
        lastSDStatus = currentSDStatus;
    }
}

// Stream a chunk of text to save RAM
void opdsWrite(AsyncResponseStream *s, const String &chunk) {
    s->print(chunk);
}
//(OPDS thing)
String xmlEscape(const String &in) {        // we already added this
  String out;
  for (char c : in) {
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      default:   out += c;        break;
    }
  }
  return out;
}

String slugify(const String &in) {          // new helper
  String out;
  for (char c : in) {
    if (isalnum(c))       out += (char)tolower(c);
    else if (c==' ' || c=='_' || c=='-') out += '-';
    // every other char is dropped
  }
  return out;
}

// Get a count of currently connected WiFi clients
void updateClientCount() {
    wifi_sta_list_t wifi_sta_list;
    if (esp_wifi_ap_get_sta_list(&wifi_sta_list) == ESP_OK) {
        connectedClients = wifi_sta_list.num;
        updateUI(connectedClients);
    }
}

// Utility: extract base name (no path, no extension)
String getFileBaseName(const String& name) {
    String base = name.substring(name.lastIndexOf('/') + 1);
    int dotIndex = base.lastIndexOf('.');
    if (dotIndex != -1) base = base.substring(0, dotIndex);
    return base;
}
bool isValidExtension(const String& filename, const std::vector<String>& exts) {
  String nameLower = filename;
  nameLower.toLowerCase();
  for (const auto& ext : exts) {
    if (nameLower.endsWith(ext)) return true;
  }
  return false;
}

void generateMediaJSON() {
    // Backwards-compatible alias: start the new indexing system (non-blocking).
    Serial.println("[MediaGen] generateMediaJSON() now boots per-dir indexer (non-blocking).");
    if (!SD_MMC.exists("/.system-index")) SD_MMC.mkdir("/.system-index");

    // Start index task if not already started
    if (indexTaskHandle == NULL) {
        BaseType_t ok = xTaskCreate(IndexTask, "IndexTask", 8192, NULL, 1, &indexTaskHandle);
        if (ok != pdPASS) {
            Serial.println("[MediaGen] ERROR: failed to start IndexTask");
            indexTaskHandle = NULL;
        } else {
            Serial.println("[MediaGen] IndexTask started.");
        }
    } else {
        Serial.println("[MediaGen] IndexTask already running, skipping create.");
    }

    // Enqueue top-level roots to build initial indexes
    enqueueDir("/");
    enqueueDir("/Movies");
    enqueueDir("/Shows");
    enqueueDir("/Books");
    enqueueDir("/Music");
    enqueueDir("/Gallery");
    enqueueDir("/Files");

    // Write a small manifest if missing
    if (!SD_MMC.exists("/.system-index/manifest.json")) {
        StaticJsonDocument<512> doc;
        doc["version"] = 1;
        doc.createNestedArray("roots");
        File m = SD_MMC.open("/.system-index/manifest.json.tmp", FILE_WRITE);
        if (m) {
            serializeJsonPretty(doc, m);
            m.close();
            SD_MMC.rename("/.system-index/manifest.json.tmp", "/.system-index/manifest.json");
        }
    }
}


String absURL(const String &path) {
    return "http://" + WiFi.softAPIP().toString() + path;
}


void handleOPDSRoot(AsyncWebServerRequest *request) {
    AsyncResponseStream *res = request->beginResponseStream(
        "application/atom+xml;profile=opds-catalog;kind=navigation");

    opdsWrite(res, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                   "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                   "xmlns:opds=\"http://opds-spec.org/2010/catalog\">\n");

    opdsWrite(res, "  <id>urn:uuid:nomad-opds-root</id>\n"
                   "  <title>Nomad OPDS Catalog</title>\n"
                   "  <updated>2025-07-12T12:00:00Z</updated>\n"
                   "  <author><name>Nomad Server</name></author>\n");

    // Add required navigation links
    opdsWrite(res, "  <link rel=\"self\" href=\"" + absURL("/opds/root.xml") + "\" "
                   "type=\"application/atom+xml;profile=opds-catalog;kind=navigation\"/>\n");
    opdsWrite(res, "  <link rel=\"start\" href=\"" + absURL("/opds/root.xml") + "\" "
                   "type=\"application/atom+xml;profile=opds-catalog;kind=navigation\"/>\n");

    opdsWrite(res, "  <entry>\n"
                   "    <title>All Books</title>\n"
                   "    <id>urn:uuid:nomad-opds-books</id>\n"
                   "    <updated>2025-07-12T12:00:00Z</updated>\n"
                   "    <link rel=\"http://opds-spec.org/catalog\" "
                   "type=\"application/atom+xml;profile=opds-catalog;kind=acquisition\" "
                   "href=\"" + absURL("/opds/books.xml") + "\"/>\n"
                   "  </entry>\n");

    opdsWrite(res, "</feed>");
    request->send(res);
}


void handleOPDSBooks(AsyncWebServerRequest *request) {
    Serial.println("[OPDS] === handleOPDSBooks() called ===");
    AsyncResponseStream *res =
        request->beginResponseStream(
            "application/atom+xml;profile=opds-catalog;kind=acquisition");

    opdsWrite(res,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                  "<feed xmlns=\"http://www.w3.org/2005/Atom\" "
                  "xmlns:opds=\"http://opds-spec.org/2010/catalog\">\n");
    opdsWrite(res,
      "  <id>urn:uuid:nomad-opds-books</id>\n"
      "  <title>All Books</title>\n"
      "  <updated>"+rfc3339Now()+"</updated>\n"
      "  <link rel=\"self\"  href=\""+absURL("/opds/books.xml")+"\" "
      "type=\"application/atom+xml;profile=opds-catalog;kind=acquisition\"/>\n"
      "  <link rel=\"start\" href=\""+absURL("/opds/root.xml")+"\" "
      "type=\"application/atom+xml;profile=opds-catalog;kind=navigation\"/>\n");

    File dir = SD_MMC.open("/Books");
    Serial.println("[OPDS] Opened /Books directory");

    if (!dir || !dir.isDirectory()) {
        Serial.println("[OPDS] /Books directory not found or is not a directory!");
    }
    while (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        if (!file) {
            Serial.println("[OPDS] No more files in /Books");
            break;
        }

        Serial.println(String("[OPDS] Found file: ") + file.name());

        if (file.isDirectory()) {
            Serial.println("[OPDS] Skipping directory");
            continue;
        }

        String fn = file.name();
        String fnLower = fn;
        fnLower.toLowerCase();

        if (!(fnLower.endsWith(".epub") || fnLower.endsWith(".pdf"))) {
            Serial.println("[OPDS] Skipping non-ebook file: " + fn);
            continue;
        }

        Serial.println("[OPDS] Processing book file: " + fn);


        String base = fn.substring(fn.lastIndexOf('/')+1);
        base = base.substring(0, base.lastIndexOf('.'));  
        String safeTitle = xmlEscape(base);                 
        String safeId    = "urn:uuid:nomad-book-" + slugify(base);  
        String mime = fn.endsWith(".epub") ?
                      "application/epub+zip" : "application/pdf";

        // Cover (falls back to placeholder)
        String coverPath = SD_MMC.exists("/Books/"+base+".jpg") ?
                           "Books/"+base+".jpg" : "placeholder.jpg";

        // Compute real download path
        String ext    = fnLower.endsWith(".pdf") ? ".pdf" : ".epub";
        String dlPath = "/Books/" + urlencode(base) + ext;

        opdsWrite(res,"  <entry>\n"
                      "    <title>"+safeTitle+"</title>\n"
                      "    <id>"+safeId+"</id>\n"
                      "    <updated>"+rfc3339Now()+"</updated>\n"
                      "    <link rel=\"http://opds-spec.org/image/thumbnail\" "
                      "type=\"image/jpeg\" href=\""+absURL("/"+urlencode(coverPath))+"\"/>\n"
                      "    <link rel=\"http://opds-spec.org/acquisition\" "
                      "type=\""+mime+"\" "
                      "href=\"" + absURL(dlPath) + "\"/>\n"
                      "  </entry>\n");

    }
    if (dir) {
        dir.close();
        Serial.println("[OPDS] Closed /Books directory");
    }


    opdsWrite(res,"</feed>");
    request->send(res);
}

// ==================== MEDIA STREAM HANDLER ====================
// Handles video/audio streaming via range requests
void handleRangeRequest(AsyncWebServerRequest *request) {

    /* ----- validate path ----- */
    if (!request->hasParam("file")) {
        request->send(400, "text/plain", "File parameter missing");
        return;
    }

    String filePath = request->getParam("file")->value();
    if (!SD_MMC.exists(filePath)) {
        request->send(404, "text/plain", "File not found");
        return;
    }

    /* ----- open file with recovery guard ----- */
    File file = SD_MMC.open(filePath, "r");
    if (!file) {                                              // ✱✱ NEW / RECOVERY ✱✱
        Serial.println("[SD] open() failed — trigger recovery");
        sdErrorFlag            = true;                        // flag main loop
        sdErrorCooldownUntil   = millis() + 5000;            // pause 5 s
        request->send(503, "text/plain",
                      "SD error — retrying shortly");        // browser will auto‑retry
        return;
    }

    size_t fileSize = file.size();

    /* ----- debug ----- */
    Serial.println("=== Range Request Debug ===");
    Serial.println("File requested: " + filePath);
    Serial.println("File size: " + String(fileSize));

    /* ----- HEAD support ----- */
    if (request->method() == HTTP_HEAD) {
        Serial.println("HEAD request received");
        AsyncWebServerResponse *headResponse =
            request->beginResponse(200, "application/octet-stream", "");
        headResponse->addHeader("Content-Length", String(fileSize));
        request->send(headResponse);
        file.close();
        return;
    }

    /* ----- parse Range header ----- */
    String rangeHeader = request->header("Range");
    Serial.println("Raw Range header: " + rangeHeader);

    size_t startByte = 0, endByte = fileSize - 1;
    if (rangeHeader.startsWith("bytes=")) {
        int dashIndex = rangeHeader.indexOf('-');
        startByte = rangeHeader.substring(6, dashIndex).toInt();
        if (dashIndex + 1 < rangeHeader.length()) {
            endByte = rangeHeader.substring(dashIndex + 1).toInt();
        }
    }
    if (endByte >= fileSize) endByte = fileSize - 1;
    size_t contentLength = endByte - startByte + 1;

    Serial.println("Computed startByte: " + String(startByte));
    Serial.println("Computed endByte: " + String(endByte));
    Serial.println("Content length: " + String(contentLength));
    Serial.println("============================");

    /* ----- create async response ----- */
    String mimeType = "application/octet-stream"; // fallback

    if (filePath.endsWith(".epub")) mimeType = "application/epub+zip";
    else if (filePath.endsWith(".pdf")) mimeType = "application/pdf";
    else if (filePath.endsWith(".mp3")) mimeType = "audio/mpeg";
    else if (filePath.endsWith(".flac")) mimeType = "audio/flac";
    else if (filePath.endsWith(".wav")) mimeType = "audio/wav";
    else if (filePath.endsWith(".ogg")) mimeType = "audio/ogg";
    else if (filePath.endsWith(".aac")) mimeType = "audio/aac";
    else if (filePath.endsWith(".m4a")) mimeType = "audio/mp4";  // or audio/x-m4a
    else if (filePath.endsWith(".mp4")) mimeType = "video/mp4";
    else if (filePath.endsWith(".webm")) mimeType = "video/webm";
    else if (filePath.endsWith(".m4v")) mimeType = "video/x-m4v";
    else if (filePath.endsWith(".jpg") || filePath.endsWith(".jpeg")) mimeType = "image/jpeg";
    else if (filePath.endsWith(".png")) mimeType = "image/png";
    else if (filePath.endsWith(".cbz")) mimeType = "application/vnd.comicbook+zip";
    else if (filePath.endsWith(".cbr")) mimeType = "application/vnd.comicbook-rar";
    AsyncWebServerResponse *response = request->beginResponse(
        mimeType,         // content type
        contentLength,    // content length
        [&, file, startByte, endByte, contentLength] (uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {
            /* seek & read */
            file.seek(startByte + index);
            size_t bytesToRead = min(maxLen, endByte - (startByte + index) + 1);
            size_t bytesRead = file.read(buffer, bytesToRead);

            /* ----- detect read failure ----- */
            if (bytesRead == 0) {
                Serial.println("[SD] read() failed — recovery");
                file.close();
                sdErrorFlag = true;
                sdErrorCooldownUntil = millis() + 5000;
                return 0;  // aborts this chunk; client retries
            }

            /* close when finished */
            if (index + bytesRead >= contentLength) {
                file.close();
            }
            return bytesRead;
        }
    );


 
    response->addHeader("Accept-Ranges", "bytes");
    response->addHeader("Content-Range", "bytes " + String(startByte) + "-" + String(endByte) + "/" + String(fileSize));
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Pragma", "no-cache");
    response->setCode(206); // Partial Content
    request->send(response);
}
void handleListFiles(AsyncWebServerRequest *request) {
    if (!request->hasParam("dir")) {
        request->send(400, "application/json", "{\"error\":\"Missing 'dir' parameter\"}");
        return;
    }
    String dir = request->getParam("dir")->value();

    if (!SD_MMC.exists(dir)) {
        request->send(404, "application/json", "{\"error\":\"Directory not found\"}");
        return;
    }

    File directory = SD_MMC.open(dir);
    if (!directory || !directory.isDirectory()) {
        request->send(404, "application/json", "{\"error\":\"Not a directory\"}");
        return;
    }

    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    File file = directory.openNextFile();
    while (file) {
        JsonObject f = arr.createNestedObject();

        String filename = String(file.name());
        // Strip dir prefix
        if (filename.startsWith(dir)) {
            filename = filename.substring(dir.length());
            if (filename.startsWith("/")) filename = filename.substring(1);
        }

        if (file.isDirectory()) {
            // Append slash to indicate directory
            f["name"] = filename + "/";
            f["size"] = 0;
            f["isDir"] = true;
        } else {
            f["name"] = filename;
            f["size"] = file.size();
            f["isDir"] = false;
        }
        file = directory.openNextFile();
    }
    directory.close();

    String response;
    serializeJson(arr, response);
    request->send(200, "application/json", response);
}

void handleFileUpload(AsyncWebServerRequest *request) {
    // If the request is just initiating upload (no file data yet)
    if (request->method() == HTTP_POST) {
        if (!request->hasParam("dir", true)) {
            request->send(400, "application/json", "{\"error\":\"Missing 'dir' form field\"}");
            return;
        }
        String dir = sanitizePath(request->getParam("dir", true)->value());

        if (!SD_MMC.exists(dir)) {
            // Try to create subdir if missing
            if (!SD_MMC.mkdir(dir)) {
                request->send(500, "application/json", "{\"error\":\"Failed to create directory\"}");
                return;
            }
        }

        request->send(200, "application/json", "{\"status\":\"Upload handler ready\"}");
    }
}

// AsyncWebServerUpload handler (handles chunks)
void onUpload(AsyncWebServerRequest *request, const String& filename,
              size_t index, uint8_t *data, size_t len, bool final) {
    String dir = "/";
    if (request->hasParam("dir", true)) {
        dir = sanitizePath(request->getParam("dir", true)->value());
    }

    String path = dir + "/" + filename;

    if (index == 0) {
        // First chunk: open file
        Serial.printf("[Upload] Starting: %s\n", path.c_str());

        // Ensure parent directory exists
        int slash = path.lastIndexOf('/');
        if (slash > 0) {
            String parentDir = path.substring(0, slash);
            if (!SD_MMC.exists(parentDir)) {
                SD_MMC.mkdir(parentDir);
            }
        }

        request->_tempFile = SD_MMC.open(path, FILE_WRITE);
        if (!request->_tempFile) {
            Serial.printf("[Upload] Failed to open %s\n", path.c_str());
            return;
        }
    }

    if (len) {
        // Write chunk
        if (request->_tempFile) {
            request->_tempFile.write(data, len);
        }
    }

    if (final) {
        // Last chunk: close file and enqueue parent dir for indexing
        if (request->_tempFile) {
            request->_tempFile.close();
        }

        Serial.printf("[Upload] Completed: %s (%u bytes)\n", path.c_str(), (unsigned int)(index + len));

        String parent = "/";
        int s = path.lastIndexOf('/');
        if (s > 0) parent = path.substring(0, s);

        enqueueDir(parent);  // refresh directory index only once
    }
}

// Helper: basic URL decode (handles %20, %2F etc. — enough for common cases)
static String urlDecode(const String &str) {
  String res;
  res.reserve(str.length());
  char buf[3] = {0,0,0};
  for (size_t i = 0; i < str.length(); ++i) {
    char c = str[i];
    if (c == '%' && i + 2 < str.length()) {
      buf[0] = str[i+1]; buf[1] = str[i+2];
      int val = (int)strtol(buf, NULL, 16);
      res += (char)val;
      i += 2;
    } else if (c == '+') {
      res += ' ';
    } else {
      res += c;
    }
  }
  return res;
}
// Helper: URL-decode (keeps your existing urlDecode if present; reuse it if already defined)
static String urlDecodeSimple(const String &s) {
  String res;
  res.reserve(s.length());
  char a, b;
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '%' && i + 2 < s.length()) {
      a = s[i+1]; b = s[i+2];
      char hex[3] = { a, b, 0 };
      int v = (int)strtol(hex, NULL, 16);
      res += (char)v;
      i += 2;
    } else if (c == '+') {
      res += ' ';
    } else {
      res += c;
    }
  }
  return res;
}

// Core rename performer: normalizes, tries tolerant variants and performs the SD rename.
// Returns true on success and sends appropriate response via request.
static bool doRename(const String &rawOld, const String &rawNew, AsyncWebServerRequest *request) {
  // Parse / decode
  String oldRaw = rawOld;
  String newRaw = rawNew;

  if (oldRaw.indexOf('%') >= 0) oldRaw = urlDecodeSimple(oldRaw);
  if (newRaw.indexOf('%') >= 0) newRaw = urlDecodeSimple(newRaw);

    // Always decode (not conditional) and preserve leading slash
    String oldCandidate = urlDecodeSimple(rawOld);
    String newCandidate = urlDecodeSimple(rawNew);

    // Defensive: ensure path starts with '/' (Nomad requires absolute paths)
    if (!oldCandidate.startsWith("/")) oldCandidate = "/" + oldCandidate;
    if (!newCandidate.startsWith("/")) newCandidate = "/" + newCandidate;

    // Collapse duplicate slashes
    while (oldCandidate.indexOf("//") >= 0) oldCandidate.replace("//", "/");
    while (newCandidate.indexOf("//") >= 0) newCandidate.replace("//", "/");


  // Defensive: collapse duplicate slashes
  while (oldCandidate.indexOf("//") >= 0) oldCandidate.replace("//", "/");
  while (newCandidate.indexOf("//") >= 0) newCandidate.replace("//", "/");

  Serial.printf("[RENAME] Attempting rename: '%s' -> '%s'\n", oldCandidate.c_str(), newCandidate.c_str());

  // Tolerant attempts if source missing: strip duplicate-leading-slashes progressively
  if (!SD_MMC.exists(oldCandidate)) {
    String alt = oldCandidate;
    while (alt.startsWith("//")) alt = alt.substring(1); // remove one leading slash
    if (alt != oldCandidate && SD_MMC.exists(alt)) {
      oldCandidate = alt;
      Serial.printf("[RENAME] Found alt source: '%s'\n", oldCandidate.c_str());
    }
  }

  if (!SD_MMC.exists(oldCandidate)) {
    Serial.printf("[RENAME] Source not found after attempts: '%s'\n", oldCandidate.c_str());
    if (request) request->send(404, "application/json", String("{\"error\":\"Original file not found\",\"tried\":\"") + oldCandidate + "\"}");
    return false;
  }

  // If destination exists, remove it (overwrite behaviour)
  if (SD_MMC.exists(newCandidate)) {
    Serial.println("[RENAME] Target exists, removing: " + newCandidate);
    if (!SD_MMC.remove(newCandidate)) {
      if (request) request->send(500, "application/json", "{\"error\":\"Failed to remove existing destination\"}");
      return false;
    }
  }

  // Ensure destination parent exists
  String parentNew = "/";
  int s2 = newCandidate.lastIndexOf('/');
  if (s2 > 0) parentNew = newCandidate.substring(0, s2);
  if (!SD_MMC.exists(parentNew)) SD_MMC.mkdir(parentNew);

  bool ok = SD_MMC.rename(oldCandidate, newCandidate);
  if (ok) {
    // enqueue parent dirs for indexing
    String parentOld = "/";
    int s = oldCandidate.lastIndexOf('/');
    if (s > 0) parentOld = oldCandidate.substring(0, s);

    enqueueDir(parentOld);
    if (parentNew != parentOld) enqueueDir(parentNew);

    Serial.printf("[RENAME] Success: '%s' -> '%s'\n", oldCandidate.c_str(), newCandidate.c_str());
    if (request) request->send(200, "application/json", "{\"ok\":true}");
    return true;
  } else {
    Serial.printf("[RENAME] Failed to rename '%s' -> '%s'\n", oldCandidate.c_str(), newCandidate.c_str());
    if (request) request->send(500, "application/json", "{\"error\":\"Rename failed\"}");
    return false;
  }
}

void handleRename(AsyncWebServerRequest *request) {
    if (renameBodyBuffer.find(request) == renameBodyBuffer.end()) {
        request->send(400, "text/plain", "Missing rename body");
        return;
    }
    String body = renameBodyBuffer[request];

    renameBodies.erase(request);  // cleanup memory

    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
    }

    String oldPath = doc["old"] | "";
    String newPath = doc["new"] | "";

    if (oldPath.isEmpty() || newPath.isEmpty()) {
        request->send(400, "text/plain", "Missing parameters");
        return;
    }

    Serial.printf("[RENAME] raw old:'%s' raw new:'%s'\n", oldPath.c_str(), newPath.c_str());

    // Ensure leading slash
    if (!oldPath.startsWith("/")) oldPath = "/" + oldPath;
    if (!newPath.startsWith("/")) newPath = "/" + newPath;

    if (SD_MMC.exists(oldPath)) {
        if (SD_MMC.rename(oldPath, newPath)) {
            Serial.printf("[RENAME] Success: '%s' -> '%s'\n", oldPath.c_str(), newPath.c_str());
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "text/plain", "Rename failed");
        }
    } else {
        Serial.printf("[RENAME] Source not found: '%s'\n", oldPath.c_str());
        request->send(404, "text/plain", "Source file not found");
    }
}

void handleRenameBody(AsyncWebServerRequest *request, uint8_t *data, size_t len,
                      size_t index, size_t total) {
    if (index == 0) {
    if (!renameBodyBuffer.count(request)) {
        renameBodyBuffer[request] = "";
    }
    renameBodyBuffer[request].concat((const char*)data, len);

    if (index + len == total) {
        Serial.printf("[RENAME] Full body received (%u bytes)\n", total);
    }
}
                      
void handleRenameEnd(AsyncWebServerRequest *request) {
    if (!renameBodyBuffer.count(request)) {
        request->send(400, "text/plain", "Missing body buffer");
        return;
    }
    String body = renameBodyBuffer[request];
    renameBodyBuffer.erase(request); // cleanup after processing


    DynamicJsonDocument doc(512);
    auto error = deserializeJson(doc, body);
    if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    String oldPath = doc["oldPath"] | "";
    String newPath = doc["newPath"] | "";

    Serial.printf("[RENAME] Attempting rename: '%s' -> '%s'\n", oldPath.c_str(), newPath.c_str());

    if (SD_MMC.rename(oldPath, newPath)) {
        Serial.printf("[RENAME] Success: '%s' -> '%s'\n", oldPath.c_str(), newPath.c_str());
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(500, "application/json", "{\"error\":\"Rename failed\"}");
    }
}

void handleDelete(AsyncWebServerRequest *request) {
    if (!request->hasParam("filename", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing 'filename' parameter\"}");
        return;
    }

    String filename = request->getParam("filename", true)->value();

    if (!SD_MMC.exists(filename)) {
        request->send(404, "application/json", "{\"error\":\"File not found\"}");
        return;
    }

    // Normalize path to absolute form for parent calculation
    String path = filename;
    if (!path.startsWith("/")) path = "/" + path;

    bool success = false;
    File f = SD_MMC.open(path);
    if (f && f.isDirectory()) {
        success = SD_MMC.rmdir(path);
    } else {
        success = SD_MMC.remove(path);
    }

    if (success) {
        request->send(200, "application/json", "{\"status\":\"Delete successful\"}");

        // enqueue the parent directory so the indexer will refresh it
        int slash = path.lastIndexOf('/');
        String parent = "/";
        if (slash > 0) parent = path.substring(0, slash);
        enqueueDir(parent);
    } else {
        request->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    }
}



std::map<AsyncWebServerRequest*, String> uploadPaths;
File uploadFile;

// --- createSimpleUploadHandler with overwrite support and index enqueue ---
void createSimpleUploadHandler(const String& mediaFolder, const char* endpoint) {
    server.on(endpoint, HTTP_POST,
        [](AsyncWebServerRequest *request) {
            request->send(200, "application/json", "{\"status\":\"Upload finished\"}");
        },
        [mediaFolder](AsyncWebServerRequest *request, const String& filename, size_t index,
                      uint8_t *data, size_t len, bool final) {

            static std::map<AsyncWebServerRequest*, File> uploadsLocal;
            static std::map<AsyncWebServerRequest*, String> uploadPathsLocal;

            if (index == 0) {
                String fullPath = "/" + mediaFolder + "/" + filename;
                fullPath = sanitizePath(fullPath);

                Serial.println("[Upload] Starting upload to: " + fullPath);

                // Overwrite flags handling
                bool overwrite = false;
                auto checkFlag = [&](const char *name)->bool {
                    if (!request->hasParam(name, true)) return false;
                    String v = request->getParam(name, true)->value();
                    v.toLowerCase();
                    return (v == "1" || v == "true" || v == "yes");
                };
                if (checkFlag("overwrite") || checkFlag("force") || checkFlag("replace")) overwrite = true;

                if (SD_MMC.exists(fullPath)) {
                    if (!overwrite) {
                        Serial.println("[Upload] Duplicate file (no overwrite): " + fullPath);
                        // Note: cannot reliably call request->send() while inside chunk in some setups, so just abort
                        return;
                    }
                    Serial.println("[Upload] Overwrite requested; removing: " + fullPath);
                    if (!SD_MMC.remove(fullPath)) {
                        Serial.println("[Upload] Failed to remove existing before overwrite: " + fullPath);
                        return;
                    }
                    delay(20); yield();
                }

                // Ensure parent folder exists
                int slashPos = fullPath.lastIndexOf('/');
                if (slashPos != -1) {
                    String folder = fullPath.substring(0, slashPos);
                    if (!SD_MMC.exists(folder)) {
                        SD_MMC.mkdir(folder);
                        delay(10); yield();
                    }
                }

                File f = SD_MMC.open(fullPath, FILE_WRITE);
                if (!f) {
                    Serial.println("[Upload] Failed to open file for writing: " + fullPath);
                    return;
                }
                uploadsLocal[request] = f;
                uploadPathsLocal[request] = fullPath;
            }

            // write chunk
            if (uploadsLocal.count(request)) {
                uploadsLocal[request].write(data, len);
                Serial.printf("[Upload] chunk write %u for %s\n", (unsigned)len, filename.c_str());
            }

            // final chunk
            if (final && uploadsLocal.count(request)) {
                uploadsLocal[request].close();
                String saved = uploadPathsLocal[request];
                uploadsLocal.erase(request);
                uploadPathsLocal.erase(request);

                // Enqueue the folder for indexing
                int slash = saved.lastIndexOf('/');
                String parent = "/";
                if (slash > 0) parent = saved.substring(0, slash);
                enqueueDir(parent);

                Serial.println("[Upload] Completed: " + saved);
            }
        }
    );
}
// --- end createSimpleUploadHandler patch ---

void scanSDCardUsage() {
    cachedUsedBytes = 0;
    cachedTotalBytes = SD_MMC.cardSize();

    std::function<void(File)> sumDirectory = [&](File dir) {
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;

            if (entry.isDirectory()) {
                sumDirectory(entry);
            } else {
                cachedUsedBytes += entry.size();
            }
            entry.close();
        }
    };

    File root = SD_MMC.open("/");
    if (root && root.isDirectory()) {
        sumDirectory(root);
        root.close();
    }

    lastScanTime = millis();
}

void handleSDInfo(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256);

    // Example: Get total and used bytes from SD_MMC or cached values
    uint64_t totalBytes = cachedTotalBytes > 0 ? cachedTotalBytes : (64ULL * 1024 * 1024 * 1024);
    uint64_t usedBytes = cachedUsedBytes; // You should update this periodically

    doc["total"] = totalBytes;
    doc["used"] = usedBytes;

    String output;
    serializeJson(doc, output);

    request->send(200, "application/json", output);
}

void updateSDBAR() {
    uint64_t totalBytes = SD_MMC.totalBytes();
    uint64_t usedBytes = SD_MMC.usedBytes();

  if (totalBytes > 0) {
    int usage = (usedBytes * 100) / totalBytes;
    lv_bar_set_value(ui_sdbar, usage, LV_ANIM_OFF);
  } else {
    lv_bar_set_value(ui_sdbar, 0, LV_ANIM_OFF);
  }
}
bool checkGenerateFlagFile() {
    if (SD_MMC.exists("/.generate_flag")) {
        Serial.println("[BOOT] Found /.generate_flag, will generate media.json");
        SD_MMC.remove("/.generate_flag");
        return true;
    }
    return false;
}
// ----- AFTER -----
void handleConnector(AsyncWebServerRequest *request) {
  // 1) Get 'dir' from POST body
  String dir = "/";
  if (request->hasParam("dir", true)) {
    dir = request->getParam("dir", true)->value();
  }
  if (!dir.endsWith("/")) dir += "/";

  // Try to serve from per-dir index (fast)
  String idxPath = encodeIndexPath(dir);
  if (SD_MMC.exists(idxPath)) {
    File idx = SD_MMC.open(idxPath, "r");
    if (!idx) {
      request->send(500, "text/plain", "Failed to open index");
      return;
    }
    String html = "<ul class=\"jqueryFileTree\" style=\"display: none;\">";
    String line;
    // skip header line (first line in the NDJSON is a header for the dir)
    bool firstLine = true;
    while (idx.available()) {
      line = idx.readStringUntil('\n');
      if (line.length() <= 1) continue;
      if (firstLine) { firstLine = false; continue; } // skip dir header
      StaticJsonDocument<256> jd;
      DeserializationError err = deserializeJson(jd, line);
      if (err) {
        // malformed line: skip
        continue;
      }
      // Expect fields: "n" (name), "t" (type: 'd' or 'f')
      const char *name = jd["n"] | "";
      const char *type = jd["t"] | "f";
      if (String(type) == "d") {
        html += "<li class=\"directory collapsed\">"
             "<a href=\"#\" rel=\"" + dir + String(name) + "/\">" + String(name) + "</a>"
             "</li>";
      } else {
        // attempt to extract extension
        String sname = String(name);
        int dot = sname.lastIndexOf('.');
        String ext = (dot > 0) ? sname.substring(dot + 1) : "";
        html += "<li class=\"file ext_" + ext + "\">"
             "<a href=\"#\" rel=\"" + dir + String(name) + "\">" + String(name) + "</a>"
             "</li>";
      }
    }
    idx.close();
    html += "</ul>";
    request->send(200, "text/html", html);
    return;
  }

  // Fallback to scanning SD directly (original behavior)
  File root = SD_MMC.open(dir);
  if (!root || !root.isDirectory()) {
    request->send(400, "text/plain", "Invalid directory");
    return;
  }

  String html = "<ul class=\"jqueryFileTree\" style=\"display: none;\">";
  root.rewindDirectory();
  File entry;
  while ((entry = root.openNextFile())) {
    String name = entry.name();
    if (entry.isDirectory()) {
      html += "<li class=\"directory collapsed\">"
           "<a href=\"#\" rel=\"" + dir + name + "/\">" + name + "</a>"
           "</li>";
    } else {
      int dot = name.lastIndexOf('.');
      String ext = dot > 0 ? name.substring(dot+1) : "";
      html += "<li class=\"file ext_" + ext + "\">"
           "<a href=\"#\" rel=\"" + dir + name + "\">" + name + "</a>"
           "</li>";
    }
    entry.close();
  }
  html += "</ul>";
  request->send(200, "text/html", html);
}

void handleMkdir(AsyncWebServerRequest *request) {
    if (!request->hasParam("dir", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing 'dir' parameter\"}");
        return;
    }

    String dirPath = request->getParam("dir", true)->value();

    if (dirPath == "/" || dirPath == "") {
        request->send(400, "application/json", "{\"error\":\"Invalid directory path\"}");
        return;
    }

    // Normalize path
    if (!dirPath.startsWith("/")) dirPath = "/" + dirPath;

    if (SD_MMC.exists(dirPath)) {
        request->send(400, "application/json", "{\"error\":\"Directory already exists\"}");
        return;
    }

    if (SD_MMC.mkdir(dirPath)) {
        request->send(200, "application/json", "{\"success\":\"Directory created\"}");
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to create directory\"}");
    }
}
void applyRGBSettings() {
  if (settings.rgbMode == "off") {
    RGB_SetMode(0);  // Off
  } else if (settings.rgbMode == "solid") {
    if (settings.rgbColor.length() == 7 && settings.rgbColor.charAt(0) == '#') {
      // Parse "#RRGGBB"
      char rs[3] = { settings.rgbColor.charAt(1), settings.rgbColor.charAt(2), 0 };
      char gs[3] = { settings.rgbColor.charAt(3), settings.rgbColor.charAt(4), 0 };
      char bs[3] = { settings.rgbColor.charAt(5), settings.rgbColor.charAt(6), 0 };

      solidR = strtol(rs, NULL, 16);
      solidG = strtol(gs, NULL, 16);
      solidB = strtol(bs, NULL, 16);

      RGB_SetMode(2);  // Solid color mode
    } else {
      Serial.println("[RGB] Invalid color format in settings.rgbColor");
    }
  } else if (settings.rgbMode == "rainbow") {
    RGB_SetMode(1);  // Rainbow mode
  }
}
void applyWiFiSettings() {
  Serial.print("Starting WiFi with SSID: ");
  Serial.println(settings.wifiSSID);
  WiFi.softAP(settings.wifiSSID.c_str(), settings.wifiPassword.c_str());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}
// Return number of connected stations on the softAP
int getConnectedUserCount() {
  // WiFi.softAPgetStationNum() returns uint8_t number of clients
  return WiFi.softAPgetStationNum();
}
void sdScanTask(void* pvParameters) {
  scanSDCardUsage();
  lv_timer_handler();
  updateSDBAR();
  vTaskDelete(NULL);
}
// ------------- Main Setup -------------------
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("=== Booting Nomad (debug) ===");
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    if (get_boot_mode() == USB_MODE) {
      clear_boot_mode();    // next boot will go back to MEDIA
      delay(500);
      Serial.println(">>> USB mode: mounting SD & starting MSC");
      LCD_Init();
      Lvgl_Init();
      ui_init();       
      btStop(); //Stops bluetooth (dont need)
      lv_scr_load(ui_Screen1);    
      lv_obj_clear_flag(ui_MediaGen, LV_OBJ_FLAG_HIDDEN);
      lv_textarea_set_text(ui_MediaGen, "USB Mass-Storage Mode");
      lv_timer_handler();
      
      launch_usb_mode();
      return;
    }


    LCD_Init();
    Lvgl_Init();
    ui_init();

    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== ESP32-S3 Captive Portal & SDMMC Server ===");

    // Start WiFi Access Point
    WiFi.softAP(settings.wifiSSID.c_str(), settings.wifiPassword.c_str());
    Serial.println("WiFi AP started...");
  

    // Initialize SD card
    Serial.println("Initializing SD Card...");
    if (!SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN, SD_D1_PIN, SD_D2_PIN, SD_D3_PIN)) {
        Serial.println("ERROR: SDMMC Pin configuration failed!");
        return;
    }

    if (!SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT, 12)) {
        Serial.println("ERROR: SDMMC Card initialization failed.");
        return;
    }
  
    Serial.println("SD Card initialized successfully!");
    Serial.println("Loading Settings...");
    loadSettings();
    Serial.printf("[SETTINGS] autoGenerateMedia = %s\n", settings.autoGenerateMedia ? "true" : "false");
    applyWiFiSettings();
    applyRGBSettings();
    lv_label_set_text(ui_ssidlabel, settings.wifiSSID.c_str());
    Serial.print("settings.brightness = ");
    Serial.println(settings.brightness);
    bool generateOnce = SD_MMC.exists("/generate_once.flag");
    bool shouldGenerate = (settings.autoGenerateMedia || generateOnce);

    // Log state
    Serial.print("[BOOT] autoGenerateMedia (from settings) = ");
    Serial.println(settings.autoGenerateMedia ? "true" : "false");
    Serial.print("[BOOT] generate_once.flag = ");
    Serial.println(generateOnce ? "true" : "false");

    // Remove /boot_done.flag if cold boot or one-time requested
    esp_reset_reason_t resetReason = esp_reset_reason();
    if (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_SW || generateOnce) {
        SD_MMC.remove("/boot_done.flag");
    }

    // Generate if needed -> bootstrap per-dir indexing (non-blocking)
    if (shouldGenerate && !SD_MMC.exists("/boot_done.flag")) {
        Serial.println("[BOOT] Indexing required. Bootstrapping system index...");

        // Ensure index dir exists
        if (!SD_MMC.exists("/.system-index")) SD_MMC.mkdir("/.system-index");

        // Start index background task (low priority, small stack)
        xTaskCreate(IndexTask, "IndexTask", 8192, NULL, 1, NULL);

        // Enqueue top-level roots to be indexed (page builders)
        enqueueDir("/");
        enqueueDir("/Movies");
        enqueueDir("/Shows");
        enqueueDir("/Books");
        enqueueDir("/Music");
        enqueueDir("/Gallery");
        enqueueDir("/Files");

        // Mark boot_done so this bootstrap runs only once unless requested later
        File flagFile = SD_MMC.open("/boot_done.flag", FILE_WRITE);
        if (flagFile) {
            flagFile.println("done");
            flagFile.close();
        }
        Serial.println("[BOOT] System index bootstrap queued. Boot will continue.");

        if (generateOnce) {
            clearOneTimeGenerate();
            Serial.println("[BOOT] Cleared one-time generation trigger.");
        }
    } else {
        Serial.println("[BOOT] Skipping media.json generation.");
    }

Set_Backlight(settings.brightness);  // now using loaded value
    updateSDBAR();
    xTaskCreatePinnedToCore(
      sdScanTask,        // function
      "SDScan",          // name
      8 * 1024,          // stack size
      NULL,              // params
      1,                 // priority (1 = low)
      NULL,              // handle (not needed)
      0                  // run on core 0
    );
    createSimpleUploadHandler("Movies", "/upload-movie");
    createSimpleUploadHandler("Music", "/upload-music");
    createSimpleUploadHandler("Books", "/upload-book");
 

    delay(2000);

    attachInterrupt(BOOT_BUTTON_PIN, [](){
      set_boot_mode(USB_MODE);
      ESP.restart();
    }, FALLING);
    // Start Captive DNS redirection
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    //OPDS Endpoint
    server.on("/opds/root.xml", HTTP_GET, handleOPDSRoot);
    server.on("/opds/books.xml", HTTP_GET, handleOPDSBooks);
    //.m3u playlist endpoint
    server.on("/playlist.m3u", HTTP_GET, [](AsyncWebServerRequest *request){
        String playlist = "#EXTM3U\n";
    server.on("/nomad.m3u", HTTP_GET, [](AsyncWebServerRequest *request) {
        // internally call the same code or just redirect
        request->redirect("/playlist.m3u");
    });

        // === MOVIES ===
        playlist += "# === MOVIES ===\n";
        File movieDir = SD_MMC.open("/Movies");
        if (movieDir && movieDir.isDirectory()) {
            File file = movieDir.openNextFile();
            while (file) {
                String name = file.name();
                if (!file.isDirectory() && (name.endsWith(".mp4") || name.endsWith(".mkv"))) {
                    String fullPath = String("/Movies/") + file.name();
                    playlist += "#EXTINF:-1," + String(file.name()) + "\n";
                    playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
                }
                file = movieDir.openNextFile();
            }
        }

        // === SHOWS ===
        playlist += "# === SHOWS ===\n";
        File showsRoot = SD_MMC.open("/Shows");
        if (showsRoot && showsRoot.isDirectory()) {
            File showFolder = showsRoot.openNextFile();
            while (showFolder) {
                if (showFolder.isDirectory()) {
                    String showFolderName = String(showFolder.name());  // e.g. "GravityFalls"
                    String fullShowPath = "/Shows/" + showFolderName;

                    File episodeDir = SD_MMC.open(fullShowPath);
                    if (episodeDir && episodeDir.isDirectory()) {
                        File ep = episodeDir.openNextFile();
                        while (ep) {
                            String epName = ep.name();
                            if (!ep.isDirectory() && (epName.endsWith(".mp4") || epName.endsWith(".mkv"))) {
                                String fullPath = fullShowPath + "/" + epName;
                                playlist += "#EXTINF:-1," + epName + "\n";
                                playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
                            }
                            ep = episodeDir.openNextFile();
                        }
                    }
                }
                showFolder = showsRoot.openNextFile();
            }
        }

    //.m3u playlist endpoint
    server.on("/playlist.m3u", HTTP_GET, [](AsyncWebServerRequest *request){
        String playlist = "#EXTM3U\n";
    server.on("/nomad.m3u", HTTP_GET, [](AsyncWebServerRequest *request) {
        // internally call the same code or just redirect
        request->redirect("/playlist.m3u");
    });

        // === MOVIES ===
        playlist += "# === MOVIES ===\n";
        File movieDir = SD_MMC.open("/Movies");
        if (movieDir && movieDir.isDirectory()) {
            File file = movieDir.openNextFile();
            while (file) {
                String name = file.name();
                if (!file.isDirectory() && (name.endsWith(".mp4") || name.endsWith(".mkv"))) {
                    String fullPath = String("/Movies/") + file.name();
                    playlist += "#EXTINF:-1," + String(file.name()) + "\n";
                    playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
                }
                file = movieDir.openNextFile();
            }
        }

        // === SHOWS ===
        playlist += "# === SHOWS ===\n";
        File showsRoot = SD_MMC.open("/Shows");
        if (showsRoot && showsRoot.isDirectory()) {
            File showFolder = showsRoot.openNextFile();
            while (showFolder) {
                if (showFolder.isDirectory()) {
                    String showFolderName = String(showFolder.name());  // e.g. "GravityFalls"
                    String fullShowPath = "/Shows/" + showFolderName;

                    File episodeDir = SD_MMC.open(fullShowPath);
                    if (episodeDir && episodeDir.isDirectory()) {
                        File ep = episodeDir.openNextFile();
                        while (ep) {
                            String epName = ep.name();
                            if (!ep.isDirectory() && (epName.endsWith(".mp4") || epName.endsWith(".mkv"))) {
                                String fullPath = fullShowPath + "/" + epName;
                                playlist += "#EXTINF:-1," + epName + "\n";
                                playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
                            }
                            ep = episodeDir.openNextFile();
                        }
                    }
                }
                showFolder = showsRoot.openNextFile();
            }
        }


        // === MUSIC ===
        playlist += "# === MUSIC ===\n";
        File musicDir = SD_MMC.open("/Music");
        if (musicDir && musicDir.isDirectory()) {
            File file = musicDir.openNextFile();
            while (file) {
                String name = file.name();
                if (!file.isDirectory() && name.endsWith(".mp3")) {
                    String fullPath = String("/Music/") + file.name();
                    playlist += "#EXTINF:-1," + String(file.name()) + "\n";
                    playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
                }
                file = musicDir.openNextFile();
            }
        }

        request->send(200, "audio/x-mpegurl", playlist);
    });

    //fAKE dlna dISCOVERY
    server.on("/dlna/device.xml", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad DLNA</modelName>
            <UDN>uuid:ESP32-DLNA-NOMAD</UDN>
          </device>
        </root>
      )rawliteral");
    });
    server.on("/ssdp/device-desc.xml", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad</modelName>
            <modelNumber>1</modelNumber>
            <UDN>uuid:ESP32-DLNA-FAKE-1234</UDN>
          </device>
        </root>
      )rawliteral");
    });
    server.on("/dlna/description.xml", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media</friendlyName>
            <manufacturer>JCorp</manufacturer>
            <modelName>ESP32-Nomad</modelName>
            <UDN>uuid:nomad-dlna-esp32</UDN>
          </device>
        </root>
      )rawliteral");
    });
    server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
        response->addHeader("Application-URL", "http://" + WiFi.softAPIP().toString() + "/dlna/");
        response->addHeader("Location", "/dlna/desc.xml");  // HTTP redirect target
        request->send(response);
    });

        // === MUSIC ===
        playlist += "# === MUSIC ===\n";
        File musicDir = SD_MMC.open("/Music");
        if (musicDir && musicDir.isDirectory()) {
            File file = musicDir.openNextFile();
            while (file) {
                String name = file.name();
                if (!file.isDirectory() && name.endsWith(".mp3")) {
                    String fullPath = String("/Music/") + file.name();
                    playlist += "#EXTINF:-1," + String(file.name()) + "\n";
                    playlist += "http://" + WiFi.softAPIP().toString() + "/media?file=" + urlencode(fullPath) + "\n";
                }
                file = musicDir.openNextFile();
            }
        }

        request->send(200, "audio/x-mpegurl", playlist);
    });

    //fAKE dlna dISCOVERY
    server.on("/dlna/device.xml", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad DLNA</modelName>
            <UDN>uuid:ESP32-DLNA-NOMAD</UDN>
          </device>
        </root>
      )rawliteral");
    });
    server.on("/ssdp/device-desc.xml", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad</modelName>
            <modelNumber>1</modelNumber>
            <UDN>uuid:ESP32-DLNA-FAKE-1234</UDN>
          </device>
        </root>
      )rawliteral");
    });
    server.on("/dlna/description.xml", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>ESP32-Nomad</modelName>
            <UDN>uuid:nomad-dlna-esp32</UDN>
          </device>
        </root>
      )rawliteral");
    });
    server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "");
        response->addHeader("Application-URL", "http://" + WiFi.softAPIP().toString() + "/dlna/");
        response->addHeader("Location", "/dlna/desc.xml");  // HTTP redirect target
        request->send(response);
    });

    
    // Set LED mode: solid (0), rainbow (1), etc.
    server.on("/led/onoff", HTTP_POST, [](AsyncWebServerRequest *request){},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        StaticJsonDocument<64> doc;
        DeserializationError err = deserializeJson(doc, data);
        if (err) {
          request->send(400, "text/plain", "Invalid JSON");
          return;
        }
        bool enabled = doc["enabled"];
        RGB_SetMode(enabled ? 1 : 0);
        request->send(200, "text/plain", "LED toggled");
      }
    );

    // /led/rainbow - Start rainbow loop
    server.on("/led/rainbow", HTTP_POST, [](AsyncWebServerRequest *request) {
      RGB_SetMode(1);  // Rainbow loop
      request->send(200, "text/plain", "Rainbow mode activated");
    });

    // /led/color - Set solid color from JSON { color: "#rrggbb" }
    server.on("/led/color", HTTP_POST, [](AsyncWebServerRequest *request){},
      NULL,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        StaticJsonDocument<64> doc;
        DeserializationError err = deserializeJson(doc, data);
        if (err) {
          request->send(400, "text/plain", "Invalid JSON");
          return;
        }

        const char* hex = doc["color"];
        if (!hex || strlen(hex) != 7 || hex[0] != '#') {
          request->send(400, "text/plain", "Invalid color format");
          return;
        }
        // Parse "#rrggbb"
        char rs[3] = { hex[1], hex[2], 0 };
        char gs[3] = { hex[3], hex[4], 0 };
        char bs[3] = { hex[5], hex[6], 0 };

        uint8_t r = strtol(rs, NULL, 16);
        uint8_t g = strtol(gs, NULL, 16);
        uint8_t b = strtol(bs, NULL, 16);
        RGB_SetColor(r, g, b);
        request->send(200, "text/plain", "Color set");
      }
    );

    server.on("/sdinfo", HTTP_GET, handleSDInfo);
    server.on("/generate-media", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("[ADMIN] /generate-media called");

        if (!SD_MMC.begin()) {
            request->send(500, "text/plain", "SD card not available.");
            Serial.println("[ADMIN] SD card not mounted.");
            return;
        }

        bool generateOnceFlagExists = SD_MMC.exists("/generate_once.flag");

        if (settings.autoGenerateMedia) {
            if (!generateOnceFlagExists) {
                // Always-generate mode: allow one extra reboot-based generation
                File f = SD_MMC.open("/generate_once.flag", FILE_WRITE);
                if (f) {
                    f.print("1");
                    f.close();
                    Serial.println("[ADMIN] Created /generate_once.flag (settings.autoGenerateMedia ON)");
                } else {
                    Serial.println("[ADMIN] Failed to create /generate_once.flag");
                }
            } else {
                Serial.println("[ADMIN] One-time flag already exists. No action taken.");
            }
        } else {
            // Auto-generation disabled: allow one-time override
            File f = SD_MMC.open("/generate_once.flag", FILE_WRITE);
            if (f) {
                f.print("1");
                f.close();
                Serial.println("[ADMIN] Created /generate_once.flag (settings.autoGenerateMedia OFF)");
            } else {
                Serial.println("[ADMIN] Failed to create /generate_once.flag");
            }
        }

        request->send(200, "text/plain", "Flag set. Rebooting to generate media...");
        delay(200);  // Let SD write settle
        ESP.restart();
    });



    // Captive Portal: Redirect all unknown requests
    server.onNotFound([](AsyncWebServerRequest *request) {
        if (request->hasHeader("User-Agent")) {
            String userAgent = request->header("User-Agent");
            Serial.println("User-Agent: " + userAgent);

            if (userAgent.indexOf("iPhone") >= 0 || userAgent.indexOf("iPad") >= 0 || userAgent.indexOf("Macintosh") >= 0) {
                Serial.println("Apple device detected. Serving appleindex.html");
                request->send(SD_MMC, "/appleindex.html", "text/html");
                return;
            }
        }
        Serial.println("Android device. Serving index.html");
        request->send(SD_MMC, "/index.html", "text/html");
    });
    // Captive triggers for Apple & Android devices
    server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Apple captive portal request detected, serving appleindex.html");
        request->send(SD_MMC, "/appleindex.html", "text/html");
    });
    
    server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
        Serial.println("Android/NORMAL captive portal request detected, serving index.html");
        request->send(SD_MMC, "/index.html", "text/html");
    });
    server.on("/dlna/desc.xml", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(200, "text/xml", R"rawliteral(
        <?xml version="1.0"?>
        <root xmlns="urn:schemas-upnp-org:device-1-0">
          <specVersion>
            <major>1</major>
            <minor>0</minor>
          </specVersion>
          <device>
            <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>
            <friendlyName>Nomad Media Server</friendlyName>
            <manufacturer>Jcorp</manufacturer>
            <modelName>Nomad</modelName>
            <UDN>uuid:ESP32-DLNA-FAKE-1234</UDN>
          </device>
        </root>
      )rawliteral");
    });

    server.on("/dlna/contentdir.xml", HTTP_GET, [](AsyncWebServerRequest *request){
      String xml = "<?xml version=\"1.0\"?><ContentDirectory>";
      File root = SD_MMC.open("/Movies");
      if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
          if (!file.isDirectory()) {
            String name = file.name();
            xml += "<item>";
            xml += "<title>" + name + "</title>";
            xml += "<res protocolInfo=\"http-get:*:video/mp4:*\">";
            xml += "http://192.168.4.1/Movies/" + name + "</res>";
            xml += "</item>";
          }
          file = root.openNextFile();
        }
      }
      xml += "</ContentDirectory>";
      request->send(200, "text/xml", xml);
    });
    server.on("/listfiles", HTTP_GET, handleListFiles);
    server.serveStatic("/assets", SD_MMC, "/assets");
    server.serveStatic("/assets/", SD_MMC, "/assets/");
    // Static HTML routes
    server.serveStatic("/movies.html", SD_MMC, "/movies.html");
    server.serveStatic("/music.html", SD_MMC, "/music.html");
    server.serveStatic("/books.html", SD_MMC, "/books.html");
    server.serveStatic("/shows.html", SD_MMC, "/shows.html");
    server.serveStatic("/admin.html", SD_MMC, "/admin.html");
    server.serveStatic("/games.html", SD_MMC, "/games.html");
    server.serveStatic("/maps.html", SD_MMC, "/maps.html");
    server.serveStatic("/menu.html", SD_MMC, "/menu.html");
    server.serveStatic("/movies", SD_MMC, "/movies.html");
    server.serveStatic("/music",  SD_MMC, "/music.html");
    server.serveStatic("/books",  SD_MMC, "/books.html");
    server.serveStatic("/shows",  SD_MMC, "/shows.html");
    server.serveStatic("/admin",  SD_MMC, "/admin.html");
    server.serveStatic("/games",  SD_MMC, "/games.html");
    server.serveStatic("/maps",   SD_MMC, "/maps.html");
    server.serveStatic("/menu",   SD_MMC, "/menu.html");
    server.serveStatic("/gallery",   SD_MMC, "/gallery.html");
    server.serveStatic("/files",   SD_MMC, "/files.html");
    // Serve root directory and default to index.html
    server.serveStatic("/", SD_MMC, "/").setDefaultFile("index.html");
    server.serveStatic("/Gallery", SD_MMC, "/Gallery")
          .setCacheControl("max-age=86400");
    server.serveStatic("/Files", SD_MMC, "/Files")
          .setCacheControl("max-age=86400");
// --- Robust /upload handler (supports overwrite flags and index enqueue) ---
server.on(
  "/upload", HTTP_POST,
  // Final response (we respond inside the chunk handler)
  [](AsyncWebServerRequest *request) {
    // Intentionally empty. Final response is sent in chunk handler to allow immediate error.
  },
  // Upload chunk handler
  [](AsyncWebServerRequest *request, const String &filename, size_t index,
     uint8_t *data, size_t len, bool final) {

    // Per-request state: file handles and path
    static std::map<AsyncWebServerRequest *, File> uploads;
    static std::map<AsyncWebServerRequest *, String> uploadPaths;

    // BEGIN new chunk
    if (index == 0) {
      String dir = "/";
      if (request->hasParam("dir", true)) {
        dir = sanitizePath(request->getParam("dir", true)->value());
      }
      if (!dir.startsWith("/")) dir = "/" + dir;
      if (dir.length() > 1 && dir.endsWith("/")) dir.remove(dir.length() - 1);

      String fullPath = dir + "/" + filename;

      // Check overwrite flags in incoming form (multiple names supported)
      bool overwrite = false;
      auto checkFlag = [&](const char *name)->bool {
        if (!request->hasParam(name, true)) return false;
        String v = request->getParam(name, true)->value();
        v.toLowerCase();
        return (v == "1" || v == "true" || v == "yes");
      };
      if (checkFlag("overwrite") || checkFlag("force") || checkFlag("replace")) {
        overwrite = true;
      }

      // If file exists and no overwrite requested -> 409 conflict
      if (SD_MMC.exists(fullPath)) {
        if (!overwrite) {
          Serial.println("[Upload] Duplicate file detected: " + fullPath);
          request->send(409, "application/json", "{\"error\":\"File already exists\"}");
          return;
        }
        // Overwrite requested: remove existing before opening
        Serial.println("[Upload] Overwrite requested; removing existing: " + fullPath);
        if (!SD_MMC.remove(fullPath)) {
          Serial.println("[Upload] Failed to remove existing file: " + fullPath);
          request->send(500, "application/json", "{\"error\":\"Failed to remove existing file\"}");
          return;
        }
        delay(20); // tiny breather for some SD hardware
        yield();
      }

      // Ensure directory exists
      int slashPos = fullPath.lastIndexOf('/');
      if (slashPos != -1) {
        String folder = fullPath.substring(0, slashPos);
        if (!SD_MMC.exists(folder)) {
          Serial.println("[Upload] Creating folder: " + folder);
          SD_MMC.mkdir(folder);
          delay(10); yield();
        }
      }

      // Open file (truncates / creates)
      File f = SD_MMC.open(fullPath, FILE_WRITE);
      if (!f) {
        Serial.println("[Upload] Failed to open file: " + fullPath);
        request->send(500, "application/json", "{\"error\":\"Failed to open file\"}");
        return;
      }

      uploads[request] = f;
      uploadPaths[request] = fullPath;
      Serial.println("[Upload] Started: " + fullPath);
    }

    // Write chunk
    if (uploads.count(request)) {
      uploads[request].write(data, len);
      Serial.printf("[Upload] Wrote %u bytes for %s\n", (unsigned)len, filename.c_str());
    } else {
      // No file handle for this request - log and abort
      Serial.println("[Upload] No active handle for request - aborting chunk write");
      // nothing to send here (the client will see failure)
    }

    // Finalize
    if (final && uploads.count(request)) {
      uploads[request].close();
      String uploadedPath = uploadPaths[request];
      uploads.erase(request);
      uploadPaths.erase(request);

      // Notify indexer of parent directory so the UI sees the file immediately
      int slash = uploadedPath.lastIndexOf('/');
      String parent = "/";
      if (slash > 0) parent = uploadedPath.substring(0, slash);
      Serial.println("[Upload] Complete: " + uploadedPath + " -> enqueue " + parent);
      enqueueDir(parent);

      request->send(200, "application/json", "{\"status\":\"Upload successful\"}");
    }
  }
);
// --- end /upload handler patch ---

server.on("/list-assets", HTTP_GET, [](AsyncWebServerRequest *request){
  if (!request->hasParam("dir")) {
    request->send(400, "application/json", "{\"error\":\"Missing dir\"}");
    return;
  }

  String dir = request->getParam("dir")->value();
  File d = SD_MMC.open(dir);
  if (!d || !d.isDirectory()) {
    request->send(404, "application/json", "{\"error\":\"Invalid dir\"}");
    return;
  }

  String output = "{\"files\":[";
  bool first = true;
  File f;
  while ((f = d.openNextFile())) {
    if (!first) output += ",";
    output += "\"" + String(f.name()).substring(String(dir).length()) + "\"";
    first = false;
    f.close();
  }
  output += "]}";
  request->send(200, "application/json", output);
});

server.on("/mkdir", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Require POST parameter "dirname"
    if (!request->hasParam("dirname", true)) {
        request->send(400, "text/plain", "Missing 'dirname' parameter");
        return;
    }

    // Get and sanitize input
    String dirName = request->getParam("dirname", true)->value();

    // Ensure it starts with a slash (absolute path)
    if (!dirName.startsWith("/")) {
        dirName = "/" + dirName;
    }

    // Prevent directory traversal (no "../")
    if (dirName.indexOf("..") >= 0) {
        request->send(400, "text/plain", "Invalid directory name");
        return;
    }

    // Check if the path already exists
    if (SD_MMC.exists(dirName)) {
        request->send(409, "text/plain", "Directory already exists");
        return;
    }

    // Attempt to create the directory
    if (SD_MMC.mkdir(dirName)) {
        request->send(200, "text/plain", "OK");

        // Enqueue parent and the new directory for indexing.
        // Parent:
        String parent = dirName;
        int slash = parent.lastIndexOf('/');
        if (slash > 0) parent = parent.substring(0, slash);
        else parent = "/";

        enqueueDir(parent);
        // Enqueue the new dir too (so an empty index is created)
        enqueueDir(dirName);
    } else {
        request->send(500, "text/plain", "Failed to create directory");
    }
});


static std::map<AsyncWebServerRequest*, String> uploadPaths;
server.on("/media", HTTP_GET, handleRangeRequest); // THE MOST IMPORTANT ONE
// Collect bodies for JSON posts (single place, once)
server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
  // We only care about /rename POST with application/json
  if (request->method() != HTTP_POST) return;
  if (request->url() != "/rename") return;

  if (index == 0) g_bodyBuf[request] = "";
  g_bodyBuf[request].reserve(total);
  g_bodyBuf[request].concat((const char*)data, len);

  if (index + len == total) {
    // nothing else here; handler will parse & erase it
  }
});

// The /rename handler accepts either form fields or JSON body
server.on("/rename", HTTP_POST, [](AsyncWebServerRequest *request){
  String oldname, newname;

  // 1) Try form/x-www-form-urlencoded fields first
  if (request->hasParam("oldname", true) && request->hasParam("newname", true)) {
    oldname = request->getParam("oldname", true)->value();
    newname = request->getParam("newname", true)->value();
  } else {
    // 2) Try JSON body from g_bodyBuf
    auto it = g_bodyBuf.find(request);
    if (it != g_bodyBuf.end()) {
      DynamicJsonDocument d(1024);
      DeserializationError err = deserializeJson(d, it->second);
      g_bodyBuf.erase(it);
      if (!err) {
        if (d.containsKey("oldname")) oldname = String(d["oldname"].as<const char*>());
        if (d.containsKey("newname")) newname = String(d["newname"].as<const char*>());
        // Support aliases (src/dst)
        if (oldname.length()==0 && d.containsKey("src")) oldname = String(d["src"].as<const char*>());
        if (newname.length()==0 && d.containsKey("dst")) newname = String(d["dst"].as<const char*>());
      }
    }
  }

  oldname = sanitizePath(oldname);
  newname = sanitizePath(newname);

  if (oldname.length() == 0 || newname.length() == 0) {
    request->send(400, "application/json", "{\"error\":\"Missing oldname/newname\"}");
    return;
  }

  doRename(oldname, newname, request);
});
server.on("/raw", HTTP_GET, [](AsyncWebServerRequest *request){
  if (!request->hasParam("path")) { request->send(400, "text/plain", "Missing path"); return; }
  String path = sanitizePath(request->getParam("path")->value());
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) { request->send(404, "text/plain", "Not found"); return; }
  AsyncWebServerResponse *res = request->beginResponse(f, String("application/octet-stream"));
  res->addHeader("Content-Disposition", "attachment; filename=\"" + String(pathToFileName(path)) + "\"");
  request->send(res);
});

server.on("/delete", HTTP_POST, handleDelete);
server.on("/connector", HTTP_POST,
  [](AsyncWebServerRequest *request){
    handleConnector(request);
  }
);
server.on("/Books/*", HTTP_GET, [](AsyncWebServerRequest *request){
    String path = request->url();  // e.g. /Books/MyBook.epub
    if (!SD_MMC.exists(path)) {
        request->send(404);
        return;
    }
    File file = SD_MMC.open(path, "r");
    if (!file) {
        request->send(500);
        return;
    }

    String mimeType = "application/octet-stream";
    if (path.endsWith(".epub")) mimeType = "application/epub+zip";

    AsyncWebServerResponse *response = request->beginResponse(mimeType, file.size(), [file](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {
        file.seek(index);
        size_t bytesRead = file.read(buffer, maxLen);
        if (bytesRead == 0) {
            file.close();
        }
        return bytesRead;
    });

    if (path.endsWith(".epub")) {
        String filename = path.substring(path.lastIndexOf('/') + 1);
        response->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    }

    request->send(response);
});

server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
  Serial.println("[SAVE] Request received");

  // Check for required POST params
  if (!request->hasParam("filename", true) || !request->hasParam("content", true)) {
    Serial.println("[SAVE] Missing filename or content parameter");
    return request->send(400, "text/plain", "Missing filename or content");
  }

  // Get parameters (POST body)
  const AsyncWebParameter* pFile    = request->getParam("filename", true);
  const AsyncWebParameter* pContent = request->getParam("content", true);

  String path    = pFile->value();    // e.g. "/docs/config.json"
  String content = pContent->value(); // new file text

  Serial.printf("[SAVE] Filename: %s\n", path.c_str());
  Serial.printf("[SAVE] Content length: %d\n", content.length());

  // Open file for writing (overwrite)
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("[SAVE] Failed to open file: %s\n", path.c_str());
    return request->send(500, "text/plain", "Failed to open " + path);
  }

  size_t bytesWritten = f.print(content);
  f.close();

  // notify indexer
  String parent = "/";
  String p = sanitizePath(path);
  int slash = p.lastIndexOf('/');
  if (slash > 0) parent = p.substring(0, slash);
  enqueueDir(parent);

  request->send(200, "text/plain", "OK");


  Serial.printf("[SAVE] Bytes written: %d\n", bytesWritten);

  if (bytesWritten != content.length()) {
    Serial.println("[SAVE] Warning: bytes written does not match content length");
  }

  request->send(200, "text/plain", "OK");
});

server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
  StaticJsonDocument<512> doc;
  doc["rgbMode"] = settings.rgbMode;
  doc["rgbColor"] = settings.rgbColor;
  doc["wifiSSID"] = settings.wifiSSID;
  doc["wifiPassword"] = settings.wifiPassword;
  doc["brightness"] = settings.brightness;
  doc["autoGenerateMedia"] = settings.autoGenerateMedia;


  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
});

// GET current settings
server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request){
  StaticJsonDocument<512> doc;
  doc["rgbMode"] = settings.rgbMode;
  doc["rgbColor"] = settings.rgbColor;
  doc["adminPassword"] = settings.adminPassword; 
  doc["wifiSSID"] = settings.wifiSSID;
  doc["wifiPassword"] = settings.wifiPassword;
  doc["brightness"] = settings.brightness;
  doc["autoGenerateMedia"] = settings.autoGenerateMedia;

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json);
});

// POST to update settings
server.on("/settings", HTTP_POST, [](AsyncWebServerRequest *request){
  if (!request->hasParam("body", true)) {
    request->send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  String body = request->getParam("body", true)->value();
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, body);
  if (error) {
    request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  if (doc.containsKey("rgbMode")) settings.rgbMode = doc["rgbMode"].as<String>();
  if (doc.containsKey("rgbColor")) settings.rgbColor = doc["rgbColor"].as<String>();
  if (doc.containsKey("adminPassword")) settings.adminPassword = doc["adminPassword"].as<String>();
  if (doc.containsKey("wifiSSID")) settings.wifiSSID = doc["wifiSSID"].as<String>();
  if (doc.containsKey("wifiPassword")) settings.wifiPassword = doc["wifiPassword"].as<String>();
  if (doc.containsKey("brightness")) settings.brightness = doc["brightness"].as<int>();
  if (doc.containsKey("autoGenerateMedia")) settings.autoGenerateMedia = doc["autoGenerateMedia"].as<bool>();

  if (saveSettings()) {
    request->send(200, "application/json", "{\"status\":\"updated\"}");
  } else {
    request->send(500, "application/json", "{\"error\":\"Failed to save settings\"}");
  }
});
  server.on("/admin-status", HTTP_GET, [](AsyncWebServerRequest *request){
    // Build JSON
    StaticJsonDocument<256> doc;
    doc["ssid"]         = settings.wifiSSID;
    doc["wifiPassword"] = settings.wifiPassword;
    doc["users"]        = getConnectedUserCount(); 

    String payload;
    serializeJson(doc, payload);
    request->send(200, "application/json", payload);
  });

  // 2) Restart the device
  server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Rebooting...");
    delay(100);             // let the response go out
    ESP.restart();          // trigger a software restart
  });

  server.on("/cpu-temp", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", String("{\"temp\":") + currentTempC + "}");
  });
  server.on("/api/index", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("path")) {
      request->send(400, "application/json", "{\"error\":\"Missing path param\"}");
      return;
    }

    String pathParam = request->getParam("path")->value();
    pathParam = urlDecode(pathParam);
    if (pathParam.length() == 0) pathParam = "/";

    // Normalize: ensure leading slash, collapse //, strip /./, handle .. safely
    pathParam = sanitizePath(pathParam);
    if (!pathParam.startsWith("/")) pathParam = "/" + pathParam;

    // Fast path: exact index file exists? stream it
    String idxPath = encodeIndexPath(pathParam);
    if (SD_MMC.exists(idxPath)) {
      File f = SD_MMC.open(idxPath, FILE_READ);
      if (!f) {
        request->send(500, "application/json", "{\"error\":\"Failed to open index\"}");
        return;
      }
      AsyncWebServerResponse *res = request->beginResponse(f, encodeIndexPath(pathParam), "application/x-ndjson", true);
      request->send(res);
      return;
    }

    // Fallback for subdirectories: we only build flat nested indices at bucket roots (e.g. /Shows, /Music).
    // Determine the bucket for this path (first segment).
    String bucket = "/";
    {
      // Find first segment after the leading slash
      int p = pathParam.indexOf('/', 1);
      if (p < 0) {
        // path is like "/Shows" or "/Music" (exact), we already handled exact-file case above → 404 if missing
        request->send(404, "application/json", "{\"error\":\"Index not found\"}");
        return;
      }
      bucket = pathParam.substring(0, p); // e.g. "/Shows"
    }

    // Only fallback for known nested-flat buckets (extend if needed)
    if (bucket != "/Shows" && bucket != "/Music") {
      request->send(404, "application/json", "{\"error\":\"Index not found\"}");
      return;
    }

    String bucketIdx = encodeIndexPath(bucket);
    if (!SD_MMC.exists(bucketIdx)) {
      request->send(404, "application/json", "{\"error\":\"Index not found\"}");
      return;
    }

    File f = SD_MMC.open(bucketIdx, FILE_READ);
    if (!f) {
      request->send(500, "application/json", "{\"error\":\"Failed to open base index\"}");
      return;
    }

    // We stream-filter: only emit entries whose "p" starts with the requested path (subdir).
    // Format is newline-delimited JSON (NDJSON). We include entries as-is.
    AsyncResponseStream *stream = request->beginResponseStream("application/x-ndjson");

    // Optionally, write a small header line identifying this view (kept minimal to stay compatible).
    // If you don't want any header meta line, comment the next line.
    stream->print("{\"_type\":\"dir\",\"path\":\"");
    // Escape quotes minimally (no quotes expected in pathParam normally).
    stream->print(pathParam);
    stream->println("\"}");

    // Read line by line
    String line;
    line.reserve(512);
    while (f.available()) {
      char c = (char)f.read();
      if (c == '\n') {
        // Process one JSON line
        if (line.length() > 0) {
          // Very small/cheap filter: look for `"p":"<pathParam>`
          // If present at any position and the value starts with pathParam + '/' OR equals pathParam (dir record), emit.
          int pPos = line.indexOf("\"p\":\"");
          if (pPos >= 0) {
            int start = pPos + 5;
            int end = line.indexOf('"', start);
            if (end > start) {
              String pval = line.substring(start, end);
              // NOTE: the index uses full absolute paths (e.g. /Shows/Gravity Falls/Season 1/xxx)
              if (pval == pathParam || pval.startsWith(pathParam + "/")) {
                stream->println(line);
              }
            }
          }
        }
        line = "";
      } else {
        line += c;
        // Safety: cap absurdly long lines to avoid memory blowups
        if (line.length() > 4096) {
          line = "";
        }
      }
    }
    f.close();

    // Flush last line (no trailing newline case)
    if (line.length() > 0) {
      int pPos = line.indexOf("\"p\":\"");
      if (pPos >= 0) {
        int start = pPos + 5;
        int end = line.indexOf('"', start);
        if (end > start) {
          String pval = line.substring(start, end);
          if (pval == pathParam || pval.startsWith(pathParam + "/")) {
            stream->println(line);
          }
        }
      }
    }

    request->send(stream);
  });

  server.on("/enterUsb", HTTP_POST, [](AsyncWebServerRequest *request){
    Serial.println(">>> /enterUsb handler hit");
    request->send(200, "text/plain", "OK: entering USB MSC mode...");
    delay(200);                 
    set_boot_mode(USB_MODE);
    ESP.restart();             
  });


// ─── USB‑mode switch: jump to USB MSC on Boot‑button press ───
attachInterrupt(BOOT_BUTTON_PIN, [](){
  set_boot_mode(USB_MODE);
  esp_restart();            // actually reboot into USB mode
}, FALLING);
// Start the web server
server.begin();
updateToggleStatus(); // Reflect initial WiFi and SD status
Serial.println("Web Server started!");
}

// ==================== MAIN LOOP ====================

void loop() {
    dnsServer.processNextRequest();
    Timer_Loop();

    if (currentLEDMode == 1) {
        RGB_Lamp_Loop(2);
    }
    if (sdErrorFlag) {                                            
        if (millis() > sdErrorCooldownUntil && tryRecoverSDCard()) {
            sdErrorFlag = false;  // we’re back in business
        } else {
            delay(5);  // keep feeding WDT while waiting
            return;    // skip rest of loop until recovery works
        }
    }
    // Update UI toggle status every second
    if (millis() - lastUpdateTime > 1000) {
        updateToggleStatus();
        lastUpdateTime = millis();
    }

    if (millis() - lastTempReading > 6000) {
      currentTempC = temperatureRead();
      lastTempReading = millis();
    }
  
    delay(5); // Prevent watchdog starvation
    updateClientCount();
}


void RGB_SetMode(uint8_t mode) {
    currentLEDMode = mode;

    if (mode == 0) {
        // Turn off LED immediately
        Set_Color(0, 0, 0);
    } else if (mode == 2) {
        Set_Color(solidG, solidR, solidB);
    }
}

