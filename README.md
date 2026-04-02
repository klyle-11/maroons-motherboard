# <div align="center">Jcorp Nomad</div>

<div align="center">
  <img src="NomadCover.png" alt="Jcorp Nomad Offline Media Server" width="800">
</div>

<p align="center"><b>A portable, offline media server powered by the ESP32-S3 in a thumbdrive form factor.</b><br>
Stream movies, music, books, and shows anywhere — no internet required.</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-CC--BY--NC--SA%204.0-blue.svg" alt="License: CC BY-NC-SA 4.0" />
  <img src="https://img.shields.io/badge/platform-ESP32--S3-orange" alt="Platform: ESP32-S3" />
  <img src="https://img.shields.io/badge/status-stable-brightgreen" alt="Status: Stable" />
</p>

---
## LATEST UPDATE

### Major Highlights

#### Plyr Integration
- Replaced native browser video elements with the **Plyr** library  
- Ensures a consistent UI across all devices  
- Fixes playback issues on Apple hardware  

#### Theme Customization
- New **Theme Control Panel** in the Admin Console  
- Expanded to **28 preset themes** (previously 15)  
- Added full **custom theme editor** for manual color adjustments  
- Mobile-optimized with compact layout and touch-friendly controls  

#### Music Overhaul & Queue
- Seamless playback across:
  - Songs
  - Playlists
  - Queue  
- New **Queue system**:
  - Add tracks from any music page  
  - Reorder or remove tracks dynamically  
- Search results now link directly to the queue  

#### Books & Comics Improvements
- **PDF Viewer**
  - Moved to a dedicated page  
  - Supports **progressive loading** for large files  

- **EPUB Reader**
  - Cleaned up and stable    
  - Prevents crashes on low-memory devices  
  - still not recomended as its slower than pdf, but does work. 
  
- **Comic Reader**
  - Supports:
    - Webtoon **infinite scroll**
    - Standard page mode  
  - Works with actual raw CBZ files  

#### Comic Formatting
To use the comic reader:
1. Add a CBZ file as if it where a PDF, will also pick up images. 
2. System can now unzip and pull images on the fly

- Automatically detected as a comic by the system  

#### Search & Performance
- Menu search now uses **cached indices** for much faster results  
- Themes are now loaded from:
  `/.system-theme.json`  
  - Ensures system-wide consistency  

#### Bug Fixes
- Fixed movies page modal transparency issue in dark mode  
- Fixed theme customizer not respecting:
  - Active theme  
  - Dark/light mode toggle  

---

### Default Themes (28)

Default Blue, Forest Night, Cherry Blossom, Mocha Latte, Ocean Depths,  
Autumn Leaves, Lavender Fields, Sunset Horizon, Coral Reef, Mountain Mist,  
Jade Garden, Desert Sand, Arctic Aurora, DeLorean, Midnight Code, 90s Retro,  
Mint Breeze, Rose Gold, Crimson Night, Emerald Dream, Royal Purple,  
Copper Sunset, Sapphire Sea, Peach Cream, Slate Storm, Lime Zest,  
Burgundy Wine, Teal Oasis

---
## What is Nomad

Jcorp Nomad is an open-source offline media server designed for travel, remote work, classrooms, camping, and more. It runs entirely on an ESP32-S3, creates a local Wi-Fi hotspot, and serves media through a browser interface. Multiple users can access separate media streams simultaneously, all without internet access.  

This project is compact, easy to modify, and includes optional 3D-printable hardware. Both firmware and web interface are fully open-source.

---

## Project Inspiration

Inspired by my experience running a Jellyfin server, I wanted a portable, low-cost solution for offline media streaming. Challenges with SBCs (Raspberry Pi, etc.) included high power usage, heat, and instability.  

Nomad focuses on delivering:

- Offline access  
- Wide device compatibility
- Simple frontend for media browsing and playback 
- Multiple user support
- High customization potential

The ESP32-S3 provides enough performance to handle these requirements efficiently, in a pocket-sized form factor.

---

## Stability & Features Update

Nomad is now **stable** on the `main` branch with several new features and improvements.  

**Notable Updates:**

- **EPUB support:** works, though formatting is rough.  
- **Audiobooks:** MP3 format with basic resume tracking; minor bugs may occur.  
- **CBZ support:** experimental; files may load slowly.  

---

## Key Improvements

1. **Faster & More Reliable Indexing**
   - Non-blocking, background indexing for large libraries.
   - Safe on power loss; partial indexes remain intact.
   - Auto-updates changes; frontend detects updates automatically.

2. **Resume Functionality**
   - Movies and Shows track playback progress.
   - Options for **Play from Start** or **Resume**.
   - Menu displays last three movies/shows; mobile shows most recent.

3. **Dark Mode**
   - Toggleable across all pages from the menu.
   - Minor visual bugs may exist depending on browser.

4. **Admin Page**
   - Full device control: shutdown, restart, flash mode, Wi-Fi, RGB LEDs, brightness, credentials, indexing, and file management.
   - Safe shutdown option for SD card health.
   - Real-time system console feedback.

5. **Stability Improvements**
   - Fixed frontend NDJSON sync issues.
   - Crash recovery on large indexes.
   - Dynamic LCD brightness adjustment.
   - Streaming stability enhancements.

6. **Improved Library Support**
   - Supports deeper folder structures for Shows and Music.
   - Flexible organization; media files can be nested at any level.

---

### Features

*   **Admin Panel:** Full device controls, library indexing, Theme Customizer.
*   **File Browser:** Upload, rename, delete, download, and inline file editing. (I recomend doing this on a PC)
*   **Global Search:** Quickly find media across all categories from the Menu page.
*   **Music Player:** Seamless background playback with subdirectory playlists and a dynamic Queue.
*   **Movies & Shows:** Plyr-integrated playback for Movies and Shows with season/special folder support.
*   **Digital Library:** Functional EPUB support, PDF handling, and a dedicated Comic/Webtoon reader.
*   **Resume Tracking:** Saves your playback progress for Movies and Shows.
*   **Gallery & Files:** Dedicated pages for image viewing, video clips, and general file sharing.
*   **Captive Portal:** Automatic login/redirection for easy guest access.
*   **Persistent Settings:** All themes and system configurations remain saved across reboots.
*   **Mobile-Friendly UI:** Fully responsive design optimized for handheld offline streaming.

---

## Hardware Requirements

- **Waveshare ESP32-S3 Dev Board (1.47" LCD version)**  
  [Amazon Link](https://amzn.to/4ktB6oT)  

- **FAT32 microSD card (16–64GB recommended)**  
  [Amazon Link](https://amzn.to/44tM1c4)  

- **SD-Card Extender (optional, 3DP case compatible)**  
  [Amazon Link](https://amzn.to/45IWIJz)  

- **USB power source**  
- **Optional:** 3D-printed enclosure (STL files included)

---

## Software Requirements

- Arduino IDE  
- Fat32Format or equivalent  
- SquareLine Studio (optional, for UI editing)

---

## Quick Start

1. Flash ESP32-S3 firmware from `/firmware/`.  
2. Format SD card as FAT32 and copy `/SD_Card_Template/` files.  
3. Place media in `/Movies`, `/Shows`, `/Books`, `/Music`, `/Gallery`, `/Files`.  
4. Insert SD card and power device via USB.  
5. Connect to Wi-Fi `Jcorp_Nomad` with password: `password`.  
6. Open the browser interface.  
7. Click the gear icon → Library Index → **Full Scan Now**.  
8. Monitor Admin Console for progress; scan may take minutes.  
9. Return to Menu page and enjoy your media!

---
```
## Folder Structure

/Movies
    Interstellar.mp4
    Interstellar.jpg

/Shows
    /The Office
        S01E01 - Pilot.mp4
        S01E02 - Diversity Day.mp4
    The Office.jpg
    
    /Gravity Falls
        /Season 1
            S1E1 - Tourist Trapped.mp4
            S1E2 - The Legend of the Gobblewonker.mp4
        /Season 2
            S2E1 - Scary-oke.mp4
            S2E2 - Into the Bunker.mp4
        Alex Hirsch Interview.mp4
    Gravity Falls.jpg

/Books
    The Martian.pdf
    The Martian.jpg

/Music
    track01.mp3
    /Artist1
        track01.mp3
        /Album1
            track02.mp3
    /PersonName
        /Playlist1
            track01.mp3
        /Playlist2
            track02.mp3

/Gallery
    image01.jpg
    video01.mp4

/Files
    document.pdf
    example.txt

index.html
appleindex.html
menu.html
movies.html
shows.html
books.html
music.html
gallery.html
files.html
Logo.png
favicon.ico
```
---

## Supported Formats

- **Video:** `.mp4, .mov, .mkv, .webm`  
- **Audio:** `.mp3, .flac, .wav`  
- **Books:** `.pdf, .epub, .mp3`  
- **Images:** `.jpg`

---
## 3D Printed Case Files:
-  [Thingiverse](https://www.thingiverse.com/thing:7223398)  
---

## Future Plans

- Offline maps with GPS support  
- Retro game emulation  
- Chat page / message board  
- Collaborative whiteboard / sketch page  
- Full CBZ comic support

---

## License

[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) — free to remix and share for non-commercial use with attribution.

---

## Credits

Developed by **Jackson Studner (Jcorp Tech)**.  
Inspired by open-source offline media projects. Contributions via PRs welcome.
