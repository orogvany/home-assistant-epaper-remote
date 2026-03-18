# Code Quality Audit

**Status**: Assessment complete — critical #2 and #3 fixed, nice-to-fix #3, #4, #5 fixed
**Created**: 2026-03-18
**Score**: 6/10 production readiness

---

## Verdict

The codebase is **worth building on**. No portions need a rewrite. Architecture is clean, separation of concerns is good, FreeRTOS usage is mostly correct. ~1000 lines of application logic, lean and readable. The critical issues are all fixable with targeted changes.

---

## Top 5 Critical Issues

### 1. CRITICAL: `config_remote.cpp` may be committed with real credentials
**File:** `src/config_remote.cpp`

WiFi SSID, password, and HA token are hardcoded. The file IS in `.gitignore` already, but if someone accidentally force-adds it, credentials are exposed.

**Fix:** Verify `.gitignore` coverage (already present). Long-term: consider NVS storage or captive portal provisioning.

**Status:** `.gitignore` already covers this. Low risk.

### 2. CRITICAL: UI task stack size is dangerously small (2048 bytes)
**File:** `main.cpp:71` — `xTaskCreate(ui_task, "ui", 2048, ...)`

The UI task calls FastEPD display operations (fullUpdate, fillScreen, drawSprite), font rendering, snprintf in battery indicator. 2048 bytes is almost certainly insufficient and risks stack overflow.

**Fix:** Increase to 4096 or 8192. Use `uxTaskGetStackHighWaterMark()` on device to measure actual usage.

### 3. HIGH: HA task 10ms polling overrides light sleep
**File:** `home_assistant.cpp:368` — `vTaskDelay(pdMS_TO_TICKS(10))`

The HA task loops every 10ms calling `wsClient->loop()`. This means the CPU wakes from light sleep every 10ms even when completely idle — effectively defeating the idle hook light sleep from Phase 2. The `SLEEP_WAKE_INTERVAL_MS` (5s) timer wake is irrelevant because this 10ms delay always fires first.

**Fix:** Replace the 10ms poll with `ulTaskNotifyTake` using `SLEEP_WAKE_INTERVAL_MS` as timeout. Only call `wsClient->loop()` when woken by notification or timeout. This is the single biggest power savings fix remaining.

### 4. HIGH: SSL root CA not passed to WebSocket client
**File:** `home_assistant.cpp:342` — `wsClient->beginSSL(host.c_str(), port, path.c_str(), nullptr, "")`

`config->root_ca` (the ISRG_ROOT_X1 cert) is configured but never passed to the WebSocket SSL connection. The `nullptr` means SSL cert validation is either skipped or fails, depending on library defaults.

**Fix:** Pass `config->root_ca` as the fingerprint/CA parameter to `beginSSL()`.

### 5. MEDIUM: Duplicate entity state in HA context vs store
**File:** `home_assistant.cpp:11-26`

The HA context maintains its own `entity_states[]` and `entity_values[]` alongside the store's `entities[]`. These can diverge (by design with the debounce feature). This duplication makes correctness hard to reason about and is the root of the FIXME at `constants.h:27`.

**Fix:** Consolidate into single source of truth with `target_value` and `authoritative_value` fields in `HomeAssistantEntity`.

---

## Top 5 Nice-to-Fix Issues

### 1. `new` allocations in tasks never freed
**Files:** `home_assistant.cpp:319,338`, `touch.cpp:27`

`hass`, `wsClient`, `ui_state` allocated with `new` in infinite-loop tasks. Not a leak (tasks never exit), but prevents clean shutdown and triggers static analysis warnings.

**Fix:** Allocate on stack where size permits, or document the intentional leak.

### 2. Magic numbers in widget drawing code
**Files:** `Slider.cpp`, `OnOffButton.cpp`

Sprite sizes `+2`, fillCircle offset `-1`, label position `30` and `-5`, `0xf` for 4BPP white. The `0xf` assumption is especially fragile.

**Fix:** Named constants.

### 3. `screen.cpp` unnecessary int16_t casts
**File:** `screen.cpp:12-15`

Casts `uint16_t` to `int16_t` then back to `uint16_t` via Rect initialization. Confusing and technically a narrowing issue.

**Fix:** Remove casts.

### 4. No explicit WiFi reconnection logic
**File:** `wifi.cpp`

Relies on Arduino WiFi auto-reconnect (enabled by default but implicit).

**Fix:** Add `WiFi.setAutoReconnect(true)` explicitly.

### 5. `accumulate_damage` checks `<= 0` on unsigned values
**File:** `ui.cpp:19`

`Rect` members are `uint16_t`, so `r.w <= 0` — the `< 0` part is dead code.

**Fix:** Change to `== 0` or leave as-is (harmless).

---

## Assessment by Category

### Memory: GOOD
- cJSON usage is correct — all `cJSON_PrintUnformatted` paired with `cJSON_free`, all `cJSON_CreateObject` paired with `cJSON_Delete`
- Sprite memory allocated once, never freed — acceptable for always-running device
- `String` objects in `parse_url` are short-lived and called once — minimal fragmentation

### Thread Safety: MOSTLY GOOD
- Store mutex consistently applied
- SharedUIState versioning pattern is clean
- Task handles (`ui_task`, `home_assistant_task`) read without mutex but set once before tasks launch — safe in practice
- `ws_hass_ctx` global pointer is fragile but works due to single-task WebSocket usage

### Power Management: NEEDS WORK
- Light sleep idle hook: good
- GPIO wake for touch: good
- WiFi modem sleep: good
- **HA task 10ms poll is the main power issue** — see Critical #3

### Architecture: GOOD
- Clean separation: store, UI state, screen, widgets, managers
- Widget abstraction is extensible
- Partial update with damage accumulation is well-designed
- The 1BPP backup plane for fast updates is efficient
