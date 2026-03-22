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

### Phase R2: Sleep Integration — IN PROGRESS

Goal: With REST, WiFi is only needed during poll/command windows. Between them, disconnect WiFi and sleep.

- Connect WiFi → poll states → process touch → send commands → disconnect WiFi → sleep
- Wake on touch → connect WiFi → send command → poll → disconnect → sleep
- Wake on timer (N seconds) → connect WiFi → poll states → disconnect → sleep
- No persistent connection needed — perfect for light sleep
- Key difference from WebSocket: no connection state to maintain through sleep

### Phase R3: Action Abstraction — NOT STARTED

- ✅ DeviceAction interface created (device_interface.h)
- ✅ HADeviceAction implementation created (ha_device_action.h/cpp)
- ❌ Widgets still reference entity configs via store, not Actions directly
- ❌ Need to wire widgets → Actions instead of store → entity index
- Future: enables non-HA backends (Alexa, webhooks, MQTT)

### Phase R4: NVS Configuration Storage

- Move config from compiled config_remote.cpp to NVS JSON
- Boot reads config from NVS
- If no config: show setup required screen

### Phase R5: Captive Portal

- WiFi AP mode with web server
- Entity discovery via `GET /api/states`
- Configuration UI (HTML/JS served from ESP32)
- Save to NVS
- This eliminates recompile for config changes entirely

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
