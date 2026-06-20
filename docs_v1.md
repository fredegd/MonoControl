# ESP32 Generative Art Synthesizer — v1

## Overview

ESP32-based generative art device controlled via a rotary encoder. Runs a WiFi
access point and serves a real-time web UI displaying Lissajous figure
animations. 8 parameters are adjustable with a NAVIGATE/EDIT mode UX state
machine. Parameters persist across reboots via NVS.

---

## Architecture

```
┌─────────────────────┐
│   Rotary Encoder     │
│   CLK=18, DT=19, SW=21, VCC=3.3V, GND
└──────┬──────────────┘
       │ ISR (encoder_isr_handler)
       │ queue
┌──────▼──────────────┐     ┌──────────────────────┐
│  encoder_reader_task │────►│  ux_processing_task  │
│  (Core 1, 50ms)     │     │  (Core 1)            │
│  SW polling +        │     │  NAVIGATE/EDIT       │
│  debounce (300ms)    │     │  state machine       │
└─────────────────────┘     └──────┬───────────────┘
                                   │ ws_notify_queue
                                   │
┌──────────────────────┐    ┌──────▼───────────────┐
│  HTTP Server          │    │    ws_task           │
│  port 80, gzip HTML   │    │  (Core 0, 100ms)    │
│  /  → index.html      │    │  snapshot + notify   │
│  /ws → WebSocket      │    │  broadcast          │
└──────────────────────┘    └──────────────────────┘
                                   │
                          ┌───────▼────────┐
                          │  Browser (Web)  │
                          │  Lissajous art  │
                          │  Param sidebar  │
                          │  Glassmorphic   │
                          └────────────────┘
```

## Components

### `main.c`
- Initialization sequence: NVS → param_store → WiFi AP → queues → encoder → HTTP → WS → UX
- 8 default parameters: freq A (0.25), freq B (0.50), fade (0.05), brightness (0.80), param 5–8 (0.50)

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
  - **NAVIGATE**: rotate → change `preselected` index, single-click → enter EDIT mode
  - **EDIT**: rotate → adjust param value (clamped to min/max/step), single-click → return to NAVIGATE, double-click → return to NAVIGATE
  - Posts state/param changes to `ws_notify_queue`

### `param_store.c` / `param_store.h`
- Up to 16 params (`PARAM_MAX_COUNT`), each with name/min/max/step/value/decimals
- Thread-safe via mutex
- NVS-backed (namespace `param_store`): saves on `set_value`, loads on `init`
- `get_snapshot()` — copies all params for WS broadcast

### `wifi_ap.c` / `wifi_ap.h`
- SoftAP mode, SSID: `GenArt`, password: `genart00`, channel 6, max 4 connections
- IP: `192.168.4.1`

### `http_server.c` / `http_server.h`
- esp-httpd server, port 80, 7 max open sockets, LRU purge enabled, 8192 stack
- Root `/` handler serves gzip'd `web_content.h` with `Cache-Control: no-store`
- Request counter logged per serve

### `ws_server.c` / `ws_server.h`
- WebSocket endpoint at `/ws`, registered with `is_websocket = true`
- `ws_post_handshake_cb` — called after handshake completes, adds client fd to `s_clients[]`
- `ws_handler` — handles incoming data frames (read and discard)
- `ws_task` (Core 0, 100ms poll):
  - On `ws_notify_queue` message: builds JSON and `ws_broadcast()` to all clients
  - Periodically checks `s_pending_snapshot[]` → sends full snapshot to new clients
- Broadcast message types:
  - `{"type":"snapshot","mode":"...","preselected":N,"active":N,"params":[...]}`
  - `{"type":"state_changed","mode":"...","preselected":N,"active":N}`
  - `{"type":"param_changed","index":N,"value":X.XXXX}`
- No dynamic allocation for JSON — all stack buffers

### `web/index.html`
- Dark glassmorphic UI, system fonts (no CDN)
- **WS module**: auto-reconnect with 2s delay, URL nonce `?t=${Date.now()}`
- **PARAMS module**: state management for mode/preselected/active/params array
- **UI module**: renders param rows with preselected/active highlighting
- **ART module**: draws Lissajous figures with neon hue cycling, parameterized fade/brightness
- Animation loop via `requestAnimationFrame`

### Build Pipeline (`web/CMakeLists.txt`, `web/bin2h.py`)
- `index.html` → gzip -9 → `index.html.gz` → `bin2h.py` → `web_content.h` (C uint8_t array)
- Generated in `build/web/web_content.h`

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
| Button relies on polling (300ms) | Medium | Misses fast presses; ISR impossible due to crosstalk |
| Rotation cooldown (500ms) | Low | Prevents false EDIT entries but limits rotation throughput |
| ws_task stack 4096 | Low | Tight for `snprintf` JSON formatting; increase if crashes |
| No client→server WS messages | Low | Unused; all communication is server→client push |
| Web UI: single Lissajous figure only | Low | No particle system or alternative art modes |
| Build: CMake not in tree | Low | No `idf.py` `reconfigure` required; sdkconfig defaults exist |
| No watchdog timer | Medium | A hung ISR or deadlock hangs the device until power cycle |
| No OTA updates | Medium | Firmware updates require USB serial reflash |
| No WiFi manager | Low | Always starts in AP mode; no STA fallback or config portal |

---

## Future Improvements

### Short-term
- Add `ws_close` detection — remove client on WebSocket close frame
- Increase ws_task stack to 6144 or 8192
- Add watchdog timer (`esp_task_wdt`) for production reliability
- Add `httpd_free_ctx_fn` to clean up on client disconnect
- Batch WS notifications: coalesce rapid param changes (e.g., during rotation) into a single broadcast

### Medium-term
- Additional art modes: particle system, plasma, perlin noise flow fields
- Parameter presets: save/load named presets from NVS
- Web UI: expand collapsed params, slider widgets, art mode selector
- OTA firmware updates via web UI
- mDNS responder (`esp32-genart.local`) for easy discovery
- WiFi manager: AP + STA modes, captive portal for first-time setup

### Long-term
- BLE remote control companion app
- Multiple encoder profiles (swap params per user)
- Art mode sequencing / autonomous parameter animation
- Power management: display sleep, deep sleep on idle
- Production enclosure CAD files + assembly guide
- Automated CI: build + lint + flash test

---

## File Reference

| File | Lines | Purpose |
|------|-------|---------|
| `main/main.c` | 61 | Entry point, init sequence, default params |
| `main/http_server.c` | 56 | HTTP server, gzip content serving |
| `main/http_server.h` | 5 | Header |
| `main/ws_server.c` | 252 | WebSocket server, client tracking, broadcast |
| `main/ws_server.h` | 25 | Header, notify message types |
| `main/encoder.c` | 121 | Rotary encoder ISR, SW poll |
| `main/encoder.h` | 24 | Pin defines, event types |
| `main/ux_task.c` | 301 | UX state machine, encoder reader, button debounce |
| `main/ux_task.h` | 19 | State type, mode enum |
| `main/param_store.c` | 145 | NVS-backed parameter storage |
| `main/param_store.h` | 23 | Param struct, API |
| `main/wifi_ap.c` | 98 | WiFi softAP init |
| `main/wifi_ap.h` | 9 | SSID/password defines |
| `web/index.html` | 499 | Full web app (HTML/CSS/JS) |
| `web/CMakeLists.txt` | 27 | Web content build pipeline |
| `web/bin2h.py` | 39 | Binary-to-C-header converter |
| `CMakeLists.txt` | 4 | Top-level CMake |
| `sdkconfig.defaults` | 20 | ESP-IDF config defaults |
| `sdkconfig` | 3460 | Full ESP-IDF config |
| `tasklist.md` | 19 | Original task tracking |

---

## Hardware Wiring

| Pin | GPIO | Connection |
|-----|------|------------|
| CLK | 18 | Rotary encoder channel A |
| DT | 19 | Rotary encoder channel B |
| SW | 21 | Rotary encoder push button |
| VCC | 3.3V | Encoder power (critical — was missing) |
| GND | GND | Common ground |
