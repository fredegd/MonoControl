# ESP32 Generative Art Synthesizer

A standalone ESP32 device that generates real-time generative art, controlled via a hardware rotary encoder and streamed to any browser over WiFi.

Connect to the `GenArt` WiFi AP, open `http://192.168.4.1`, and rotate the encoder to explore 5 animation modes with full parameter control.

## Features

- **5 generative animations**: Lissajous curves, Particle Flow, Hypnotic Spiral, Wave Grid, Conway's Game of Life
- **Rotary encoder input**: rotate to navigate/select, press to edit parameters
- **Real-time WebSocket UI**: glassmorphic web interface, no polling, no internet required
- **Captive portal**: all DNS resolves to the ESP32 — no configuration needed
- **NVS persistence**: parameter values survive power cycles
- **17 parameters per animation**: fully adjustable via encoder

## Quick Start

### Hardware

| Pin | GPIO | Connection |
|-----|------|------------|
| CLK | 18 | Rotary encoder channel A |
| DT | 19 | Rotary encoder channel B |
| SW | 21 | Rotary encoder push button |
| VCC | 3.3V | Encoder power |
| GND | GND | Common ground |

### Build & Flash

```bash
cd esp32_gen_art
source ~/.espressif/v6.0.1/esp-idf/export.sh
idf.py -p /dev/cu.usbserial-1410 flash monitor
```

Connect to WiFi SSID `GenArt` (password `genart00`), open `http://192.168.4.1`.

## UX Controls

| Action | Mode | Effect |
|--------|------|--------|
| Rotate | Anim Select | Scroll through animations |
| Press | Anim Select | Select animation, enter Navigate mode |
| Rotate | Navigate | Scroll through parameters |
| Press | Navigate | Enter Edit mode for selected param |
| Press "← Back" | Navigate | Return to Anim Select |
| Rotate | Edit | Adjust parameter value |
| Press | Edit | Return to Navigate |

Double-press at any point returns to the animation gallery.

## Animations

| # | Name | Description | Key Params |
|---|------|-------------|------------|
| 0 | Lissajous | Parametric curves with neon motion trails | freq A/B, fade, shadow, density |
| 1 | Particle Flow | Flowing particles with gravity, wind & turbulence | spawn rate, life span, forces |
| 2 | Hypnotic Spiral | Rotating geometric mandala | arms, twist, pulse, color cycle |
| 3 | Wave Grid | Undulating 3D wave field | wave height, perspective, hue shift |
| 4 | Game of Life | Conway-style cellular automaton | birth/survive rules, grid size, glow |

## Project Structure

```
├── CMakeLists.txt
├── sdkconfig.defaults
├── main/
│   ├── CMakeLists.txt
│   ├── main.c              # Entry point, init sequence
│   ├── encoder.c/h         # Rotary encoder ISR driver (GPIO 18/19/21)
│   ├── ux_task.c/h         # UX state machine (ANIM_SELECT/NAVIGATE/EDIT)
│   ├── param_store.c/h     # NVS-backed parameter storage (5 profiles × 17 params)
│   ├── ws_server.c/h       # WebSocket server, client tracking, broadcast
│   ├── http_server.c/h     # HTTP server, gzip content serving
│   ├── wifi_ap.c/h         # WiFi softAP (SSID: GenArt)
│   └── dns_server.c/h      # Captive portal DNS (all → 192.168.4.1)
├── web/
│   ├── index.html          # Full web app (HTML/CSS/JS, ~1370 lines)
│   ├── CMakeLists.txt      # Build pipeline: HTML → gzip → C header
│   └── bin2h.py            # Binary-to-C-header converter
└── build/                  # Generated build artifacts
```

## Architecture

```
┌─────────────────────┐
│   Rotary Encoder     │
│   CLK=18, DT=19, SW=21
└──────┬──────────────┘
       │ ISR → queue
┌──────▼──────────────┐     ┌──────────────────────┐
│  encoder_reader_task │────►│  ux_processing_task  │
│  (Core 1)           │     │  (Core 1)            │
│  SW polling +        │     │  UX state machine    │
│  double-click detect │     └──────┬───────────────┘
└─────────────────────┘            │ ws_notify_queue
                                   │
┌──────────────────────┐    ┌──────▼───────────────┐
│  HTTP Server          │    │    ws_task           │
│  port 80, gzip HTML   │    │  (Core 0)           │
│  /  → index.html      │    │  broadcast JSON      │
│  /ws → WebSocket       │    │  to all clients     │
└──────────────────────┘    └──────────────────────┘
                                   │
                          ┌───────▼────────┐
                          │  Browser (Web)  │
                          │  Canvas + UI    │
                          └────────────────┘
```

## Build Pipeline

`index.html` → gzip -9 → `index.html.gz` → `bin2h.py` → `web_content.h` (C uint8_t array compiled into firmware).

## Detailed Documentation

See [`docs_v1.md`](docs_v1.md) for in-depth architecture, debugging stories, and lessons learned.

## Tech Stack

- **MCU**: ESP32 (dual-core Xtensa LX6)
- **Framework**: ESP-IDF v6.0 (native, no Arduino)
- **Web**: Vanilla JS, Canvas 2D, WebSocket, CSS glassmorphism
- **Storage**: NVS (Non-Volatile Storage)
- **Build**: CMake, custom build pipeline for web assets

## License

MIT
