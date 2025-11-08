# <div align="center">Jcorp Nomad</div>


<div align="center">
  <img src="NomadCover.png" alt="Jcorp Nomad Offline Media Server" width="800">
</div>

<p align="center"><b>A portable, offline media server powered by the ESP32-S3 in a thumbdrive form factor.</b><br>
Stream movies, music, books, and shows anywhere — no internet required.</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-CC--BY--NC--SA%204.0-blue.svg" alt="License: CC BY-NC-SA 4.0" />
  <img src="https://img.shields.io/badge/platform-ESP32--S3-orange" alt="Platform: ESP32-S3" />
  <img src="https://img.shields.io/badge/status-beta-lightgrey" alt="Status: Beta" />
</p>


---

## Experimental Branch — Mk2 Early Release!
---

**Sorry for the delay.** This update took longer than expected while I worked through reliability issues with the new indexing system, it’s a lot of moving parts. There are still improvements planned, but the core features are usable and ready for testing. I plan to have yall mess around with this version and make individual feature updates based on your ideas. This is the version I plan to call "complete" so I will not be making any major overhaul updates for awhile, going to focus on smaller frontend stuff. 

**As a note you MUST re-index from the admin panel as soon as you get this version set up, it wont index automaticly, I may add a check for media.json, but have not done so yet.**

### Highlights

- **New indexing system**
  - Much more reliable for very large libraries and heavy edits.
  - **Non-blocking** indexing: runs in the background without requiring a reboot.
  - Index edits can often be made without reindexing the entire card.
  - Power-loss safety: if the device is unplugged during indexing, items indexed up to that point remain visible (you will need to reindex to finish, but the index won’t be catastrophically broken).
  - **Recommended workflow:** for critical bulk changes I still recommend editing the SD card offline, but the Admin UI now supports robust in-place editing.

- **Menu page overhaul**
  - Global **search bar** on the menu to search & access media from one place.
  - Search results show cover image, title, directory, and file type; clicking opens the file (action depends on file type).
  - **Dark Mode** toggle added, a few visual bugs remain, but it generally works across pages.
  - Plan to add ZIM content into the search bar, Its possible, just haven't been able to get it working. 

- **Shows & Music Pages**
  - **Shows:** supports season folders (e.g. `/ShowName/Season1/Ep1.mp4`) and also episodes placed at the show root so single-season shows and specials display correctly.
  - **Music:** split `music.html` into `music.html` + `playlist.html` for increased reliability. Supports `/Artist/Album/Song.mp3` and music at any folder level. Both pages still have some bugs — please report edge cases. (music is hard for some reason lol)

- **Flash Mode**
  - New **Flash Mode** button in the Admin Panel lets you reflash/update the device without opening the case. In case you couldn't guess I broke another screen. 

- **Admin panel password**
  - Fixed the password overlay and locking behavior so you can lock the system (handy to prevent unintended changes by children or other users). This system is super basic and unsecure, its meant to be a mild deterent. 

- **Frontend re-index checking**
  - The frontend now checks the backend on startup to detect index changes (you may notice a startup lag).
  - Working toward: load the old index first from cache, then update when the background scan completes, not finished yet.

- **Resume function**
  - Movies and Shows track watched progress (typically within ~1 second accuracy).
  - Items present **Play from Start** and **Resume** options.
  - Menu shows the **last three movies** and **last three shows** (on mobile, shows the most recent of each).

- **Books (WIP)**
  - Books page overhaul planned, goal is native **EPUB** and **audiobook** support. Not stable yet; may switch JS libraries to achieve this.
  - I only need Epub.js and the existing range request handler + resume logic for the current scope. But getting foliate or similar to work is a better longterm system. 

- **Case design**
  - Added clip/guard divots to thicken up thin walls. Still experimenting with aesthetics, feedback welcome. (at present I am not too happy with the look)

- **Bug Fixes**
  - Fixed the crashing behavior on large index opening in admin panel.
  - Sometimes the index rescan when opening a tab can cause a crash, I am working on fixing this, but the new recovery system is pretty snappy for most devices. (if you are streaming you likely wont even notice someone crashed it) 
  - Fixed the SD bar not updating / file system size accuracy 

### Known issues / caveats

- Indexing is improved but **not final**, very large libraries may still expose edge cases.
- Menu Dark Mode has a few visual bugs. > (admin panel text editor for example)
- Music / Playlist pages are more reliable but still a bit buggy in places.
- Frontend startup is currently slower while it checks for index updates.
- Books page and full EPUB/audiobook features are not completed. 

---

This branch is for users who want the newest features and can help test stability. If you find bugs or odd behavior, please open issues or PRs — your feedback is how this moves from `experimental` into `main`.

---

### Key Features

- Admin panel with controls for restart, USB mode, Flash Mode, Wi-Fi, RGB, brightness, password/locking, and manual/automatic indexing 
- Frontend-based configuration (no firmware edits required)  
- Full file system browser with upload, rename, delete, download, and inline editing 
- non-blocking background indexing, safe on power-loss, editable without full reindex for many changes, improved reliability for large libraries  
- Global search on the Menu page with cover image, title, directory, and file type results
- Dark Mode
- Music player with playlist support, folder-based playlists, shuffle, loop, downloads, and sorting.
- Shows page with season-folder support plus support for episodes at the show root (single-season shows & specials supported)  
- Books section with PDF support and limited EPUB support (EPUB/audiobook native support planned; WIP)  
- Gallery page for browsing images and video playback  
- Files page for general-purpose file sharing and downloads (FAT32 limits apply)  
- LCD interface with USB mode status and SD card usage display and real-time operation feedback  
- Automatic Frontend re-caching when Index is updated. 
- Resume/watch-progress tracking for Movies and Shows with Play from Start / Resume options and recent items shown on the Menu (last three movies / last three shows)  
- Captive portal for easy user access  
- OPDS support for eBook integration  
- Basic DLNA support for media discovery on supported devices  
- Custom UI built with SquareLine Studio  
- Persistent settings stored across reboots  
- Improved mobile-friendly web UI and performance optimizations for large libraries  
- Reliability and UX improvements: better feedback during long ops, performance tweaks, and various bug fixes


Use this branch if you want the most up to date features, improved stability, and a much more polished experience. While still under active development, this is now the recommended version for most users and I will be pushing it to main after a few days of user testing. 


---


## What is Nomad

Jcorp Nomad is an open-source offline media server built for travel, remote work, classrooms, camping, and more. It runs entirely on an ESP32-S3 dev board, creates a local Wi-Fi hotspot, and serves media through a browser-accessible interface. It does not require internet access and works similarly to in-flight entertainment systems. It also allows multiple users watching seperate media streams at the same time. 

This project is designed to be compact, simple, and easily modifiable. It includes optional 3D-printable hardware and a fully open source firmware and web interface.

---

## Project Inspiration
This project was inspired by my experience running a Jellyfin server at home. I love having full control over my media library, and Jellyfin gave me everything I wanted > streaming movies, shows, books, and music, all from my own hardware. Naturally, I started looking for ways to take that experience on the go.

My first thought was to build a portable server, but I quickly ran into some major hurdles:

- Power-hungry hardware — Even low-power x86 boxes needed a hefty battery setup to stay running for long trips.

- High cost — SBCs like the Raspberry Pi 4, plus power banks, USB storage, and screen interfaces added up quickly.

- Heat and reliability — Trying to cram a full stack of services into a compact case often led to thermal issues and instability when running software meant for server hardware.

That’s when I pivoted.

Instead of replicating a full home media server, I focused on delivering the core experience:

- Offline access

- Local Wi-Fi hotspot

- Simple media browsing and playback

- Support for multiple users

The ESP32-S3 offered just enough performance to handle all of that - with a fraction of the power draw and cost.
The result is Nomad: a minimalist, reliable, and low-cost media server that delivers the essential features of a home streaming setup in a smaller than pocket sized format.

Is it fancy? No.
Does it work? Absolutely.
And it’s open-source, so anyone can expand, improve, and adapt it for their own needs.

---

## Features

- Creates a local Wi-Fi hotspot with captive portal  
- Serves HTML media interface to any connected device  
- Streams content directly from a FAT32-formatted microSD card  
- Supports simultaneous connections (tested with up to 4 video streams)  
- No app or internet connection required  
- Open source firmware and UI  
- Customizable web interface / Features

---


## Hardware Requirements

**Disclaimer:**  
The following links are Amazon affiliate links. Purchasing through these links supports this project at no additional cost to you. Thank you for your support!

- **Waveshare ESP32-S3 Dev Board (1.47" LCD version)**  
  [https://amzn.to/4ktB6oT](https://amzn.to/4ktB6oT)

- **FAT32-formatted microSD card (16 GB minimum recommended, 64 GB preferred)**  
  [https://amzn.to/44tM1c4](https://amzn.to/44tM1c4)

- **SD-Card Extender** Not needed, but useful for the latest case version, lets you get to the SD card without disasembly. 
  [https://amzn.to/45IWIJz](https://amzn.to/45IWIJz)

- **USB power source** (e.g., battery bank or computer)

- **Optional:** 3D-printed enclosure (STL files included in the repository)

---

## Software Requirements

- Arduino IDE or PlatformIO (to flash firmware)
- Python 3.x (to run `media.py`)
- SquareLine Studio (optional, for screen UI editing)

All software used is free and available on Windows, macOS, and Linux.

---

## Quick Start

1. Flash the ESP32-S3 with the firmware in the `/firmware/` directory.
2. Format your SD card as **FAT32** and copy the web files from `/SD_Card_Template/`.
3. Place your media files into the appropriate folders (see structure below).
4. Run `media.py` to generate `media.json` automatically.
5. Insert the SD card and power the device.
6. Connect to the Wi-Fi network named `NomadServer`.
7. Your browser will be redirected to the offline media interface.

---

## Folder Structure (on SD Card)

```
/Movies/
    Interstellar.mp4
    Interstellar.jpg
/Shows/
    The Office/
        S01E01 - Pilot.mp4
        S01E02 - Diversity Day.mp4
    The Office.jpg
/Books/
    The Martian.pdf
    The Martian.jpg
/Music/
    track01.mp3

index.html
appleindex.html
menu.html
movies.html
shows.html
books.html
music.html
media.py
media.json
placeholder.jpg
Logo.png
favicon.ico
```
---

## Supported Formats

- Video: `.mp4`
- Audio: `.mp3`
- Books: `.pdf`
- Images: `.jpg` (used for covers and folder images only)

Ensure all images and media files use matching names for proper display.

---

## Customization

- Wi-Fi name and password can be changed in `firmware/Nomad.ino`
- Branding (logo, favicon) can be replaced in `/SD_Card_Template/`
- Sections (Movies, Music, etc.) can be removed from `menu.html`
- Web interface can be edited using any text editor
- The screen UI can be edited with SquareLine Studio (the free version is fine) 

---

## What's Included

- `/firmware/` – Arduino firmware for ESP32-S3
- `/SD_Card_Template/` – Web UI, HTML files, and `media.py`
- `/case/` – STL files for optional 3D-printed enclosure
- `/docs/` – Logos, screenshots, and design files

---

## Troubleshooting

Common issues and their solutions are detailed in the Troubleshooting section on instructibles.

Topics covered include:

- SD card read errors
- Captive portal issues on some phones
- Video playback and seeking bugs
- UI glitches or screen failure

---

## Future Plans

These are features I'd like to explore in future updates. If you'd like to contribute, feel free to fork the repo or open a pull request!


### Offline Maps with GPS Support
Inspired by [Backcountry Beacon](https://www.instructables.com/USB-Powered-Offline-Map-Server/), the goal is to serve cached map tiles and display the live GPS position from the user's phone or connected device — entirely offline.

### HTML5 Games
Similar to [Gams Offline](https://github.com/Gams-Offline/Gams), I would like to embed simple HTML5 games that work in-browser. The selection in Gams is amazing and make me think of cool math games, would be neat to have even though most require a keyboard.

### Audiobook-Friendly Mode
Improve playback for long-form audio by adding bookmarks, chapter display, and smart pause/resume. Intended for better audiobook handling in the Music section. Potentialy add an entire new audiobook section or a seperate handling system for it in books (Under read it could have a listen option).

---

## Build Guide on Instructables

Looking for a step-by-step tutorial?  
Check out the full build guide on **Instructables** for detailed instructions, photos, and tips on setting up Jcorp Nomad.

👉 [Read the Instructables Guide](https://www.instructables.com/Jcorp-Nomad-Mini-WIFI-Media-Server/) 

---

## License

This project is licensed under the [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License](https://creativecommons.org/licenses/by-nc-sa/4.0/).

You may remix, adapt, and share this project **for non-commercial use**, as long as you give credit and share under the same terms.

For commercial licensing, please contact the author.


---

## Credits

Developed by **Jackson Studner (Jcorp Tech)**.  
Inspired by open-source offline projects like Backcountry Beacon.

If you build, remix, or improve this project, please consider submitting a pull request or tagging the project.

---


 
