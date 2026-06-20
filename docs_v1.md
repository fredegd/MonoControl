# ESP32 Generative Art Synthesizer — v1

## Overview

ESP32-based generative art device controlled via a rotary encoder. Runs a WiFi
access point and serves a real-time web UI with 5 generative animation modes
(Lissajous, Particle Flow, Hypnotic Spiral, Wave Grid, Game of Life). 17
parameters per animation are adjustable with an ANIM_SELECT/NAVIGATE/EDIT mode
UX state machine. Parameters persist across reboots via NVS.

---

## Architecture

```
┌─────────────────────┐
│   Rotary Encoder     │
│   CLK=18, DT=19, SW=21
└──────┬──────────────┘
       │ ISR → queue
┌──────▼──────────────┐     ┌──────────────────────┐
│  encoder_reader_task │────►│  ux_processing_task  │
│  (Core 1, 50ms)     │     │  (Core 1)            │
│  SW polling +        │     │  ANIM_SELECT /       │
│  double-click detect │     │  NAVIGATE / EDIT     │
└─────────────────────┘     └──────┬───────────────┘
                                   │ ws_notify_queue
                                   │
┌──────────────────────┐    ┌──────▼───────────────┐
│  HTTP Server          │    │    ws_task           │
│  port 80, gzip HTML   │    │  (Core 0, 100ms)    │
│  /  → index.html      │    │  broadcast JSON      │
│  /ws → WebSocket       │    │  to all clients     │
└──────────────────────┘    └──────────────────────┘
        │                              │
┌───────▼────────┐           ┌─────────▼──────────┐
│  DNS Server     │           │  Browser (Web)      │
│  port 53,       │           │  5 generative arts  │
│  captive portal │           │  Glassmorphic UI    │
│  all → 192.168.4.1  │           └────────────────────┘
└────────────────┘
```

## Components

### `main.c`
- Initialization sequence: NVS → param_store → WiFi AP → DNS → queues → encoder → HTTP → WS → UX
- 5 animation profiles with 17 parameters each defined in `param_store.c`

### `encoder.c` / `encoder.h`
- GPIO: CLK=18, DT=19 (ISR, any-edge, pull-up), SW=21 (polling only, pull-up, no ISR)
- ISR fires on every edge, tracks state transitions, only emits events on return-to-idle (both pins high, state 3)
- 2ms rotation cooldown (hardware debounce in ISR)
- `encoder_read_sw()` — direct GPIO read, non-blocking

### `ux_task.c`
- `encoder_reader_task` (50ms poll, Core 1):
  - Reads rotation events from ISR queue → forwards to UX queue
  - Polls SW pin with 6-sample debounce (300ms)
  - 500ms rotation cooldown — cancels any pending click timer (prevents false EDIT entry after rotation)
  - Click timer: 300ms window → single-click if timer expires, double-click if another press arrives
  - `button_pressed_flag` ensures one event per press-release cycle
- `ux_processing_task` (Core 1):
  - **ANIM_SELECT**: rotate → cycle animations, click → load selected animation → enter NAVIGATE
  - **NAVIGATE**: rotate → change `preselected` param, click → enter EDIT, click "← Back" → return to ANIM_SELECT, double-click → return to ANIM_SELECT
  - **EDIT**: rotate → adjust param value (clamped to min/max/step), click → return to NAVIGATE
  - Posts state/param/anim changes to `ws_notify_queue`
- Supports `ux_task_post_back()` — triggered via WebSocket "back" action from the browser's swipe-down gesture

### `param_store.c` / `param_store.h`
- Up to 17 params (`PARAM_MAX_COUNT`) per animation profile, with
  name/min/max/step/value/decimals
- 5 animation profiles (`s_anim_profiles[]`) — Lissajous, Particle Flow,
  Hypnotic Spiral, Wave Grid, Game of Life
- Thread-safe via mutex
- NVS-backed: each animation has own namespace `params_0`–`params_4`,
  saves on `set_value`, loads on `init` and `switch_anim`
- Current animation index persists across reboots
- `get_snapshot()` — copies all params for WS broadcast
- Profile 0 includes a virtual "← Back" entry (index 0) for UX navigation

### `wifi_ap.c` / `wifi_ap.h`
- SoftAP mode, SSID: `GenArt`, password: `genart00`, channel 6, max 4 connections
- IP: `192.168.4.1`

### `dns_server.c` / `dns_server.h`
- Custom UDP DNS server on port 53
- Captive portal: responds to all DNS queries with `192.168.4.1`
- No external dependencies — raw sockets

### `http_server.c` / `http_server.h`
- esp-httpd server, port 80, 7 max open sockets, LRU purge enabled, 8192 stack
- Root `/` handler serves gzip'd `web_content.h` with `Cache-Control: no-store`
- Captive portal redirects for `/generate_204`, `/hotspot-detect.html`, etc.
- Request counter logged per serve

### `ws_server.c` / `ws_server.h`
- WebSocket endpoint at `/ws`, registered with `is_websocket = true`
- `ws_post_handshake_cb` — called after handshake completes, adds client fd to `s_clients[]`
- `ws_handler` — handles incoming data frames, parses JSON for `"action":"back"` to trigger `ux_task_post_back()`
- `ws_task` (Core 0, 100ms poll, 6144 stack):
  - On `ws_notify_queue` message: builds JSON and `ws_broadcast()` to all clients
  - Periodically checks `s_pending_snapshot[]` → sends full snapshot to new clients
- Broadcast message types:
  - `{"type":"snapshot","mode":"...","anim_index":N,"anim_count":5,"anim_names":["Lissajous",...],"preselected":N,"active":N,"params":[...]}`
  - `{"type":"state_changed","mode":"...","preselected":N,"active":N}`
  - `{"type":"param_changed","index":N,"value":X.XXXX}`
  - `{"type":"animation_changed","anim_index":N,"anim_name":"..."}`
- No dynamic allocation for JSON — all stack buffers
- `httpd_ws_send_frame_async` used for non-blocking sends

### `web/index.html`
- Dark glassmorphic UI, system fonts (no CDN)
- **WS module**: auto-reconnect with 2s delay, URL nonce `?t=${Date.now()}`
- **PARAMS module**: state management for mode/preselected/active/params array + anim_index
- **UI module**: renders param rows with preselected/active highlighting, animation gallery
  in ANIM_SELECT mode with descriptions, swipe-down gesture for "back" action
- **ART dispatcher**: routes to the active sketch module (out of 5), handles switch with
  fade-to-black and animation name badge
- **5 ART modules** (`ART_Lissajous`, `ART_ParticleFlow`, `ART_HypnoticSpiral`,
  `ART_WaveGrid`, `ART_GameOfLife`) — each an IIFE with `init()`/`draw()` contract
- Animation loop via `requestAnimationFrame`

### Build Pipeline (`web/CMakeLists.txt`, `web/bin2h.py`)
- `index.html` → gzip -9 → `index.html.gz` → `bin2h.py` → `web_content.h` (C uint8_t array)
- Generated in `build/web/web_content.h`
- `web/bin2h.py` is invoked by the build system to convert gzip binary to C header

---

## Build & Run

```bash
cd /Users/gm/Documents/HARDW_PROJECT/ESP32_rotary_server
source ~/.espressif/v6.0.1/esp-idf/export.sh
idf.py -p /dev/cu.usbserial-1410 flash monitor
```

Connect to WiFi SSID `GenArt` (password `genart00`), browse to `http://192.168.4.1`.

---

## Debugging Stories (Lessons Learned)

### 1. Encoder VCC Pin Not Connected
- Symptom: Button presses triggered on every rotation (CLK/DT crosstalk into floating SW pin)
- Root cause: VCC pin was unconnected — encoder's internal pull-ups didn't work, SW pin floated
- Fix: Wired VCC to 3.3V

### 2. Google Fonts CDN Blocking Page Load
- Symptom: Page never loaded on isolated AP, JS blocked by CDN timeout
- Root cause: `<link href="https://fonts.googleapis.com/...">` requires internet
- Fix: Removed CDN links, used system font stack (`-apple-system, BlinkMacSystemFont, ...`)

### 3. WebSocket Handler Never Called
- Symptom: Browser showed "WS connected" (status 101), but ESP32 serial log showed no `ws_handler called`, sidebar stayed empty
- Root cause: esp-idf's `httpd_uri.c:362` **skips calling the registered handler during WebSocket handshake**:
  ```c
  /* If the request is websocket handshake, then do not call the uri->handler */
  return ESP_OK;
  ```
  The handler is only called on subsequent **data frames**, which our client never sends (server→client only).
- Fix:
  1. Enabled `CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT=y` in sdkconfig
  2. Added `ws_post_handshake_cb()` — this callback IS invoked right after handshake
  3. Registered it via `.ws_post_handshake_cb = ws_post_handshake_cb` in the URI handler struct

### 4. Button ISR Noise
- Symptom: >10k interrupts/second from SW pin (CLK/DT crosstalk)
- Fix: Removed button ISR entirely, polling only with 300ms debounce

---

## Known Issues

| Issue | Severity | Notes |
|-------|----------|-------|
| Button relies on polling (10ms task) | Low | Max ~30ms latency; ISR impossible due to CLK/DT crosstalk on SW pin |
| Rotation cooldown (50ms) | Low | 50ms window after last rotation suppresses button to prevent false EDIT entry |
| Captive portal detection varies | Low | Catch-all redirect added; some edge-case OS probes may still slip through |

### Intentionally Out of Scope

| Item | Rationale |
|------|-----------|
| OTA firmware updates | Requires changing the partition table (TOT, size layout), adding an HTTP upload handler with firmware verification, and a web UI upload button. Would increase attack surface and code complexity significantly. The current USB-serial flash workflow is simple and reliable for a device meant to be built once and flashed rarely. |
| WiFi manager (AP+STA with config portal) | The device is deliberately **always in AP mode** — no internet required, works anywhere. Adding a STA mode with credential persistence and AP fallback would double the networking code, add a config portal web page, and undermine the "plug and play, works in a desert" ethos. If STA mode is needed for your use case, this is a fork-friendly extension point. |

---

### Resolved

| Issue | Fix |
|-------|-----|
| ws_task stack 4096 | Increased to 6144 — adequate |
| No client→server WS messages | "back" action implemented via JSON `{"action":"back"}` |
| No watchdog timer | `esp_task_wdt` added to main task |
| No mDNS | `genart.local` registered via ESP-IDF mDNS component |

## Future Improvements

### Short-term
- Batch WS notifications: coalesce rapid param changes (e.g., during rotation) into a single broadcast
- Add canvas screenshot/download button in web UI

### Medium-term
- Parameter presets: save/load named presets from NVS
- Web UI improvements: collapsible sections, slider widgets
- BLE remote control companion app
- Multiple encoder profiles (swap params per user)

### Long-term
- Art mode sequencing / autonomous parameter animation
- Power management: display sleep, deep sleep on idle
- Production enclosure CAD files + assembly guide
- Automated CI: build + lint + flash test

---

## File Reference

| File | Lines | Purpose |
|------|-------|---------|
| `main/main.c` | 62 | Entry point, init sequence (mDNS, watchdog) |
| `main/http_server.c` | 92 | HTTP server, gzip content serving, captive redirects |
| `main/http_server.h` | 5 | Header |
| `main/ws_server.c` | 306 | WebSocket server, client tracking, broadcast |
| `main/ws_server.h` | 27 | Header, notify message types (+ ANIM_CHANGED) |
| `main/encoder.c` | 121 | Rotary encoder ISR, SW poll |
| `main/encoder.h` | 24 | Pin defines, event types |
| `main/ux_task.c` | 379 | UX state machine (3 modes), encoder reader, button reader (10ms), click debounce |
| `main/ux_task.h` | 21 | State type, mode enum (ANIM_SELECT/NAVIGATE/EDIT) |
| `main/param_store.c` | 328 | NVS-backed parameter storage, 5 animation profiles |
| `main/param_store.h` | 35 | Param struct, profile API, ANIM_MAX_COUNT |
| `main/wifi_ap.c` | 98 | WiFi softAP init |
| `main/wifi_ap.h` | 9 | SSID/password defines |
| `main/dns_server.c` | 108 | Captive portal DNS server |
| `main/dns_server.h` | 4 | Header |
| `web/index.html` | ~1376 | Full web app (HTML/CSS/JS) with 5 art modules |
| `web/CMakeLists.txt` | 27 | Web content build pipeline |
| `web/bin2h.py` | 39 | Binary-to-C-header converter |
| `CMakeLists.txt` | 4 | Top-level CMake |
| `sdkconfig.defaults` | 20 | ESP-IDF config defaults |

---

## Hardware Wiring

| Pin | GPIO | Connection |
|-----|------|------------|
| CLK | 18 | Rotary encoder channel A |
| DT | 19 | Rotary encoder channel B |
| SW | 21 | Rotary encoder push button |
| VCC | 3.3V | Encoder power (critical — was missing) |
| GND | GND | Common ground |
