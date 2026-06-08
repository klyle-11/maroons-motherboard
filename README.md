# mm26-nomad

Offline media captive portal for the **LILYGO TTGO LoRa32 T3 v1.6.1**. Acts as a sister firmware to **stock Meshtastic** — hold the BOOT button to switch between them at boot.

The original Jcorp Nomad source it's spiritually based on lives in [`jcorp-nomad/`](./jcorp-nomad/) for reference; it does **not** run on this board (different SoC, display, and SD controller).

## What it does (Nomad mode)

- Open WiFi AP `NOMAD-AP` (no password, 2 clients).
- Captive portal: every OS pops the "join network" sheet straight into the file browser.
- File browser at `http://192.168.4.1/` serving from the SD card with HTTP range requests, so video/audio seek and resume work.
- 128×64 SSD1306 display:
  - Ham-radio-dial intro animation.
  - Dashboard: italic title, dial-needle client gauge, rotating tagline, AP/clients footer.
  - Idle screensaver: rain alternating with a waving Jamaica flag (monochrome).

## Board layout (T3 v1.6.1)

| Peripheral | Pins |
|---|---|
| SD (SPI)   | CS 13 · MOSI 15 · MISO 2 · SCK 14 |
| OLED (I²C) | SDA 21 · SCL 22 · RST 16 · addr 0x3C |
| LoRa (idle)| CS 18 (held HIGH so the SX1276 stays off the bus) |
| Button     | GPIO 0 (BOOT) |

## Build & flash (PlatformIO)

```
pio run -t upload && pio device monitor
```

## Switching firmware

Stock Meshtastic gets flashed to **OTA slot 0**; this Nomad sketch lives in **OTA slot 1**. On any boot, hold the BOOT button for ~1.5 s — the screen confirms, then reboots into Meshtastic. From Meshtastic side, flash this firmware again with `pio run -t upload` to return.

If OTA slot 0 has no valid Meshtastic image, the switch is refused and the device stays in Nomad mode (with a brief on-screen warning).

## SD card layout

Anything. Files are listed straight from the root of the SD; common layout is `/Movies/`, `/Music/`, `/Books/`. Dotfiles are hidden.
