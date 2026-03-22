# REST API Migration & Dynamic Configuration Plan

**Status**: DRAFT — Awaiting alignment
**Created**: 2026-03-22

---

## Why REST over WebSocket

- WebSocket has ~5-12s round-trip latency on LAN (library issue, not our code)
- WebSocket requires persistent connection management, reconnection logic, idle disconnect/reconnect dance
- Light sleep and WebSocket fight each other — connection state vs CPU sleep
- REST is stateless: call, get response, done. Sleep whenever you want.
- REST makes the architecture simpler and more extensible

## Design Principles

### 1. Optimistic UI Updates

When the user taps a button or moves a slider:

- **Immediately** update the screen to reflect the new state (assume success)
- Fire the REST call in the background
- If the call fails, **revert** the UI to the previous state and show a brief error indicator
- Never show error screens or grey states during normal operation

### 2. Polling for State Sync

- On boot: fetch all entity states via REST (`GET /api/states/<entity_id>`)
- Periodically poll for state changes (configurable interval, e.g., every 10-30 seconds when active)
- On touch: immediately poll to get fresh state before displaying
- When idle/sleeping: don't poll at all (no WiFi needed)

### 3. Sleep-Friendly Architecture

- WiFi is only needed during active polling windows and command sends
- Between polls: WiFi can be off, CPU can sleep
- Flow: wake → connect WiFi → poll states → process touch → send commands → disconnect WiFi → sleep
- Touch interrupt wakes from sleep → immediate command send, then poll for confirmation

### 4. No Error Screens During Normal Operation

- If WiFi fails to connect: retry silently, show last known state
- If REST call fails: revert UI, show small error indicator (not full screen error)
- Full-screen error only on boot when there's never been a successful connection
- Once connected successfully at least once, always show the last known widget state

---

## Architecture

### Action Abstraction (beyond HA)

Arduino supports classes and inheritance. Design actions as an interface:

```cpp
class Action {
public:
    virtual ~Action() = default;
    virtual bool execute(uint8_t value) = 0;     // Send command, return success
    virtual int8_t getState() = 0;                // Poll current state, return value
    virtual const char* getEntityId() = 0;
};

class HARestAction : public Action {
    // REST call to HA: POST /api/services/<domain>/<service>
    // State poll: GET /api/states/<entity_id>
};

class HTTPAction : public Action {
    // Generic HTTP call to any URL (webhooks, IFTTT, custom APIs)
};

class MQTTAction : public Action {
    // Future: MQTT publish for direct device control
};
```

Each widget gets an Action pointer instead of a hardcoded entity config. This decouples the UI from HA entirely.

### Dynamic Configuration via NVS + Captive Portal

#### Phase 1: NVS Storage

- Store widget/entity configuration in NVS (non-volatile storage) instead of compiled config_remote.cpp
- On first boot with empty NVS: start captive portal for setup
- Configuration stored as JSON in NVS

#### Phase 2: Captive Portal

- When no configuration exists (or user holds button on boot): start WiFi AP + captive portal
- Web page served from ESP32 for configuration:
    - WiFi SSID/password
    - HA URL and access token
    - Device discovery: `GET /api/states` returns all HA entities
    - Drag and drop entity selection and widget assignment
    - Widget type selection (slider, button, etc.)
    - Widget positioning (or auto-layout)
    - Save to NVS
    - Reboot into normal mode

#### HA Entity Discovery

HA REST API supports:

- `GET /api/states` — returns ALL entities with current state and attributes
- Response includes entity_id, state, attributes (friendly_name, device_class, etc.)
- We can filter by domain (light.*, switch.*, fan.*, etc.) and present a clean list
- User selects entities, assigns widget types, arranges layout
- Config saved to NVS as JSON

### REST API Calls

#### State Polling

```
GET /api/states/<entity_id>
Authorization: Bearer <token>

Response: {"entity_id": "light.living_room", "state": "on", "attributes": {"brightness": 180, ...}}
```

#### Command Sending

```
POST /api/services/<domain>/<service>
Authorization: Bearer <token>
Content-Type: application/json

{"entity_id": "<entity_id>", "brightness_pct": 75}
```

#### Bulk State Fetch (efficient)

```
GET /api/states
Authorization: Bearer <token>

Response: [{"entity_id": "...", "state": "...", ...}, ...]
```

Filter client-side for our configured entities. One call gets everything.

---

## Implementation Phases

### Phase R1: REST Client (replace WebSocket) ✅ COMPLETE

- ✅ Replace WebSocket with HTTPClient REST calls
- ✅ Per-entity state fetch via `GET /api/states/<entity_id>`
- ✅ Command send via `POST /api/services/<domain>/<service>`
- ✅ Periodic polling (HA_REST_POLL_INTERVAL_MS, default 10s)
- ✅ Remove WebSocket library dependency, add ArduinoJson
- ✅ Created lib/ha-rest/ as portable library with DeviceAction interface
- ✅ Wake locks around all HTTP operations
- ✅ WiFi idle disconnect/reconnect preserved
- ✅ PMS150G shutdown preserved
- ⚠️ Optimistic UI partially working — commands fire immediately, but no revert on failure yet
- ⚠️ Slider rendering artifact when value decreases (pre-existing, not REST-related)
- ⚠️ input_number state parsing via numeric string works

**Result**: Noticeably faster boot and command response vs WebSocket. No more 12s latency.

### Phase R2: Sleep Integration — MOSTLY COMPLETE

Goal: With REST, WiFi is only needed during poll/command windows. Between them, disconnect WiFi and sleep.

- ✅ All power features re-enabled and working with REST:
  - WiFi modem sleep, light sleep, idle WiFi disconnect, PMS150G shutdown, BMI270 suspend
- ✅ Wake locks protect all HTTP operations (REST calls, state polling)
- ✅ Wake lock bridge: store_send_command() holds lock until HA task processes command
  (prevents CPU sleeping between touch and command send)
- ✅ Light sleep idle hook with guards: 60s boot delay, USB detection, 100ms min wake, single core
- ✅ No persistent connection to maintain — REST is stateless, perfect for sleep
- ✅ WiFi reconnect after idle disconnect falls through to command processing
  (no leaked wake locks)
- ⚠️ Testing in progress — verifying touch responsiveness after idle periods
- ⚠️ Light sleep + WiFi first-entry disconnect seen once previously (60s delay may fix)

Key improvement over WebSocket: no 12-second latency, no connection state machine,
no fighting between persistent connection and sleep cycles.

### Phase R3: Action Abstraction — NOT STARTED

- ✅ DeviceAction interface created (device_interface.h)
- ✅ HADeviceAction implementation created (ha_device_action.h/cpp)
- ❌ Widgets still reference entity configs via store, not Actions directly
- ❌ Need to wire widgets → Actions instead of store → entity index
- Future: enables non-HA backends (Alexa, webhooks, MQTT)

### Phase R4: NVS Configuration Storage ✅ COMPLETE

- ✅ ConfigStore class in lib/ha-rest/ — single JSON blob in NVS
- ✅ AppConfig struct: WiFi, HA, settings, known devices, UI devices
- ✅ Wired into main.cpp and HA REST manager
- ✅ Seeds defaults from hardcoded config_remote.cpp (backward compat)
- ✅ NVS values override hardcoded if saved
- ✅ Poll interval read from config store

### Phase R4.5: On-Device Settings UI — IN PROGRESS

Goal: Gear icon + settings menu on e-ink screen, prerequisite for captive portal.

- Gear icon (bottom right, always visible on main screen)
- Touch target for gear icon opens settings menu
- Settings menu screen: rendered list with touch targets
  - "Configure WiFi" → WiFi setup screen
  - "Configure Home Assistant" → HA setup screen
  - "About" → version/battery/IP info
  - Back button → return to main screen
- WiFi setup screen: "Connect to 'HA-Remote' to configure WiFi"
  - Starts WiFiManager captive portal AP
  - Shows AP name and instructions
- HA setup screen: "Open http://{device_ip} to configure"
  - Starts web server for HA token/entity config
- All screens use existing e-ink widget patterns (no LVGL)

**Status**: ✅ COMPLETE
- ✅ Gear icon in bottom-right corner of main screen
- ✅ Settings menu: Configure WiFi, Configure HA, About (About not yet implemented)
- ✅ WiFi setup screen with AP name + captive portal instructions
- ✅ HA setup screen with device IP + browser instructions
- ✅ Back navigation from all screens
- ✅ Buzzer feedback on all touches, debounced (single chirp)
- ✅ UI mode override in store for screen switching

### Phase R5: Captive Portal — IN PROGRESS

WiFi provisioning and HA configuration via web browser.

**WiFi Setup (via WiFiManager library)**:
- Tapping "Configure WiFi" in settings starts WiFiManager AP ("HA-Remote")
- User connects phone/laptop to AP, captive portal auto-opens
- Select WiFi network, enter password, save
- WiFiManager handles AP teardown + reconnect to selected network
- On success: save creds to NVS via ConfigStore, return to main screen
- On back/cancel: stop AP, return to settings menu

**HA Setup (via built-in web server)**:
- Tapping "Configure HA" starts web server on device's WiFi IP
- E-ink screen shows "Open http://{ip}" instructions
- Web page: enter HA URL + token, discover entities, configure widgets
- Save to NVS via ConfigStore
- On back: stop web server, return to settings menu

**Reference**: `reference/HomeControl/src/ui/screens/admin_portal.cpp` for WiFiManager pattern

---

## Reference Implementation: HomeControl (reference/HomeControl/)

Existing Alexa-specific project with captive portal, NVS config, web UI, device management. Reviewed and mapped below
for reuse.

### Directly Reusable (copy & adapt)

| Component           | Source File                                 | What It Does                                                           | Adaptation Needed                                         |
|---------------------|---------------------------------------------|------------------------------------------------------------------------|-----------------------------------------------------------|
| **WiFi Setup**      | `src/ui/screens/admin_portal.cpp`           | WiFiManager captive portal (non-blocking)                              | Minimal — swap LVGL UI for e-ink "setup WiFi" screen      |
| **Config Service**  | `src/services/config_service.cpp/.h`        | NVS key-value storage with Preferences lib                             | Rename keys for HA config (url, token, polling intervals) |
| **Device Config**   | `src/services/device_config_service.cpp/.h` | Per-device NVS storage (visibility, sort, icon) using FNV-1a hash keys | Change from alexa_id to HA entity_id                      |
| **Web Server**      | `src/services/web_server.cpp/.h`            | Arduino WebServer on port 80, REST API routes                          | Replace Alexa routes with HA routes                       |
| **Web Config UI**   | `web/index.html`                            | HTML5 drag-and-drop device config, icon picker                         | Replace Alexa device list with HA entity list             |
| **HTTP Client**     | Pattern from `lib/alexa-esp32/`             | WiFiClientSecure + HTTPClient for REST calls                           | Replace Alexa endpoints with HA REST API                  |
| **Partition Table** | `partition_table.csv`                       | 24KB NVS, 14MB app, 1.9MB SPIFFS                                       | Adjust sizes for our needs                                |

### Patterns to Adopt

1. **Service Architecture**: C-style functions per service (`config_service_init()`, `device_service_init()`). Each
   service manages its own NVS namespace.

2. **NVS Storage**: `Preferences` library with namespaces. Per-device config via hashed entity IDs. Helper functions:
   `load_str`, `save_str`, `load_int`, `save_int`.

3. **Device Struct**: Adapt `device_info_t` for HA:

```cpp
typedef struct {
    char name[48];
    char entity_id[64];
    device_type_t type;       // LIGHT, SWITCH, FAN, COVER, etc.
    uint8_t value;            // 0-100 or on/off
    bool visible;
    int sort_order;
    char icon_id[32];
    char widget_type;         // SLIDER, BUTTON
} ha_device_info_t;
```

4. **Web UI**: Template-based HTML, fetch-based API calls, native HTML5 drag-and-drop. Icons as base64 data URIs in
   manifest JSON.

5. **Non-blocking WiFi**: WiFiManager polled via timer, not blocking main loop. Portal auto-exits on successful
   connection.

6. **Polling**: Two-tier polling (background slow + active fast) with configurable intervals stored in NVS.

### Libraries to Add

- `https://github.com/tzapu/WiFiManager` — Captive portal WiFi provisioning
- `bblanchon/ArduinoJson@^7.0.0` — JSON parsing (replacing cJSON)
- Arduino built-in `WebServer` — REST API serving
- Arduino built-in `Preferences` — NVS storage

### Not Reusable (Alexa-specific)

- `lib/alexa-esp32/` — OAuth, PKCE, GraphQL device discovery (HA uses simple token + REST)
- LVGL UI screens — e-ink uses custom widget system
- Weather/Time services — separate feature, not needed for core

## Open Questions

1. How much SPIFFS/NVS space do we need for config storage? (Currently 3.4MB SPIFFS partition unused)
2. Should the captive portal use LVGL on the e-ink screen for local feedback, or just the web UI?
3. Do we want MQTT support alongside REST for future direct device control?
4. Polling interval tradeoffs: faster = more responsive but more battery drain. Configurable per-user?
5. Should we support HA's template entities for computed states?
