# ESP32 Generative Art Synthesizer — VJ Platform

A plug-and-play **VJ (visual jockey) platform** built on an ESP32. Power it via USB, connect any device to its WiFi, and you have a full generative art engine with real-time parameter control — no internet, no setup, no cloud.

**Hardware needed:** one ESP32, one rotary encoder, one USB cable.

---

## Why This Exists

This project demonstrates how a $5 ESP32 microcontroller can:

- **Serve as a standalone web server** that beams its own WiFi
- **Run real-time generative art** (Canvas 2D) streamed to any browser
- **Accept physical input** from a single rotary encoder (rotate + push)
- **Push UI updates** over WebSocket — zero polling, zero latency
- **Work completely offline** — in a desert, on a mountain, at a festival

It's an **open base** designed to be tweaked, expanded, or repurposed. Swap the art engine. Add MIDI control. Turn it into a data visualizer. The architecture is modular — the `web/index.html` is the only file you need to edit to change what's drawn on screen.

---

## Quick Start

### 1. Hardware Wiring

| Encoder Pin | ESP32 GPIO | Notes |
|-------------|------------|-------|
| CLK | 18 | Channel A (quadrature) |
| DT | 19 | Channel B (quadrature) |
| SW | 21 | Push button (pull-up, no ISR) |
| VCC | 3.3V | **Must be connected** — internal pulls won't work without it |
| GND | GND | Common ground |

Straight to the ESP32 pins — no level shifters, no external pull-ups, no breadboard components except the encoder itself.

### 2. Build & Flash

```bash
# Clone (if you haven't)
git clone <repo-url> && cd esp32_gen_art

# Set up ESP-IDF (one time)

# Option A: ESP-IDF v5.x/v6.x
export IDF_PATH=/path/to/esp-idf
source $IDF_PATH/export.sh

# Option B: using espressif's managed IDF (recommended)
source ~/.espressif/v6.0.1/esp-idf/export.sh

# Build
idf.py build

# Flash (replace port with yours)
idf.py -p /dev/cu.usbserial-1410 flash

# Monitor (optional)
idf.py -p /dev/cu.usbserial-1410 monitor
```

### 3. Connect & Control

1. Power the ESP32 via USB (or any 5V source)
2. On your phone/laptop, join WiFi **`GenArt`** (password: `genart00`)
3. Open `http://192.168.4.1` — the art loads instantly
4. Rotate the encoder to browse animations, press to tweak parameters

That's it. No app install, no account, no internet.

---

## User Experience Walkthrough

### The Three Modes

```
ANIM_SELECT  ──[press]──►  NAVIGATE  ──[press]──►  EDIT
    │                          │  ▲                    │
    │◄──[press "← Back"]───────┘  │                    │
    │◄─────[double-press]──────────┘                    │
    │◄─────────────────────[press or double-press]──────┘
```

| Your Action | What Happens |
|-------------|--------------|
| **Rotate** at the gallery | Scroll through 5 animations (Lissajous, Particle Flow, Hypnotic Spiral, Wave Grid, Game of Life) |
| **Press** at the gallery | Launch the selected animation, enter parameter list |
| **Rotate** in the list | Scroll through 17 parameters per animation |
| **Press** on "← Back" | Return to the animation gallery |
| **Press** on any parameter | Enter Edit mode — the parameter glows green |
| **Rotate** in Edit mode | Adjust the parameter value up/down |
| **Press** in Edit mode | Lock the value, return to list navigation |
| **Double-press** anytime | Instantly return to the animation gallery |

### What You See in the Browser

- **Full-screen canvas** with the current generative animation
- **Side panel** (swipes in from the right) showing all tweakable parameters
- **Animation name badge** that fades in/out on switch
- **Persistent settings** — every value you change is saved to the ESP32's NVS and restored after power loss

---

## Adding Your Own Animation

This is the whole point of the repo. You can add a new art mode by editing **one file**.

### Option A: Replace an existing animation

Edit `web/index.html`. Find the `ART_*` IIFE you want to replace (e.g., `ART_Lissajous`). Replace its `draw(params, timestamp, w, h)` function with your own Canvas 2D logic.

### Option B: Add a new slot

1. **`web/index.html`** — add your `ART_MySketch` IIFE with `init()` and `draw()` methods, then register it in the `sketches` array inside the ART dispatcher
2. **`main/param_store.c`** — add a new entry to `s_anim_profiles[]` with your parameter definitions
3. Update `ANIM_MAX_COUNT` in `main/param_store.h`

The `draw()` contract:

```javascript
function draw(params, timestamp, canvasWidth, canvasHeight) {
  // params[i].value — read float values from the firmware
  // timestamp — DOMHighResTimeStamp from requestAnimationFrame
  // Canvas 2D context is already set up — just paint
}
```

No firmware changes are needed to swap the visual output (Option A). The ESP32 just relays parameter values over WebSocket — it doesn't know or care what the canvas draws.

---

## Project Anatomy

```
├── main/                          # ESP32 firmware (C + ESP-IDF)
│   ├── main.c                     # Boot sequence: NVS → WiFi → queues → servers → UX
│   ├── encoder.c / .h             # Rotary encoder — ISR-driven quadrature decode (GPIO 18/19)
│   ├── ux_task.c / .h             # UX state machine — ANIM_SELECT / NAVIGATE / EDIT
│   ├── param_store.c / .h         # Parameter storage — 5 profiles × 17 params, NVS-backed
│   ├── ws_server.c / .h           # WebSocket server — JSON broadcast to all browser clients
│   ├── http_server.c / .h         # HTTP server — serves gzip'd web app, captive portal redirects
│   ├── wifi_ap.c / .h             # WiFi softAP — SSID "GenArt", no internet needed
│   └── dns_server.c / .h          # Captive portal DNS — all queries → 192.168.4.1
│
├── web/                           # The entire browser application
│   ├── index.html                 # Single-file web app — HTML, CSS, 5 art engines, WS client (~1370 lines)
│   ├── CMakeLists.txt             # Build pipeline: index.html → gzip → C header
│   └── bin2h.py                   # Binary-to-C-header converter (embeds web app in firmware)
│
├── CMakeLists.txt                 # Top-level ESP-IDF project
├── sdkconfig.defaults             # ESP-IDF config presets (WS support, FreeRTOS tick, etc.)
├── README.md                      # This file
└── docs_v1.md                     # Full architectural deep-dive, debugging stories, known issues
```

---

## Architecture

```
┌─────────────────────┐
│   Rotary Encoder     │          Three wires: CLK, DT, SW
│   (the only input)   │          No matrix, no keyboard, no touch
└──────┬──────────────┘
       │ ISR → FreeRTOS queue
┌──────▼──────────────┐     ┌──────────────────────┐
│  encoder_reader_task │────►│  ux_processing_task  │
│  (Core 1, 50ms)     │     │  (Core 1)            │
│  polls SW + debounce │     │  UX state machine    │
└─────────────────────┘     └──────┬───────────────┘
                                   │ ws_notify_queue
                                   │
┌──────────────────────┐    ┌──────▼───────────────┐
│  HTTP Server          │    │    ws_task           │
│  port 80              │    │  (Core 0)           │
│  /  → index.html.gz   │    │  broadcast JSON     │
│  /ws → WebSocket      │    │  to all browsers    │
│  captive redirects    │    └──────────────────────┘
└──────────────────────┘
        │                        ┌──────────────────────┐
┌───────▼────────┐               │  Your Browser        │
│  DNS Server     │               │                      │
│  port 53        │               │  Canvas 2D art       │
│  all → 192.168.4.1│             │  Param sidebar       │
└────────────────┘               │  WebSocket client    │
                                  └──────────────────────┘
```

Key architectural choices:

- **No Arduino layer** — native ESP-IDF for full control over tasks, timers, and memory
- **No JSON library** — `snprintf` only, zero heap fragmentation
- **No external dependencies** — all JS inline, system fonts, no CDN
- **No polling** — everything is pushed over WebSocket
- **No internet** — the ESP32 is the server, the router, and the DNS

---

## Animations Included

| # | Name | What It Does | Tweak |
|---|------|-------------|-------|
| 0 | **Lissajous** | Neon parametric curves with motion trails | Frequencies, fade, shadow glow, point density |
| 1 | **Particle Flow** | Thousands of particles with gravity, wind, turbulence | Spawn rate, lifespan, physics forces |
| 2 | **Hypnotic Spiral** | Rotating geometric mandala | Arm count, twist, pulse, color cycling |
| 3 | **Wave Grid** | 3D undulating wireframe wave field | Wave height, perspective, hue shift |
| 4 | **Game of Life** | Conway's cellular automaton | Birth/survive rules, grid size, glow, reseed |

Each animation exposes **17 parameters** mapped to the encoder's rotation. You can customize the ranges, defaults, and names in `param_store.c`.

---

## Customization Guide

### Change WiFi credentials

Edit `main/wifi_ap.h`:
```c
#define WIFI_AP_SSID      "MyCustomSSID"
#define WIFI_AP_PASSWORD  "mysecret"
```

### Change parameter ranges

Edit the profile entry in `main/param_store.c`:
```c
{ "speed", 0.0f, 2.0f, 0.01f, 0.50f, 2 },
//          min   max   step   default  decimals
```

### Add a new parameter slot

Increase `PARAM_MAX_COUNT` in `main/param_store.h` (currently 17), then add the new param definition to each profile.

### Change encoder pins

Edit `main/encoder.h`:
```c
#define ENCODER_PIN_CLK   18
#define ENCODER_PIN_DT    19
#define ENCODER_PIN_SW    21
```

### Replace the entire UI

Edit `web/index.html`. Everything is in one file. The build system compresses it into the firmware automatically.

---

## Build Pipeline

```
web/index.html                  ← You edit this
       │
       ▼ gzip -9
index.html.gz                   ← Automatic during build
       │
       ▼ python3 bin2h.py
web_content.h                   ← C uint8_t array embedded in binary
       │
       ▼ idf.py build
esp32_gen_art.bin               ← Flashed to ESP32
```

Run `idf.py build` after any HTML change — the pipeline detects the dependency and regenerates automatically.

---

## Detailed Reference

**`docs_v1.md`** contains the full architectural deep-dive: every component's implementation notes, debugging stories (including the notorious WebSocket handler bug and encoder VCC issue), known limitations, and a roadmap of future improvements.

---

## Tech Stack

| Layer | Technology | Why |
|-------|-----------|-----|
| MCU | ESP32 (dual-core Xtensa LX6) | $5, built-in WiFi+BLE, widely available |
| Firmware | ESP-IDF v6.0 (C) | Full control, no Arduino overhead |
| Real-time | FreeRTOS tasks, queues, ISR | Deterministic, dual-core |
| Web server | esp-httpd + WebSocket | Built into ESP-IDF, no porting |
| Frontend | Vanilla JS, Canvas 2D, CSS | Zero dependencies, works offline |
| Storage | NVS (flash) | Survives reboots, no SD card |
| Build | CMake + custom pipeline | Standard ESP-IDF, automatic |

---

## License

MIT — do whatever you want with this. Build a product, teach a workshop, make art. If you build something cool, I'd love to see it.
