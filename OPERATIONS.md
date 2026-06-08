# Nomad — Operations & Troubleshooting

Practical procedures for running, recovering, flashing, and debugging the
**mm26-nomad** firmware on the **LILYGO TTGO LoRa32 T3 v1.6.1** (plain ESP32).

- Firmware source: `src/main.cpp`
- PlatformIO env: `ttgo-lora32-v1` (see `platformio.ini`)
- Board controls: **RST button** + **power on/off switch** only — there is **no
  BOOT button** exposed.

This document grows over time — new sections are appended as procedures are
worked out.

---

## Power cycling & flashing (no BOOT button)

This board has **no BOOT button**, only **RST** and an **on/off power switch**.
That matters because the usual "hold BOOT to flash" trick doesn't exist here —
the board relies on the USB-serial chip (CP2104) to drop the ESP32 into download
mode automatically over the DTR/RTS lines.

### A. Plain power cycle (just restart the running firmware)

Use this when the firmware is misbehaving but is otherwise fine — frozen UI,
Wi-Fi in a bad state, or you just want a clean boot.

1. Press and release **RST** once, **or** flip the power switch **OFF**, wait
   ~3 seconds, and back **ON**.
2. The intro animation plays and the firmware starts normally (Nomad mode).

No special timing needed for a normal restart.

### B. Flashing / upload (normal path)

On this board upload should "just work" — esptool toggles DTR/RTS to reset the
ESP32 into download mode for you, then resets it back to run the new firmware:

1. Make sure the **power switch is ON**.
2. **Close any open serial monitor** — a monitor holding the port will block the
   upload.
3. Run the upload (`PlatformIO: Upload`).

### C. Upload fails: `Wrong boot mode detected (0xb)!`

```
A fatal error occurred: Failed to connect to ESP32: Wrong boot mode detected (0xb)!
The chip needs to be in download mode.
```

This means esptool is talking to the chip, but the chip booted your firmware
instead of parking in the bootloader — i.e. a **strapping pin (GPIO0) was at the
wrong level at reset.**

**#1 known cause on this board: the microSD card was inserted.** The SD card
shares the SPI bus, and a seated card can hold a strapping pin (GPIO0/2/12) at
the wrong level during reset, blocking download mode.

**→ Remove the microSD card, then upload. Re-insert it after flashing.**

That was the actual fix here — once the card is out, the normal auto-reset works
and you can keep `upload_speed = 460800`.

If pulling the card doesn't do it, then chase the auto-reset itself:

1. **Close the serial monitor.** An open monitor (or anything holding
   `/dev/cu.usbserial-*`) blocks the upload.
2. **RST-timing trick (no BOOT button needed):** click Upload, **press and hold
   RST**, and **release RST** the instant the log shows `Connecting......`. Retry
   2–3 times — auto-reset on this board is marginal and often catches on a retry.
3. **Lower the upload speed** temporarily to `upload_speed = 115200` for a more
   forgiving connection, then put it back to `460800`.
4. **USB path:** a **passive USB-C↔USB-A adapter** is more reliable than a
   powered dock/hub — many docks don't pass the DTR/RTS reset lines. Use a
   known-good data cable, ideally short.

> **Last resort (hardware):** force download mode by pulling **GPIO0 (IO0) to GND**
> while tapping RST, then releasing — only if auto-reset genuinely never works.

### D. BOOT button in the firmware vs. the physical board

Note `src/main.cpp` still *reads* GPIO0 (`#define BOOT_BUTTON 0`,
`digitalRead(BOOT_BUTTON)`) during the intro animation to switch modes. Since
this board exposes no physical BOOT button, GPIO0 just sits HIGH (internal
pull-up = "not pressed"), so that mode-switch path is effectively inactive
unless you manually jumper IO0 to GND.

Summary:

| Goal | What to do |
|------|-----------|
| Restart firmware | Tap RST, or power switch OFF→ON |
| Normal flash | Switch ON, close monitor, hit Upload |
| Fix `0xb` upload failure | `upload_speed = 115200`, then reset at "Connecting…" |
| Force download mode (last resort) | Jumper IO0→GND, tap RST, release |

---

## Wi-Fi AP not broadcasting after setting a password

**Symptom:** firmware compiles and uploads, other changes take effect, but the
`MM-2026` network stops appearing once a password is set.

**Cause:** the ESP32 SoftAP uses WPA2, which requires the passphrase to be
**8–63 characters**. The AP is started in `src/main.cpp` with:

```cpp
static const char *AP_SSID     = "MM-2026";
static const char *AP_PASSWORD = "pass";        // <-- 4 chars: INVALID
...
WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, AP_MAX_CLIENTS);
//          ssid     password (8-63 chars!)
```

`"pass"` is only **4 characters**, so `softAP()` returns `false` and **no
network is broadcast at all** (not even hidden).

**Fix — pick one:**

1. **Open network** (matches the "one-tap join" intent): pass `NULL` for the
   password —
   ```cpp
   WiFi.softAP(AP_SSID, NULL, 1, 0, AP_MAX_CLIENTS);
   ```
2. **Password-protected:** set `AP_PASSWORD` to **8–63 characters** (avoid spaces
   and unusual symbols while testing).

To confirm the diagnosis, log the result:

```cpp
bool ok = WiFi.softAP(AP_SSID, AP_PASSWORD, 1, 0, AP_MAX_CLIENTS);
Serial.printf("softAP %s (ssid='%s' passlen=%d)\n",
              ok ? "OK" : "FAILED", AP_SSID, (int)strlen(AP_PASSWORD));
```

`FAILED` with `passlen < 8` confirms the password length is the problem.

---

## SD content empty + console error `pdfjsLib is not defined`

**Symptom:** you can connect to the AP and the page loads, but the file listing
is blank, and the browser console shows:

```
pdf.js:1 Uncaught SyntaxError: Unexpected token '<'
Uncaught ReferenceError: pdfjsLib is not defined
```

**Cause:** the page requests `/assets/pdf.js`, but the firmware had **no route
serving `/assets/`** — so the request fell through to the captive-portal
`onNotFound` handler, which returns **HTML**. The browser tried to parse that
HTML as JavaScript (`Unexpected token '<'`), `pdfjsLib` was never defined, and
the line `pdfjsLib.GlobalWorkerOptions.workerSrc = ...` threw at the very top of
the page's `<script>`. That aborted the **entire** script block, so the
`/api/list` loader never ran and the listing rendered blank. **The SD card was
not the problem — the page died before it ever asked for the listing.**

**Fixes (both in `src/main.cpp`):**

1. Serve assets from the SD card so `/assets/pdf.js` resolves:
   ```cpp
   server.serveStatic("/assets/", SD, "/assets/");   // before server.onNotFound(...)
   ```
2. Guard the pdf.js usage so a missing/failed asset degrades gracefully instead
   of blanking the page:
   ```js
   if (window.pdfjsLib) {
     pdfjsLib.GlobalWorkerOptions.workerSrc = "/assets/pdf.worker.js";
   }
   ```

**Also required:** `pdf.js` and `pdf.worker.js` must actually exist on the SD
card under `/assets/`. With fix #2 the listing works even if they're missing —
you just lose PDF thumbnails until the files are present.

**Check SD mounted:** the serial log prints `[sd] ok` or `[sd] FAIL` at boot
(`src/main.cpp`, `initSD()`/`sdReady`). If it says `FAIL`, the card isn't
mounting — reseat it (remember it has to be **removed to flash**, then
**re-inserted** afterward), and `/api/list` returns `503 sd not ready` until it
mounts.
