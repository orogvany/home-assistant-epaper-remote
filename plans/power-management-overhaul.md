# Power Management Overhaul

**Status**: IN PROGRESS — Phase 0+1 complete, starting Phase 2
**Created**: 2026-03-17
**Branch**: `feature/power-management`

---

## Context

The device currently draws ~154 mA continuously (WiFi active, CPU running, display power on, touch polling). With a 1800 mAh battery, that's roughly **11-12 hours** — matching the README's "a few hours" claim.

### Hardware Constraints (M5Paper S3)

- **GPIO 48 (touch INT) is NOT an RTC GPIO** — deep sleep with touch wake is **impossible**. Light sleep is the only viable path for touch-wake.
- **einkPower() and GT911 share a power rail** — calling `einkPower(false)` kills touch. Must keep this rail on or find a workaround.
- **GT911 has a doze/gesture mode** at ~0.78 mA that still detects touch and fires INT.
- **E-ink retains image with power off** — display content persists indefinitely without voltage.
- **PMS150G power management IC is OTP** — its behavior cannot be reprogrammed.
- **ESP32 deep sleep with main power on draws ~5.1 mA** due to other components on the power rail (gyro, etc).

### Target Power Budget

| Component | Current (now) | Target | Method |
|-----------|--------------|--------|--------|
| ESP32-S3 CPU | ~40 mA active | ~0.28 mA avg | Auto light-sleep with tickless idle |
| WiFi radio | ~100-200 mA | ~2.45 mA avg | Modem-sleep + auto light-sleep (DTIM wake) |
| GT911 touch | ~10-15 mA active | ~0.78 mA | Doze mode (auto after idle timeout) |
| Display DC/DC | ~1-3 mA standby | ~0.02 mA | Sleep mode between refreshes |
| Other board components | ~5 mA | ~5 mA | Cannot change (gyro, RTC, etc.) |
| **Total** | **~154 mA** | **~8-9 mA** | |

**Projected battery life**: 1800 mAh / ~8.5 mA ≈ **210 hours (~9 days)**

With aggressive idle WiFi disconnect (Phase 3), idle current could drop to ~6 mA, yielding **~12 days**.

---

## Phase 0: Battery Voltage Monitoring ✅ COMPLETE

**Goal**: Add battery voltage reading as both a serial log diagnostic and an on-screen indicator. Provides the measurement tool we need to validate every subsequent phase.

**Also completed**: Replaced `esp_websocket_client` (ESP-IDF component) with `links2004/WebSockets` (Arduino native PlatformIO library) to fix build dependency issues. Simplified `home_assistant.cpp` by removing manual WebSocket frame reassembly.

### 0A. Battery ADC Reading

- **GPIO 3** is the battery voltage ADC input on M5Paper S3
- **2:1 voltage divider** — multiply `analogReadMilliVolts(3)` by 2 to get actual battery mV
- **GPIO 4** is charge detection (LOW = charging)
- No external hardware or M5Unified dependency needed — raw ADC with ESP32-S3 factory calibration

### 0B. Periodic Reading Task

- Read battery voltage every 5 minutes (configurable via `BATTERY_READ_INTERVAL_MS` in constants.h)
- Log to serial: `ESP_LOGI(TAG, "Battery: %d mV (%d%%)", voltage_mv, percentage)`
- Single ADC read takes microseconds — negligible power/CPU impact

### 0C. On-Screen Battery Indicator

- Small, tasteful battery percentage display in a corner of the main screen
- Only update on screen when the value actually changes (avoid unnecessary redraws)
- Show charging indicator when GPIO 4 is LOW
- Integrate into the existing UI task's refresh cycle — no additional display refreshes needed

### 0D. Voltage-to-Percentage Mapping

LiPo discharge curve is non-linear. Use a lookup table with interpolation:

| Voltage (mV) | % |
|---|---|
| 4200 | 100 |
| 4150 | 95 |
| 4110 | 90 |
| 4080 | 85 |
| 4020 | 80 |
| 3980 | 70 |
| 3950 | 60 |
| 3910 | 50 |
| 3870 | 40 |
| 3830 | 30 |
| 3790 | 20 |
| 3750 | 15 |
| 3700 | 10 |
| 3600 | 5 |
| 3300 | 0 |

### Validation

- Compare readings against a multimeter on the battery terminals
- Verify charging detection works when USB is plugged in
- Confirm on-screen indicator renders cleanly without disrupting widget layout

---

## Phase 1: WiFi Power Save + CPU Frequency Scaling ✅ COMPLETE

**Goal**: Lowest-hanging fruit. No architectural changes. Drop from ~154 mA to ~40-60 mA.

### 1A. Enable WiFi Modem Sleep ✅

In `managers/wifi.cpp`, after `WiFi.begin()`:

- Call `WiFi.setSleep(WIFI_PS_MIN_MODEM)` to enable modem power save
- WiFi radio sleeps between DTIM beacons (~100-300 ms intervals set by AP)
- WebSocket connection is maintained — no application changes needed
- Expected savings: WiFi drops from ~100-200 mA constant to ~20 mA average

### 1B. Enable CPU Dynamic Frequency Scaling (DFS) ✅

- Configure `esp_pm_config_t` with `max_freq_mhz = 160`, `min_freq_mhz = 40`
- CPU automatically scales down when no tasks need full speed
- WiFi/SPI drivers acquire frequency locks when they need full speed
- Expected savings: ~10-20 mA during idle periods

### 1C. Reduce Touch Polling Frequency When Idle ✅

In `managers/touch.cpp`:

- Current: polls every 200 ms when idle
- Change: increase to 500 ms when idle (touch latency from 200 ms to 500 ms is barely perceptible for an e-ink remote)
- Active polling (25 ms) stays the same for responsive slider tracking

### Validation

- Measure current draw before and after (USB power meter or INA219)
- Verify WebSocket connection stays alive through modem-sleep cycles
- Verify touch responsiveness is acceptable at 500 ms idle polling
- A/B comparison: `main` branch vs `feature/power-management`

### Risks

- Some WiFi APs have DTIM settings that don't play well with modem-sleep — may need configuration guidance
- DFS may add interrupt latency (up to 40 µs on frequency switch) — shouldn't matter for this use case

---

## Phase 2: Auto Light-Sleep + Interrupt-Driven Touch

**Goal**: Major power reduction. CPU sleeps automatically between events. Drop to ~8-10 mA average.

### 2A. Enable FreeRTOS Tickless Idle + Auto Light-Sleep

- Enable `CONFIG_FREERTOS_USE_TICKLESS_IDLE` in sdkconfig/platformio.ini
- Configure `esp_pm_config_t` with `light_sleep_enable = true`
- When all FreeRTOS tasks are blocked (waiting on semaphores, notifications, delays), the system automatically enters light sleep
- CPU drops from ~40 mA to ~0.28 mA during sleep periods
- System auto-wakes for WiFi beacons, timer events, and GPIO interrupts

### 2B. Switch Touch from Polling to Interrupt-Driven

This is the critical change that makes light sleep effective:

- Configure GPIO 48 (TOUCH_INT) as a light-sleep wake source using `gpio_wakeup_enable(GPIO_NUM_48, GPIO_INTR_LOW_LEVEL)`
- Replace the touch task's `vTaskDelay(200)` idle polling with a semaphore wait
- GT911 fires INT on touch → wakes CPU from light sleep → touch task runs
- GT911 automatically enters doze mode (~0.78 mA) after no touch for a few seconds

Touch task flow becomes:
1. Block on a binary semaphore (CPU can sleep)
2. GPIO ISR on pin 48 gives the semaphore
3. Touch task wakes, reads touch data, processes widgets
4. After `TOUCH_RELEASE_TIMEOUT_MS` with no new touch, go back to blocking on semaphore

### 2C. Optimize Display Refresh Power

- Pass `bKeepOn = false` to `fullUpdate()` and `partialUpdate()` when no immediate follow-up refresh is expected
- This lets the e-ink DC/DC circuit power down between refreshes
- The display image persists (e-ink property)
- Re-evaluate the 5-second forced full refresh (`DISPLAY_FULL_REDRAW_TIMEOUT_MS`) — consider increasing to 15-30 seconds or making it conditional on whether partial updates actually occurred

### 2D. Reduce Unnecessary Logging

- `ESP_LOGI` calls throughout the codebase keep UART active, which prevents deeper sleep
- Gate verbose logging behind `CORE_DEBUG_LEVEL` (already partially done via platformio.ini)
- Ensure production builds use `CORE_DEBUG_LEVEL=0`

### Validation

- Measure average current with oscilloscope or power profiler (Nordic PPK2 ideal)
- Verify light-sleep entry/exit with `esp_sleep_get_wakeup_cause()`
- Verify touch responsiveness — interrupt-driven should actually feel _faster_ than 200 ms polling
- Verify display updates still work correctly after light-sleep wake
- Verify WebSocket stays connected through light-sleep cycles
- Soak test: leave running overnight, measure battery drain %

### Risks

- Auto light-sleep interacts with all peripherals — need to verify I2C (touch) and SPI (display) aren't disrupted
- Some ESP-IDF components may hold power management locks that prevent sleep — need to audit
- GT911 INT pin behavior needs empirical verification on this specific board
- Possible need for sdkconfig changes that affect other parts of the build

### Dependencies

- Phase 1 must be stable first (DFS is prerequisite for auto light-sleep)

---

## Phase 3: Idle Detection + WiFi Disconnect

**Goal**: For extended idle periods, disconnect WiFi entirely for deeper power savings.

### 3A. Implement Idle Timer

- Track last touch timestamp globally
- After configurable idle timeout (e.g., 5 minutes), transition to "idle" state
- Display an idle indicator (e.g., dim status bar or subtle icon) before disconnecting

### 3B. WiFi Disconnect on Extended Idle

- After idle timeout: `WiFi.disconnect()` + `WiFi.mode(WIFI_OFF)`
- WiFi radio off saves the ~2.45 mA modem-sleep average
- Device draws only: GT911 doze (~0.78 mA) + board standby (~5 mA) + ESP32 light-sleep (~0.28 mA) ≈ **~6 mA**
- Display retains last known state (e-ink)

### 3C. WiFi Fast Reconnect on Touch Wake

On touch event after idle:
1. Show "Reconnecting..." on display (fast partial update)
2. `WiFi.mode(WIFI_STA)` + `WiFi.begin()` with saved channel/BSSID for fast reconnect (~0.8-1.2 s)
3. WebSocket reconnect + HA auth + entity subscribe (~0.5-1.5 s with TLS)
4. Total reconnect: **~1.5-3 seconds** — acceptable for an e-ink remote

Save AP channel and BSSID to RTC memory (survives light sleep) for fast reconnect.

### 3D. Stale State Indication

- When WiFi is disconnected, widget values on screen are stale
- On reconnect, refresh all entity states from HA subscription
- Consider showing a visual indicator that values may be stale until reconnect completes

### Validation

- Measure idle current with WiFi off
- Time the reconnect flow end-to-end
- Verify entity states are correctly refreshed after reconnect
- User experience test: is 2-3 second reconnect acceptable?

### Risks

- User touches a button during reconnect — need to queue commands and send after connection is up (store already supports pending commands)
- If the user expects instant response, the 2-3 second reconnect may feel sluggish
- Need to handle edge case where reconnect fails (show error state, retry)

---

## Phase 4: Deep Sleep for Long Idle (Optional/Experimental)

**Goal**: Maximize battery life for overnight/unused periods. This phase has significant trade-offs.

### Constraints

- GPIO 48 (touch INT) is NOT an RTC GPIO → **cannot wake from deep sleep on touch**
- Deep sleep with main power on still draws ~5.1 mA (worse than Phase 3 light-sleep + WiFi off at ~6 mA)
- Deep sleep = full reboot on wake, losing all state

### 4A. Timer-Based Deep Sleep (if viable)

- After very long idle (e.g., 30+ minutes with no touch), enter deep sleep with RTC timer wake
- Wake every N minutes to check... nothing (no touch wake possible)
- This is only useful if we can get below the ~5.1 mA deep sleep floor — unlikely without hardware mods

### 4B. PMS150G Power-Off (Nuclear Option)

- The PMS150G can cut main power, dropping to ~9.28 µA
- Wake requires physical button press (not touch)
- Essentially "turning off" the device
- Could be triggered after 1+ hour idle as a battery-saver mode

### Assessment

**Phase 4 is likely not worth pursuing** given the hardware constraints. Light sleep (Phase 2) + WiFi disconnect (Phase 3) at ~6 mA already gives ~12 days on battery. Deep sleep can't improve on this due to the 5.1 mA floor from other board components, and it loses touch wake entirely. The PMS150G power-off is viable as a "shutdown" feature but not as a sleep mode.

---

## Implementation Order & Branch Strategy

```
main (clean baseline for A/B comparison)
 └── feature/power-management
      ├── Phase 0 commits (battery ADC reading, on-screen indicator)
      ├── Phase 1 commits (WiFi modem-sleep, DFS, polling reduction)
      ├── Phase 2 commits (auto light-sleep, interrupt touch, display power)
      └── Phase 3 commits (idle detection, WiFi disconnect/reconnect)
```

Each phase should be a set of atomic commits that can be individually reverted if issues arise. Tag after each phase is stable for easy comparison:

- `v0.0-phase0` — Battery voltage monitoring + on-screen indicator
- `v0.1-phase1` — WiFi + CPU power save
- `v0.2-phase2` — Light sleep + interrupt touch
- `v0.3-phase3` — Idle WiFi disconnect

### sdkconfig / platformio.ini Changes Required

Phase 2 will require build configuration changes:
- `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`
- `CONFIG_PM_ENABLE=y`
- Possibly `CONFIG_ESP_WIFI_SLP_IRAM_OPT=y` for WiFi light-sleep optimization
- These need to be added via `board_build.partitions` or `build_flags` in platformio.ini

---

## Open Questions

1. ~~**Do we have a way to measure current draw?**~~ **RESOLVED** — Phase 0 adds software-based battery voltage monitoring via GPIO 3 ADC. Charge to 100%, run for 1-2 hours, compare voltage drop between branches.
2. **What DTIM interval is your WiFi AP configured for?** This affects modem-sleep effectiveness.
3. **Is 500 ms touch latency acceptable for idle state?** (Phase 1C)
4. ~~**Is 2-3 second reconnect on wake acceptable?**~~ **RESOLVED** — Yes, as long as touch is instant and commands queue. Screen must never freeze; commands fire once WebSocket reconnects.
5. **Should we implement PMS150G power-off as a "shutdown" feature?** (Phase 4B)
6. **Are there any entities that need real-time monitoring** (e.g., alarm state) that would argue against WiFi disconnect?

---

## References

- [ESP-IDF Power Management (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/power_management.html)
- [ESP-IDF Sleep Modes (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html)
- [ESP-IDF WiFi Low Power Mode](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-wifi.html)
- [GT911 Programming Guide](https://www.crystalfontz.com/controllers/GOODIX/GT911ProgrammingGuide/478/)
- [M5PaperS3 Documentation](https://docs.m5stack.com/en/core/papers3)
- [FastEPD GitHub](https://github.com/bitbank2/FastEPD)
