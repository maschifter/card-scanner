# CARD-NEXUS

A card scanner with machine-learning card recognition for trading card games,
including Magic: The Gathering, Lorcana, Flesh and Blood, and Pokémon. It runs
on phones through React Native and on desktops as an OBS Studio plugin.

## What is in the repository

One recognition pipeline in C++, wrapped for each platform:

| Path | What it is |
|---|---|
| `packages/card-scanner-core` | The pipeline: segmentation, dewarping, embedding, and search. Platform-agnostic. |
| `packages/mobile-card-scanner` | The React Native wrapper. Links ExecuTorch for inference. |
| `packages/desktop-card-scanner` | The desktop wrapper and the OBS bindings. Links ONNX Runtime. |
| `apps/mobile-example` | The Expo app that scans through a phone camera. |
| `apps/desktop-example` | The OBS plugin and the scanner server it talks to. |

Core declares `loadSession` and leaves it undefined. Each platform links
exactly one definition, which is how one pipeline runs on two inference
runtimes without either one leaking into the other.

## Documentation

- [API reference](docs/API.md) — the React Native surface
- [Pipeline architecture](docs/PIPELINE.md) — how a frame becomes a card id
- [Mobile package](packages/mobile-card-scanner/README.md) — ObjectBox
  integration and native details
- [Desktop package](packages/desktop-card-scanner/README.md) — the ONNX
  backend, the OBS bindings, and the process split
- [OBS plugin](apps/desktop-example/README.md) — install it and use it

## Mobile

Supported platforms are iOS 15.1 and later, and Android 10 (API 29) and later.

### Prerequisites

- Node.js 20.19.4 or later
- Yarn 4.1.1
- Git LFS
- CMake 3.18 or later, which generates the native code
- Xcode, for iOS
- Android Studio, for Android

Install CMake and Git LFS for your platform:

```sh
# macOS
brew install cmake git-lfs

# Debian or Ubuntu
sudo apt-get install cmake git-lfs
```

On Windows, download [CMake](https://cmake.org/download/) and
[Git LFS](https://git-lfs.github.com/).

### Set up

```sh
git lfs install
git lfs pull
yarn
```

If you cloned before installing Git LFS, `git lfs pull` fetches the files that
the clone left as pointers.

### Run the example app

```sh
cd apps/mobile-example
yarn expo run:ios       # or: yarn expo run:android
```

To start the development server on its own, run `yarn expo start`.

## Desktop

The desktop build is an OBS Studio plugin: point a camera at a card and OBS
names it on your stream.

The models and the databases come from Git LFS, so run `git lfs pull` first if
you have not already.

```sh
cd apps/desktop-example && node tools/release.mjs
```

That fetches and compiles every dependency, builds the overlay and the plugin,
and installs the bundle into OBS. The
[OBS plugin README](apps/desktop-example/README.md#install) has the options.

Restart OBS, add the **Card Scanner** filter to your camera source, and click
**Add overlay to current scene**. The
[OBS plugin README](apps/desktop-example/README.md) covers the settings, the
dock, the control protocol, and troubleshooting.

Both macOS on Apple silicon and Windows 11 (DirectML) build and run the plugin.
macOS is where most of the testing happens; the files under
`apps/desktop-example/benchmark-summary/` record what each platform was measured on.
