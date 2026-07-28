# ShelfRemote

*EARLY ACCESS* - ShelfRemote is still in early development and may not work as expected.
If you find a problem, open a GitHub Issue with as much detail as possible.

A remote-friendly (10-foot) Linux client for **Audiobookshelf** intended for Home Theater PCs or similar use. 
ShelfRemote is an unofficial Audiobookshelf client and not affiliated with Audiobookshelf.

## Features
- Local **and** OIDC login
- Server discovery, multiple saved servers, encrypted token storage
- Home shelves, library grid with sort, on-screen-keyboard search, item details
- Streaming playback via libmpv: multi-file direct play **and** HLS transcode,
  global-timeline seeking, chapters, speed, sleep timer
- Progress sync with correct wall-clock `timeListened` at any speed
- MPRIS2 desktop/media-key control
- Full keyboard/remote focus navigation

## Install

Releases are published as a Flatpak repository via GitHub Pages, so you can add
it once and update with `flatpak update`.

```bash
flatpak remote-add --if-not-exists \
  shelfremote https://jw6ventures.github.io/shelfremote/shelfremote.flatpakrepo
flatpak install shelfremote com.jw6ventures.ShelfRemote
flatpak run com.jw6ventures.ShelfRemote
# later
flatpak update
```

The KDE runtime dependency is pulled from Flathub automatically (`RuntimeRepo`), so make
sure Flathub is available: `flatpak remote-add --if-not-exists flathub
https://flathub.org/repo/flathub.flatpakrepo`.

Prefer a one-off file? Every `v*` tag also attaches a single-file
`ShelfRemote-<tag>.flatpak` bundle to the GitHub **Release**:
`flatpak install --user ShelfRemote-v1.2.3.flatpak`.

## Roadmap
- Offline downloads
- Ebook support
- Podcast subscription management
- Socket.IO live updates.
