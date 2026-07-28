# ShelfRemote

*EARLY ACCESS* - ShelfRemote is still in early development and may not work as expected.
If you find a problem, open a GitHub Issue with as much detail as possible.

A remote-friendly (10-foot) Linux client for **Audiobookshelf**, built with
Qt 6 / QML + C++ and libmpv. Unofficial and not affiliated with Audiobookshelf.

See [SUMMARY.md](SUMMARY.md) for the architecture rationale and
[docs/api-contract.md](docs/api-contract.md) for the REST contract.

## Features (MVP)
- Local **and** OIDC/PKCE login (app-brokered, custom-scheme callback)
- Server discovery, multiple saved servers, encrypted token storage
- Home shelves, library grid with sort, on-screen-keyboard search, item details
- Streaming playback via libmpv: multi-file direct play **and** HLS transcode,
  global-timeline seeking, chapters, speed, sleep timer
- Progress sync with correct wall-clock `timeListened` at any speed
- MPRIS2 desktop/media-key control
- Full keyboard/remote focus navigation

## Build — native (fast iteration)

Install dependencies (Fedora):

```bash
sudo dnf install -y \
  cmake ninja-build gcc-c++ pkgconf-pkg-config \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtquickcontrols2-devel \
  mpv-libs-devel sqlite-devel openssl-devel
```

Configure and build:

```bash
cmake -G Ninja -B build -S .
cmake --build build
./build/src/shelfremote
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

## Install (Flatpak, recommended)

Releases are published as a Flatpak repository via GitHub Pages, so you can add
it once and update with `flatpak update`.

```bash
flatpak remote-add --if-not-exists \
  shelfremote https://jw6jw6jw6.github.io/shelfremote/shelfremote.flatpakrepo
flatpak install shelfremote us.jw6.ShelfRemote
flatpak run us.jw6.ShelfRemote
# later
flatpak update
```

The KDE runtime dependency is pulled from Flathub automatically (`RuntimeRepo`), so make
sure Flathub is available: `flatpak remote-add --if-not-exists flathub
https://flathub.org/repo/flathub.flatpakrepo`.

Prefer a one-off file? Every `v*` tag also attaches a single-file
`ShelfRemote-<tag>.flatpak` bundle to the GitHub **Release**:
`flatpak install --user ShelfRemote-v1.2.3.flatpak`.

### Publishing the repo (maintainers)
`.github/workflows/pages.yml` builds the OSTree repo and deploys it to Pages on
every push to `main` and on `v*` tags. One-time setup:

1. **Settings → Pages → Source: "GitHub Actions"**.
2. (Optional, recommended) sign the repo: create a GPG key, then add a repo
   secret `FLATPAK_GPG_PRIVATE_KEY` = `gpg --export-secret-keys KEYID | base64 -w0`.
   When present, CI signs the repo and embeds the public key in the
   `.flatpakrepo`, so clients can add it without `--no-gpg-verify`.

## Build — Flatpak locally

```bash
flatpak install -y flathub org.kde.Platform//6.10 org.kde.Sdk//6.10
flatpak-builder --user --install --force-clean \
  build-flatpak packaging/us.jw6.ShelfRemote.yaml
flatpak run us.jw6.ShelfRemote
```

## OIDC redirect setup
Add `jw6-shelfremote://oauth` to the Audiobookshelf server's **mobile redirect
URI** whitelist (an exact URI is safer than a wildcard). OIDC requires an HTTPS
server URL.

## Notes / roadmap
- Token encryption uses AES-256-GCM with an HKDF-derived key. The master secret
  currently falls back to a locally generated 0600 key; wiring the XDG Secret
  portal is marked `TODO(portal)` in `src/storage/SecureStore.cpp`.
- Deferred (post-MVP): offline downloads, ebooks, admin, Chromecast, podcast
  subscription management, and Socket.IO live updates.
