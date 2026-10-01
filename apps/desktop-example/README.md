# Card Scanner for OBS Studio

Point a camera at a trading card and OBS names it on your stream. The plugin
recognizes cards from Magic: The Gathering, Lorcana, Pokémon, One Piece, Flesh
and Blood, and a dozen other games, and draws an overlay with the card's name,
set, and art.

This is the desktop consumer of `desktop-card-scanner`. The package holds the
scanner; this app supplies the models, the databases, a `main()`, and the web UI.

## How it works

The plugin is two processes. A thin OBS module taps frames and puts the scan
region on a socket. A separate `card-scanner-server` process owns the models,
the card database, and the state machine.

```
OBS process                            card-scanner-server
┌────────────────────────┐             ┌─────────────────────────────┐
│ filter_video tap       │  ROI bytes  │ FrameServer :27846          │
│ region crop (memcpy)   │ ──:27846──► │  └ ScannerService (worker)  │
│ depth-1 mailbox        │             │    └ ScanSession            │
│ socket writer thread   │ ◄─result─── │ ControlServer :27845 ───────┼──► overlay
└────────────────────────┘             │ OverlayServer :27847        │    and dock
                                       └─────────────────────────────┘
```

The module links **libobs and nothing else**. No OpenCV, no ONNX Runtime, no
ObjectBox runs inside OBS, so a crash in the machine learning stack cannot take
a live broadcast down with it. The module starts the server when OBS loads it
and drops frames rather than waiting when the server falls behind.

## Requirements

- OBS Studio 32.x. The libobs interface changes between major versions, and a
  module built against different headers fails when OBS loads it.
- CMake 3.14 or later.
- OpenCV 4.x. Version 5 is not compatible. On macOS, run
  `brew install opencv@4`.
- Node.js and Yarn, to build the overlay.
- Git LFS, which carries the models and the databases. Run `git lfs install`
  once, then `git lfs pull` in your clone.
- macOS on Apple silicon is the tested platform. Windows support is written but
  has not been compiled or run on Windows yet.

## Install

Build the overlay first, then the plugin. The install target copies everything
into a single bundle.

```sh
cd web && yarn install && yarn build && cd ..
cmake -S . -B build -DBUILD_OBS_MODULE=ON
cmake --build build --target install-obs-plugin
```

The bundle lands at
`~/Library/Application Support/obs-studio/plugins/obs-card-scanner.plugin` and
contains the module, the server, the models, the databases, and the overlay
page. There is no second process to start and no paths to configure.

To rebuild after a change, run the same three commands. OBS loads plugins at
startup, so restart OBS to pick up a new build.

## Use it

1. In OBS, select your camera source and click **Filters**.
2. Click **+** and choose **Card Scanner**.
3. In the filter's settings, click **Add overlay to current scene**.

The button creates a Browser Source named **Card Scanner Overlay**, sized to
your canvas, and puts it at the top of the scene. Clicking it again does
nothing; in another scene it adds the same source rather than a second one.
Hold a card in front of the camera, inside the scan region, and it appears on
the overlay.

**Show scan region** starts on so you can aim the camera. The region is drawn
into the video, so turn it off before you go live.

### Filter settings

| Setting | Default | What it does |
|---|---|---|
| **Enabled** | on | Turns scanning off without removing the filter. |
| **Scan rate (FPS)** | 30 | Caps how many frames per second reach the server. Lower it to give the rest of your scene more CPU. |
| **Show scan region** | on | Draws the region into the video. For aiming, not for broadcast. |
| **Region X / Y / width / height** | `0.25`, `0.20`, `0.50`, `0.60` | The part of the frame that gets scanned, as fractions of the full frame. The default is centered, half the width and 60% of the height. |
| **Overlay URL** | `http://127.0.0.1:27847` | The page the Browser Source loads. |
| **Dock URL** | `http://127.0.0.1:27847/?view=dock` | The page to paste into a custom browser dock. |

The overlay takes two query parameters of its own. Add `?debug=0` to hide the
status readout, which reports what the scanner is doing when no card is on
screen: not connected, no frames, nothing detected, or detected but scoring too
low to accept. Turn it off for a real broadcast. Add `?port=N` when the control
server is not on its default port.

Keep the region a little wider than the card. The segmentation model uses the
scene around a card to find it, and a card that fills the whole region gets no
detections at all.

### The dock

The dock gives you the controls the overlay does not have: manual mode, a scan
history, and a live accept threshold. OBS exposes only Qt widget docks to
plugins, so it does not install itself.

To add it, go to **Docks** > **Custom Browser Docks**, name it `Card Scanner`,
and paste the **Dock URL** from the filter settings.

In auto mode, a confirmed card goes to the overlay on its own. In manual mode
the scanner still recognizes cards but waits for you to click **Emit**, which
is what you want when you are talking over a pack opening. Click any card in
the history to put it back on stream.

## Without a camera

The server replays a directory of images as if they were camera frames, which
is enough to check the models, the databases, and the overlay:

```sh
./build/card-scanner-server config.json \
    --replay ../mobile-example/assets/games_images/lorcana
```

Open `http://127.0.0.1:27847` in a browser to watch the overlay, and
`http://127.0.0.1:27847/?view=dock` for the dock.

To scan a single still image and print the result, use the headless harness:

```sh
./build/desktop_card_scanner config.json path/to/card.jpg
```

It exits `0` when it identifies the card, `2` when it scans but identifies
nothing, and `1` on an error.

## Configuration

`config.json` sits beside the executables in the bundle and holds the model
paths, the database directory, and the scanner thresholds. Relative directories
resolve against the config file's own location, so the config and its data move
together.

The models and the databases ship through Git LFS, so `git lfs pull` gets you a
working set. They live under `assets/` in the layout the config expects:

```
assets/
├── models/
│   ├── CardSegmentationModel.onnx
│   ├── CardRecognitionModel.onnx
│   └── CardRecognitionModel.onnx.data
└── databases/
    ├── mtg/data.mdb
    ├── lorcana/data.mdb
    └── <game>/data.mdb
```

To swap in your own, keep that layout. A model with external weights needs its
`.onnx.data` sidecar in the same directory as the `.onnx`, or ONNX Runtime
fails to load it. LMDB's `lock.mdb` and the `cache/` directory are runtime
state and stay out of Git.

`gameClassMapping` maps the segmentation model's class ids to the databases
each one searches. Its entry count decodes the model's prediction tensor, so it
must be contiguous from `0` and must match the class order the model was
exported with. See the package README for what happens when it does not.

## Server options

The module starts the server for you and passes these itself. You need them
only when you run the server by hand.

```
card-scanner-server <config.json> [options]
  --token N          shared secret the module presents on the frame socket
  --frame-port N     default 27846
  --control-port N   default 27845
  --overlay-port N   default 27847
  --overlay <path>   overlay page to serve, default overlay.html beside the config
  --replay <dir>     pump images from a directory instead of waiting for OBS
```

## Control protocol

The overlay and the dock both connect to `ws://127.0.0.1:27845`. The server
broadcasts the whole state on every change, so a client that reloads is current
from its first message:

```json
{
  "type": "state",
  "payload": {
    "status": "emitted",
    "mode": "auto",
    "candidate": null,
    "emitted": { "cardId": "...", "game": "mtg", "score": 0.83, "name": "..." },
    "scan": { "live": true, "detections": 1, "topScore": 0.83, "ms": 15.4 },
    "history": [],
    "settings": { "acceptScore": 0.6, "stableDetections": 2 }
  }
}
```

Card names, sets, and art arrive from a product lookup that runs on its own
thread. A card appears with its id first and fills in the rest when the lookup
returns, so the scanner keeps running when the network does not.

Clients send commands on the same socket:

| Command | Fields | Effect |
|---|---|---|
| `set_mode` | `mode`: `auto` or `manual` | Switches emit behavior. |
| `emit_current` | — | Puts the current candidate on stream. Manual mode. |
| `clear_emitted` | — | Takes the current card off stream. |
| `emit_from_history` | `cardId` | Puts a previously scanned card back on stream. |
| `set_settings` | `acceptScore`, `stableDetections`, `gracePeriodMs`, `emittedTimeoutMs` | Changes thresholds without a restart. Every field is optional. |

## Troubleshooting

**OBS reports that the plugin failed to load.** OBS logged why it skipped the
module. Open the most recent log under
`~/Library/Application Support/obs-studio/logs/` and search for
`obs-card-scanner`. Two causes account for most of these:

- `symbol not found in flat namespace`. The module reached a symbol that lives
  in the desktop library, which it does not link. On macOS the linker defers
  those to load time, so the build stays green. The build now fails instead:
  `cmake --build build` checks the module for undefined `cardscanner` symbols
  and names any it finds. Rebuild and reinstall.
- A version mismatch between the headers the module was built against and the
  installed OBS. Set `OBS_VERSION` to your OBS version and rebuild.

**The overlay stays blank.** The Browser Source needs the server running, which
means the filter has to exist on a source. The server writes its own log beside
OBS's plugin configuration, at
`~/Library/Application Support/obs-studio/plugin_config/obs-card-scanner/card-scanner-server.log`.
The module logs that path when it starts the server, so search OBS's log for
`server starting` if you cannot find it. The server prints `ready` and its three
ports once the models load, which takes a few seconds.

**Cards are not recognized.** With **Show scan region** on, confirm the card
sits inside the region with room to spare. Then read the overlay's status
readout, which reports the detection count and the best match score even when
nothing crosses the accept threshold. No detections points at framing;
detections that score low point at the database not holding that card.

**The overlay sits behind the video.** OBS adds a new source at the bottom of
the scene. The **Add overlay to current scene** button moves it to the top, but
a source you added by hand needs moving yourself.

**Scanning feels slow.** The state message carries the per-frame scan time in
`scan.ms`. If that number is low and the overlay still lags, lower
**Scan rate (FPS)**: the cost is in moving frames, not in the scan.

**A stale server holds the ports.** OBS does not call `obs_module_unload` when
it quits, so the server watches its parent process and exits when OBS goes
away. If one survives anyway, `pkill card-scanner-server` clears it.

## Layout

```
apps/desktop-example/
├── CMakeLists.txt         # builds both executables and assembles the plugin
├── config.json            # models, databases, thresholds
├── assets/                # models and databases, through Git LFS
├── src/
│   ├── main.cpp           # headless harness: scan one image, print the result
│   └── server_main.cpp    # card-scanner-server: parse args, run, wait
└── web/                   # overlay and dock, one bundle, chosen by ?view=dock
```

The overlay and the dock share one React bundle that Vite inlines into a single
`index.html`, because a Browser Source loads one file.
