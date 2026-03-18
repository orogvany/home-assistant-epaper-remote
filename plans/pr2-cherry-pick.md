# PR #2 Cherry-Pick Plan

**Status**: DRAFT — Awaiting alignment
**Created**: 2026-03-17
**Source**: `pr/2-entity-types-and-configurator` (saved
from [ugomeda/home-assistant-epaper-remote#2](https://github.com/ugomeda/home-assistant-epaper-remote/pull/2))
**Author note**: PR was written by a non-developer using Claude Code. Quality varies — some parts are clean, others have
real bugs.

---

## Candidate Features

### 1. New Entity Command Types

**What it offers**: Support for controlling more HA device types from the remote. Currently the remote can only control
lights (brightness), fans (speed), switches (on/off), and automations (on/off). This adds:

| Entity Type   | Control            | Widget Type | Use Case                                             |
|---------------|--------------------|-------------|------------------------------------------------------|
| Cover/Blinds  | Position 0-100%    | Slider      | Open/close blinds, shades, garage doors              |
| Scene         | Activate (one-tap) | Button      | "Movie mode", "Goodnight", etc.                      |
| Script        | Run (one-tap)      | Button      | Run HA automations/scripts                           |
| Lock          | Lock/Unlock toggle | Button      | Door locks                                           |
| Media Player  | Volume 0-100%      | Slider      | Speaker/TV volume                                    |
| Media Player  | Play/Pause toggle  | Button      | Play/pause media                                     |
| Input Number  | Value 0-100%       | Slider      | HA helper sliders (e.g., thermostat setpoint helper) |
| Input Boolean | On/Off toggle      | Button      | HA helper toggles                                    |
| Vacuum        | Start/Stop/Dock    | Button      | Robot vacuums                                        |

**Value assessment**: HIGH. These are all common HA entities that people would want on a physical remote. The
implementation follows existing patterns (just new cases in a switch statement for sending commands + new state strings
for parsing). Covers and media player volume are probably the most universally useful.

**Quality**: Good. The command-sending code is clean — each type uses the correct HA service domain and service name.
The state parsing additions (locked/unlocked/playing/paused/open/closed/idle) follow the existing pattern.

**Effort to cherry-pick**: LOW. It's adding enum values and switch cases to existing code. We'd need to adapt it to our
WebSockets-based `home_assistant.cpp` (the PR was written against the old `esp_websocket_client` version).

**Recommendation**: TAKE — adapt to our codebase.

---

### 2. Bugfix: `last_command_sent_at_ms` Zero-Check

**What it offers**: Fixes a real bug on `main` where the first server update for an entity after boot could be
incorrectly ignored. The `last_command_sent_at_ms` array is initialized to 0, and `(now - 0)` with unsigned arithmetic
wraps to a huge number that's less than the ignore delay threshold, causing the update to be silently dropped.

**Value assessment**: HIGH. This is a correctness fix — without it, the remote may show stale/zero values for entities
until the next HA state change.

**Quality**: Good. Simple one-line guard: `if (hass->last_command_sent_at_ms[widget_idx] != 0 && ...)`.

**Effort to cherry-pick**: TRIVIAL. One line change.

**Recommendation**: TAKE.

---

### 3. Slider Tap-to-Toggle

**What it offers**: Tapping the icon area of a slider toggles between 0 (off) and 100 (full on), instead of requiring
the user to drag the slider all the way. For example, tapping the lightbulb icon on a dimmer slider instantly turns the
light fully on or off.

**Value assessment**: MEDIUM-HIGH. Good UX improvement — saves the awkward gesture of dragging from 0 to 100 just to
turn something on/off.

**Quality**: Clean. ~12 lines added to `Slider.cpp`'s `getValueFromTouch` — checks if touch is in the icon area (left
side of slider) and returns 100 or 0 based on current state.

**Effort to cherry-pick**: LOW. Self-contained change in one file.

**Recommendation**: TAKE.

---

### 4. `MAX_ENTITIES` Bump (8 → 16)

**What it offers**: Allows more than 8 HA entities to be configured on the remote. Currently limited to 8 entities
total (not 8 per screen — 8 total across all widgets).

**Value assessment**: DEPENDS. With only one screen and max 8 widgets, 8 entities is already sufficient — you can't
display more than 8 widgets anyway. However, if we ever add multi-page support, or if entities are shared across widgets
differently, 16 gives headroom. The thermostat widget in the PR consumes 2 entity slots per thermostat, which is what
motivated this increase.

Each entity adds ~30 bytes of RAM to the store (entity_id pointer, state, value, command state, timing), so 16 vs 8
adds ~240 bytes — negligible.

**Quality**: One-line change in constants.h.

**Effort to cherry-pick**: TRIVIAL.

**Recommendation**: TAKE — it's free insurance. Bump to 16.

---

### 5. Multi-Page Navigation (Carousel)

**What it offers**: Multiple screens/pages of widgets, navigated by left/right arrows and page dots at the bottom.
Allows organizing entities by room or function (e.g., page 1 = living room lights, page 2 = bedroom, page 3 = media).

**Value assessment**: MEDIUM. On a 960-pixel tall screen with widgets that are ~170px each (sliders) or ~100px (
buttons), you can fit ~4-5 widgets per screen. For a house with many controllable devices, multiple pages would be
useful. But for a bedside remote controlling a few lights and a fan, one page is enough.

**Quality concerns**:

- Thread-safety issue: `current_page` written by touch task, read by UI task, no mutex
- `MAX_PAGES = 20` is excessive (4-6 is plenty)
- Page transitions use full `CLEAR_SLOW` refresh (~1-2 seconds)
- Navigation bar steals 60px at the bottom with no validation against widget overlap

**Effort to cherry-pick**: MEDIUM. Changes span screen.h/cpp, touch.cpp, ui.cpp. Would need thread-safety fix and
constant adjustment.

**Recommendation**: DEFER. Not needed for the power management work. Useful feature for later, but needs thread-safety
fixes and the 60px nav bar changes widget layout assumptions. Worth revisiting after the core power work is done.

---

### 6. Tab Navigation

**What it offers**: Alternative to carousel — labeled tabs at the top/bottom for direct page jumping. Includes scrolling
tabs with arrow controls for overflow.

**Value assessment**: LOW. Tabs are more complex than carousel, and the implementation has quality issues (approximate
touch detection based on equal-width assumption that doesn't match rendered widths, complex scrolling logic). Carousel
is simpler and sufficient for an e-ink remote.

**Quality concerns**: Same thread-safety issues as carousel, plus approximate touch targets and scattered magic numbers.

**Effort to cherry-pick**: HIGH relative to value. Lots of rendering and touch logic.

**Recommendation**: SKIP. If we do multi-page, carousel is enough.

---

### 7. Thermostat Widget

**What it offers**: Climate control from the remote — mode button (off/heat/cool/auto) and temperature +/- buttons with
current/target temp display.

**Value assessment**: MEDIUM. Useful for homes with smart thermostats, but it's a niche use case for a bedside/wall
remote. Most people control thermostats from the HA app or a dedicated thermostat display.

**Quality concerns** (significant):

- Dual-entity slot trick is fragile — same widget pointer in two consecutive slots
- Scattered `if (getType() == WidgetType::Thermostat)` branches in touch.cpp and ui.cpp
- `int8_t` overflow for temperature values
- `uint8_t` can't represent negative Celsius temperatures
- `partialDraw` just calls `fullDraw` (defeats e-ink partial update purpose)
- 8 sprite objects per thermostat (RAM hungry)
- Hardcoded Fahrenheit default (70°F)
- Thread-unsafe `current_temperature` access

**Effort to cherry-pick**: HIGH. Would need redesign of the dual-slot approach into a proper single-widget
implementation. The climate entity parsing also needs rework for type safety.

**Recommendation**: SKIP for now. If someone needs thermostat control, it should be redesigned from scratch rather than
carrying the fragile dual-slot pattern. The climate entity command types from item #1 can still be taken — the
thermostat *widget* is the problem, not the HA command support.

---

## Proposed Cherry-Pick Order

### Phase A — Quick wins ✅ COMPLETE

1. **Bugfix: `last_command_sent_at_ms` zero-check** ✅ — trivial, fixes real bug
2. **Slider tap-to-toggle** ✅ — small, clean UX improvement
3. **`MAX_ENTITIES` bump to 16** ✅ — one line, free headroom

### Phase B — New entity types (separate branch/PR)

4. **New entity command types** — adapt to our WebSockets-based home_assistant.cpp
    - Includes new `CommandType` enum values
    - Includes state parsing expansion
    - Includes command sending for all new types
    - Needs careful adaptation since our HA manager was rewritten

### Phase C — Future consideration

5. **Multi-page carousel navigation** — defer until power work is complete, fix thread-safety first
6. **Thermostat widget** — redesign from scratch if needed
7. **Tab navigation** — skip unless carousel proves insufficient

---

## Open Questions — RESOLVED

1. ~~Do you actually have covers/blinds, media players, locks, or other entity types you'd want to control?~~ **Yes — media players, potentially others later.**
2. ~~Do you need more than one screen of widgets for your setup?~~ **No — keeping it concise/minimalist. Single page.**
3. ~~Is thermostat control something you'd use on this device?~~ **No — skip thermostat.**

**Implications**: Phase B (new entity types) is worth doing for media player support. Multi-page (Phase C #5) and thermostat (#6, #7) are confirmed skipped.
