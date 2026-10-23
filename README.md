# CARD-NEXUS

A card scanner with machine-learning card recognition for trading card games,
including Magic: The Gathering, Lorcana, Flesh and Blood, Pokémon, Yu-Gi-Oh!,
and a dozen others. The games are whatever the detector's classes and the CDN
manifest's databases cover. It runs on phones through React Native, on
desktops in C++ (shown as an OBS Studio plugin), and in the browser on WebGPU.

## What is in the repository

One recognition pipeline in C++, wrapped for each platform:

| Path | What it is |
|---|---|
| `packages/card-scanner-core` | The pipeline: segmentation, dewarping, embedding, and search. Platform-agnostic. |
| `packages/mobile-card-scanner` | The React Native wrapper. Links ExecuTorch for inference. |
| `packages/desktop-card-scanner` | The desktop wrapper and the OBS bindings. Links ONNX Runtime. |
| `packages/web-card-scanner` | The browser port of the pipeline on WebGPU (ruNNtime). |
| `apps/mobile-example` | Example: an Expo app that scans through a phone camera. |
| `apps/desktop-example` | Example: an OBS plugin and the scanner server it talks to. |
| `apps/web-example` | Example: a React client and a reference search backend. |

The SDKs are the `packages/`. The `apps/` only show how to call the SDK
APIs: they aren't product code, and you don't need to copy their structure.
To add a scanner to your own app, follow the integration guide in the SDK's
README:

- React Native: [Integrate the SDK into your app](packages/mobile-card-scanner/README.md#integrate-the-sdk-into-your-app)
- Desktop C++: [Integrate the package into your app](packages/desktop-card-scanner/README.md#integrate-the-package-into-your-app)
- Web: [Integrate the package into your app](packages/web-card-scanner/README.md#integrate-the-package-into-your-app)

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
- [Web package](packages/web-card-scanner/README.md) — the browser library on
  WebGPU
- [Web example](apps/web-example/README.md) — run the browser example and its
  backend

## Mobile

Supported platforms are iOS 17 and later (the CoreML models need it), and
Android 10 (API 29) and later.

### Prerequisites

- Node.js 20.19.4 or later
- Yarn 4.1.1
- Git LFS
- CMake 3.14 or later, which generates the native code
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

The app bundles the models and the databases the CDN manifest serves: Metro
runs `tools/manifest.mjs`, which downloads them into
`apps/mobile-example/manifest-assets/`. A model the manifest has no usable
version of keeps its repo copy; the tool prints which came from where. To keep
the repo copies, set `USE_MANIFEST=0` for either command, for example
`USE_MANIFEST=0 yarn expo run:ios`.

## Desktop

The desktop build is an OBS Studio plugin: point a camera at a card and OBS
names it on your stream.

The models and the databases come from Git LFS, so run `git lfs pull` first if
you have not already; the build then takes the CDN manifest's unless
`--no-manifest` (see the
[OBS plugin README](apps/desktop-example/README.md#configuration)).

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
