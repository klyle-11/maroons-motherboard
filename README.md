# <div align="center">Jcorp Nomad</div>

<div align="center">
  <img src="NomadCover.png" alt="Jcorp Nomad Offline Media Server" width="800">
</div>

<p align="center"><b>A portable, offline media server powered by the ESP32-S3 in a thumbdrive form factor.</b><br>
Stream movies, music, books, and shows anywhere — no internet required.</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-CC--BY--NC--SA%204.0-blue.svg" alt="License: CC BY-NC-SA 4.0" />
  <img src="https://img.shields.io/badge/platform-ESP32--S3-orange" alt="Platform: ESP32-S3" />
  <img src="https://img.shields.io/badge/status-experimental-lightgrey" alt="Status: Experimental" />
</p>

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
- High customazation potential

ESP32-S3 provides enough performance to handle these requirements efficiently, in a pocket-sized form factor.

---

## Experimental Branch

This branch is now considered stable enough for general use, and will soon be merged with `main`. While still under active development, it serves as a fully functional version of Nomad with several new features and reliability improvements.  

**Important notes:**

- EPUB support works, though formatting is rough.  
- Audiobooks function in MP3 format, with basic resume tracking, but may exhibit some bugs.  
- CBZ support is experimental; files may load VERY slowly and are not really supported yet.  

---

## Key Improvements

1. **Faster and More Reliable Indexing System**  
   - Non-blocking, background indexing for large libraries.  
   - Safe on power loss; partial indexes remain intact.  
   - Supports in-place editing without full reindex in many cases.
   - Auto updates changes when they are made live, frontend also checks for index updates so you don't need to clear cache to see new content.

2. **Resume Functionality**  
   - Movies and Shows track progress.  
   - Items display **Play from Start** and **Resume** options.  
   - Menu shows last three movies and last three shows. On small screens it just shows the most recent of each. 
   - This information is stored on the browser cache, so clearing cache will reset watch progress. 

3. **Dark Mode**  
   - Toggleable across all pages from menu page. 
   - Some minor visual bugs may exist depending on the browser.

4. **Admin Page**  
   - Shutdown, Restart, Flash Mode, Wi-Fi, RGB, brightness, Wifi SSID/Password, Admin Login, and indexing/file managment.
   - Added safe shutdown button, not neccisary to use, but some user prefer it for SD card health. (unmounts SD card and performs a safe power down.)
   - Added a console output that gives real-time system feedback.  
   - Tried to keep it fairly simple so its clear whats going on. 

5. **Stability Improvements**  
   - Fixed frontend NDJSON sync issues.  
   - Crash recovery on large indexes.  
   - Dynamic LCD brightness adjustment without restart.  
   - Fixed a few streaming stability issues. 

6. **Improved Library Support**  
   - Shows and Music Support deeper Directory Structures. 
   - Shows can now have Shows/Showname/Seasonname/Ep1.mp4
   - Music can now have /Music/Artist/Album/Song1.mp3
   - This is configurable, and media files can sit at any level even with directories, this allows for specials or movies within a show to display and play in order

---

## Features

- Admin panel with full device controls  
- File browser: upload, rename, delete, download, inline editing  
- Global search on Menu page with media details  
- Music player with playlist support, shuffle, loop, and downloads  
- Shows page with season folder support and specials  
- Books page with PDF and limited EPUB/audiobook support  
- Gallery page with image browsing and video playback  
- Files page for general-purpose sharing / downloads
- Resume/play progress tracking for Movies and Shows  
- Captive portal for easy access    
- Persistent settings across reboots  
- Mobile-friendly web UI

---

## Hardware Requirements

- **Waveshare ESP32-S3 Dev Board (1.47" LCD version)**  
  [Amazon Link](https://amzn.to/4ktB6oT)  

- **FAT32 microSD card (16–64GB recommended)**  
  [Amazon Link](https://amzn.to/44tM1c4)  

- **SD-Card Extender (optional, but the 3DP case is designed for it)**  
  [Amazon Link](https://amzn.to/45IWIJz)  

- **USB power source**  
- **Optional:** 3D-printed enclosure (STL files included)

---

## Software Requirements

- Arduino IDE
- Fat32Format or similar
- SquareLine Studio (optional, for UI editing)  

---

## Quick Start

1. Flash ESP32-S3 firmware from `/firmware/`.  
2. Format SD card as FAT32 and copy `/SD_Card_Template/` files.  
3. Place media in the appropriate folders: `/Movies`, `/Shows`, `/Books`, `/Music`, `/Gallery`, `/Files`. 
4. Insert SD card and power device via USB.  
5. Connect to default Wi-Fi `Jcorp_Nomad` with password: `password`.  
6. Open browser interface and follow connection guide. 
7. Once you are on the Menu page click the gear icon in the top right. 
8. Scroll to Library Index section and click "Full Scan Now"
9. Watch the Admin Console just below Library Index, the scan can take a few minutes depending on how much media is being scaned, it will let you know when complete.
10. Click "Back" to return to Menu and Enjoy!


---

## Folder Structure

```
/Movies
    /Interstellar.mp4
    /Interstellar.jpg
/Shows
    /The Office
        /S01E01 - Pilot.mp4
        /S01E02 - Diversity Day.mp4
    /The Office.jpg
    
    AND / OR (you can have both styles based on how you layout your folders, its per show entry)
    
    /Gravity Falls
        /Season 1
            /S1E1 - Tourist Trapped.mp4
            /S1E2 - The Legend of the Gobblewonker.mp4
        /Season 2
            /S2E1 - Scary-oke.mp4
            /S2E2 - Into the Bunker.mp4
        /Alex Hirsh Interveiw.mp4 (this is a single episode that will apear next to the season folders)
    /Gravity Falls.jpg
/Books/
    The Martian.pdf
    The Martian.jpg
/Music/
    track01.mp3
    
    AND / OR 
    
/Music/
    /Artist1
        /track01.mp3
        
    AND / OR 
/Music/
    /Artist1
        /track01.mp3
        /Album1
            /track02.mp3
            
    AND / OR
/Music/
    /PersonName
        /Playlist1
            /track01.mp3
        /Playlist2
            /track02.mp3
(You can name the two directory levels whatever you want, and place music at any level so its fully customizable to whatever you want to do)

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

- Video: `.mp4, .mov, .mkv, .webm`  
- Audio: `.mp3, .flac, .wav`  
- Books: `.pdf, .epub, .mp3`
- Images: `.jpg`  

---

## Future Plans

- Offline maps with GPS support  
- Retro game emulation
- Chat page / message board
- Whiteboard / Sketch page thats shared / live and shared between users. 
- Full CBZ support for Comic books

---

## License

[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) — free to remix and share for non-commercial use with attribution.

---

## Credits

Developed by **Jackson Studner (Jcorp Tech)**.  
Inspired by open-source offline media projects. Contributions via PRs are welcome.
