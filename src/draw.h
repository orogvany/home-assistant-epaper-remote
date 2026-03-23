#pragma once

#include <FastEPD.h>
#include <cstddef>
#include <cstdint>

void drawCenteredIconWithText(FASTEPD* epaper, const uint8_t* icon, const char* const* lines, uint8_t line_spacing,
                              uint8_t icon_spacing);

void drawStatusBar(FASTEPD* epaper, bool wifi_connected, bool ha_connected,
                   bool alexa_connected, bool alexa_enabled,
                   uint8_t battery_pct, bool battery_charging, bool show_battery);
void drawGearIcon(FASTEPD* epaper);
void drawSettingsMenu(FASTEPD* epaper);
void drawWifiSetupScreen(FASTEPD* epaper, const char* ap_name);
void drawConfigureScreen(FASTEPD* epaper, const char* device_ip);
void drawAboutScreen(FASTEPD* epaper, const char* version, const char* wifi_ssid,
                     const char* ha_url, bool ha_connected, uint8_t battery_pct);
void drawPinEntryScreen(FASTEPD* epaper, int digits_entered, bool wrong_pin);

// PIN pad touch detection - returns digit 0-9, 10 = delete, -1 = nothing
int getPinPadTouched(uint16_t touch_x, uint16_t touch_y);
void drawIdleScreen(FASTEPD* epaper, int16_t offset_x, int16_t offset_y);

// Touch hit areas for settings menu items
constexpr uint16_t SETTINGS_ITEM_HEIGHT = 80;
constexpr uint16_t SETTINGS_ITEM_MARGIN = 30;
constexpr uint16_t SETTINGS_ITEM_Y_START = 120;
constexpr uint16_t GEAR_ICON_SIZE = 64;
constexpr uint16_t GEAR_MARGIN = 16;

bool isGearIconTouched(uint16_t touch_x, uint16_t touch_y);
int getSettingsMenuItemTouched(uint16_t touch_x, uint16_t touch_y);
