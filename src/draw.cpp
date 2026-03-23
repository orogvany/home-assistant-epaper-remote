#include "draw.h"
#include "assets/icons.h"
#include "assets/Montserrat_Regular_26.h"
#include "boards.h"
#include <FastEPD.h>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstdio>

void drawCenteredIconWithText(FASTEPD* epaper, const uint8_t* icon, const char* const* lines, uint8_t line_spacing,
                              uint8_t icon_spacing) {
    BB_RECT rect;
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);

    // Figure out the height of the text
    uint16_t text_height = 0;
    for (size_t i = 0; lines[i] != nullptr; ++i) {
        epaper->getStringBox(lines[i], &rect);
        text_height += rect.h;
        if (i > 0) {
            text_height += line_spacing;
        }
    }

    // Draw the icon
    const int icon_x = DISPLAY_WIDTH / 2 - 256 / 2;
    uint16_t cursor_y = DISPLAY_HEIGHT / 2 - (256 + icon_spacing + text_height) / 2;
    epaper->loadBMP(icon, icon_x, cursor_y, 0xf, BBEP_BLACK);

    // Draw each line
    cursor_y += icon_spacing + 256;
    for (size_t i = 0; lines[i] != nullptr; ++i) {
        epaper->getStringBox(lines[i], &rect);
        const int text_x = DISPLAY_WIDTH / 2 - rect.w / 2;

        epaper->setCursor(text_x, cursor_y);
        epaper->write(lines[i]);

        cursor_y += rect.h + line_spacing;
    }
}

constexpr uint16_t BATT_ICON_W = 28;
constexpr uint16_t BATT_ICON_H = 14;
constexpr uint16_t BATT_TIP_W = 3;
constexpr uint16_t BATT_BORDER = 2;
constexpr uint16_t BATT_MARGIN = 16;


constexpr uint16_t STATUS_ICON_SIZE = 24;
constexpr uint16_t STATUS_ICON_GAP = 6;

void drawStatusBar(FASTEPD* epaper, bool wifi_connected, bool ha_connected,
                   bool alexa_connected, bool alexa_enabled,
                   uint8_t battery_pct, bool battery_charging, bool show_battery) {
    char label[8];
    BB_RECT text_rect = {};
    uint16_t batt_section_w = 0;

    if (show_battery) {
        if (battery_charging) {
            batt_section_w = BATT_ICON_W + BATT_TIP_W + 6 + 14;
        } else {
            snprintf(label, sizeof(label), "%d%%", battery_pct);
            epaper->setFont(Montserrat_Regular_26);
            epaper->setTextColor(BBEP_BLACK);
            epaper->getStringBox(label, &text_rect);
            batt_section_w = BATT_ICON_W + BATT_TIP_W + 6 + text_rect.w;
        }
    }

    // Count status icons that need to be shown
    uint8_t icon_count = 0;
    if (!wifi_connected) icon_count++;
    if (!ha_connected) icon_count++;
    if (alexa_enabled && !alexa_connected) icon_count++;

    uint16_t status_section_w = 0;
    if (icon_count > 0) {
        status_section_w = icon_count * STATUS_ICON_SIZE + (icon_count - 1) * STATUS_ICON_GAP;
        if (show_battery) {
            status_section_w += STATUS_ICON_GAP * 2;
        }
    }

    uint16_t total_w = status_section_w + batt_section_w;
    uint16_t x = DISPLAY_WIDTH - BATT_MARGIN - total_w;
    uint16_t y = BATT_MARGIN;

    // Clear the entire status bar area
    uint16_t clear_h = std::max({(int)STATUS_ICON_SIZE, (int)BATT_ICON_H, text_rect.h}) + 4;
    epaper->fillRect(x - 4, y - 2, total_w + 8, clear_h, 0xf);

    // Draw status icons (leftmost), vertically centered on same baseline as battery
    uint16_t icon_y = y;
    if (!wifi_connected) {
        epaper->loadBMP(status_wifi_off, x, icon_y, 0xf, BBEP_BLACK);
        x += STATUS_ICON_SIZE + STATUS_ICON_GAP;
    }
    if (!ha_connected) {
        epaper->loadBMP(status_ha_off, x, icon_y, 0xf, BBEP_BLACK);
        x += STATUS_ICON_SIZE + STATUS_ICON_GAP;
    }
    if (alexa_enabled && !alexa_connected) {
        epaper->loadBMP(status_alexa_off, x, icon_y, 0xf, BBEP_BLACK);
        x += STATUS_ICON_SIZE + STATUS_ICON_GAP;
    }
    if (icon_count > 0 && show_battery) {
        x += STATUS_ICON_GAP;
    }

    // Draw battery (rightmost)
    if (show_battery) {
        epaper->drawRect(x, y, BATT_ICON_W, BATT_ICON_H, BBEP_BLACK);
        epaper->fillRect(x + BATT_ICON_W, y + 3, BATT_TIP_W, BATT_ICON_H - 6, BBEP_BLACK);

        uint16_t fill_w = (BATT_ICON_W - 2 * BATT_BORDER) * battery_pct / 100;
        if (fill_w > 0) {
            epaper->fillRect(x + BATT_BORDER, y + BATT_BORDER, fill_w, BATT_ICON_H - 2 * BATT_BORDER, BBEP_BLACK);
        }

        uint16_t after_icon_x = x + BATT_ICON_W + BATT_TIP_W + 6;
        if (battery_charging) {
            uint16_t bx = after_icon_x;
            uint16_t by = y;
            epaper->drawLine(bx + 5, by,     bx + 1, by + 6, BBEP_BLACK);
            epaper->drawLine(bx + 6, by,     bx + 2, by + 6, BBEP_BLACK);
            epaper->drawLine(bx + 1, by + 6, bx + 4, by + 6, BBEP_BLACK);
            epaper->drawLine(bx + 4, by + 6, bx,     by + 13, BBEP_BLACK);
            epaper->drawLine(bx + 5, by + 6, bx + 1, by + 13, BBEP_BLACK);
        } else {
            epaper->setFont(Montserrat_Regular_26);
            epaper->setTextColor(BBEP_BLACK);
            epaper->setCursor(after_icon_x, y + (BATT_ICON_H + text_rect.h) / 2 - 2);
            epaper->write(label);
        }
    }
}

void drawGearIcon(FASTEPD* epaper) {
    uint16_t x = DISPLAY_WIDTH - GEAR_MARGIN - GEAR_ICON_SIZE;
    uint16_t y = DISPLAY_HEIGHT - GEAR_MARGIN - GEAR_ICON_SIZE;
    epaper->loadBMP(chrome_cog_outline, x, y, 0xf, BBEP_BLACK);
}

bool isGearIconTouched(uint16_t touch_x, uint16_t touch_y) {
    uint16_t x = DISPLAY_WIDTH - GEAR_MARGIN - GEAR_ICON_SIZE;
    uint16_t y = DISPLAY_HEIGHT - GEAR_MARGIN - GEAR_ICON_SIZE;
    // Generous touch area (icon + margin)
    return touch_x >= (x - 10) && touch_x <= DISPLAY_WIDTH &&
           touch_y >= (y - 10) && touch_y <= DISPLAY_HEIGHT;
}

// Settings menu items - returns 0-based index or -1 for back/none
// 0 = Configure WiFi, 1 = Configure HA, 2 = About, -1 = Back
int getSettingsMenuItemTouched(uint16_t touch_x, uint16_t touch_y) {
    // Back button - top-left area (generous hit box over "< Back" text)
    if (touch_x < 200 && touch_y < 80) return -1;

    // Menu items
    for (int i = 0; i < 3; i++) {
        uint16_t item_y = SETTINGS_ITEM_Y_START + i * (SETTINGS_ITEM_HEIGHT + 10);
        if (touch_y >= item_y && touch_y < item_y + SETTINGS_ITEM_HEIGHT &&
            touch_x >= SETTINGS_ITEM_MARGIN && touch_x < DISPLAY_WIDTH - SETTINGS_ITEM_MARGIN) {
            return i;
        }
    }
    return -2; // Nothing hit
}

void drawSettingsMenu(FASTEPD* epaper) {
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);

    // Back button top-left, Settings title right-justified, same line
    epaper->setCursor(SETTINGS_ITEM_MARGIN, 40);
    epaper->write("< Back");

    BB_RECT rect;
    epaper->getStringBox("Settings", &rect);
    epaper->setCursor(DISPLAY_WIDTH - SETTINGS_ITEM_MARGIN - rect.w, 40);
    epaper->write("Settings");

    // Menu items with borders
    const char* items[] = {"WiFi Setup", "Configure", "About"};
    for (int i = 0; i < 3; i++) {
        uint16_t y = SETTINGS_ITEM_Y_START + i * (SETTINGS_ITEM_HEIGHT + 10);
        // Draw rounded rectangle border
        epaper->drawRect(SETTINGS_ITEM_MARGIN, y,
                         DISPLAY_WIDTH - 2 * SETTINGS_ITEM_MARGIN,
                         SETTINGS_ITEM_HEIGHT, BBEP_BLACK);
        // Center text vertically in the item
        epaper->getStringBox(items[i], &rect);
        epaper->setCursor(SETTINGS_ITEM_MARGIN + 20,
                          y + (SETTINGS_ITEM_HEIGHT + rect.h) / 2 - 4);
        epaper->write(items[i]);
    }
}

void drawWifiSetupScreen(FASTEPD* epaper, const char* ap_name) {
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);

    epaper->setCursor(SETTINGS_ITEM_MARGIN, 50);
    epaper->write("< Back");

    BB_RECT rect;
    const char* line1 = "WiFi Setup";
    epaper->getStringBox(line1, &rect);
    epaper->setCursor((DISPLAY_WIDTH - rect.w) / 2, 150);
    epaper->write(line1);

    const char* line2 = "Connect to WiFi:";
    epaper->getStringBox(line2, &rect);
    epaper->setCursor((DISPLAY_WIDTH - rect.w) / 2, 250);
    epaper->write(line2);

    epaper->getStringBox(ap_name, &rect);
    epaper->setCursor((DISPLAY_WIDTH - rect.w) / 2, 310);
    epaper->write(ap_name);

    const char* line3 = "Then open";
    epaper->getStringBox(line3, &rect);
    epaper->setCursor((DISPLAY_WIDTH - rect.w) / 2, 400);
    epaper->write(line3);

    const char* line4 = "192.168.4.1";
    epaper->getStringBox(line4, &rect);
    epaper->setCursor((DISPLAY_WIDTH - rect.w) / 2, 460);
    epaper->write(line4);
}

void drawConfigureScreen(FASTEPD* epaper, const char* device_ip) {
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);

    epaper->setCursor(SETTINGS_ITEM_MARGIN, 40);
    epaper->write("< Back");

    BB_RECT rect;
    epaper->getStringBox("Configure", &rect);
    epaper->setCursor(DISPLAY_WIDTH - SETTINGS_ITEM_MARGIN - rect.w, 40);
    epaper->write("Configure");

    const char* line1 = "Open in browser:";
    epaper->getStringBox(line1, &rect);
    epaper->setCursor(std::max(0, ((int)DISPLAY_WIDTH - rect.w) / 2), 200);
    epaper->write(line1);

    char url[64];
    snprintf(url, sizeof(url), "http://%s", device_ip);
    epaper->getStringBox(url, &rect);
    epaper->setCursor(std::max(0, ((int)DISPLAY_WIDTH - rect.w) / 2), 260);
    epaper->write(url);
}

void drawAboutScreen(FASTEPD* epaper, const char* version, const char* wifi_ssid,
                     const char* ha_url, bool ha_connected, uint8_t battery_pct) {
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);

    // Back button
    epaper->setCursor(SETTINGS_ITEM_MARGIN, 40);
    epaper->write("< Back");

    // Title
    BB_RECT rect;
    epaper->getStringBox("About", &rect);
    epaper->setCursor(DISPLAY_WIDTH - SETTINGS_ITEM_MARGIN - rect.w, 40);
    epaper->write("About");

    // Info lines
    uint16_t y = 130;
    constexpr uint16_t line_h = 45;

    char buf[128];

    snprintf(buf, sizeof(buf), "Version: %s", version);
    epaper->setCursor(SETTINGS_ITEM_MARGIN, y);
    epaper->write(buf);
    y += line_h;

    snprintf(buf, sizeof(buf), "WiFi: %s", wifi_ssid);
    epaper->setCursor(SETTINGS_ITEM_MARGIN, y);
    epaper->write(buf);
    y += line_h;

    snprintf(buf, sizeof(buf), "HA: %s", ha_connected ? "Connected" : "Disconnected");
    epaper->setCursor(SETTINGS_ITEM_MARGIN, y);
    epaper->write(buf);
    y += line_h;

    snprintf(buf, sizeof(buf), "IP: %s", ha_url);
    epaper->setCursor(SETTINGS_ITEM_MARGIN, y);
    epaper->write(buf);
    y += line_h;

    snprintf(buf, sizeof(buf), "Battery: %d%%", battery_pct);
    epaper->setCursor(SETTINGS_ITEM_MARGIN, y);
    epaper->write(buf);
    y += line_h;

    snprintf(buf, sizeof(buf), "Build: %s", __DATE__);
    epaper->setCursor(SETTINGS_ITEM_MARGIN, y);
    epaper->write(buf);
}

// PIN pad layout constants
constexpr uint16_t PIN_PAD_X = 80;
constexpr uint16_t PIN_PAD_Y = 300;
constexpr uint16_t PIN_BTN_W = 120;
constexpr uint16_t PIN_BTN_H = 80;
constexpr uint16_t PIN_BTN_GAP = 10;
constexpr uint16_t PIN_DOT_Y = 210;
constexpr uint16_t PIN_DOT_R = 20;
constexpr uint16_t PIN_DOT_GAP = 35;

void drawPinEntryScreen(FASTEPD* epaper, int digits_entered, bool wrong_pin) {
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);

    // Back button
    epaper->setCursor(SETTINGS_ITEM_MARGIN, 40);
    epaper->write("< Back");

    // Title
    BB_RECT rect;
    const char* title = wrong_pin ? "Wrong PIN" : "Enter PIN";
    epaper->getStringBox(title, &rect);
    epaper->setCursor((DISPLAY_WIDTH - rect.w) / 2, 120);
    epaper->write(title);

    // 4 indicator dots/squares
    uint16_t dots_total_w = 4 * PIN_DOT_R * 2 + 3 * PIN_DOT_GAP;
    uint16_t dot_x = (DISPLAY_WIDTH - dots_total_w) / 2;
    for (int i = 0; i < 4; i++) {
        uint16_t cx = dot_x + i * (PIN_DOT_R * 2 + PIN_DOT_GAP) + PIN_DOT_R;
        if (i < digits_entered) {
            epaper->fillCircle(cx, PIN_DOT_Y, PIN_DOT_R, BBEP_BLACK);
        } else {
            epaper->drawCircle(cx, PIN_DOT_Y, PIN_DOT_R, BBEP_BLACK);
        }
    }

    // Number pad: 1-9 in 3x3 grid, then < 0 (enter) on bottom row
    const char* labels[] = {"1","2","3","4","5","6","7","8","9","<","0",""};
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            if (labels[idx][0] == '\0') continue; // Empty slot

            uint16_t bx = PIN_PAD_X + col * (PIN_BTN_W + PIN_BTN_GAP);
            uint16_t by = PIN_PAD_Y + row * (PIN_BTN_H + PIN_BTN_GAP);

            epaper->drawRect(bx, by, PIN_BTN_W, PIN_BTN_H, BBEP_BLACK);

            epaper->getStringBox(labels[idx], &rect);
            epaper->setCursor(bx + (PIN_BTN_W - rect.w) / 2,
                              by + (PIN_BTN_H + rect.h) / 2 - 4);
            epaper->write(labels[idx]);
        }
    }
}

int getPinPadTouched(uint16_t touch_x, uint16_t touch_y) {
    // Map: 0=1, 1=2, 2=3, 3=4, 4=5, 5=6, 6=7, 7=8, 8=9, 9=<(delete), 10=0
    const int values[] = {1,2,3,4,5,6,7,8,9,10,0,-1};
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            if (values[idx] == -1) continue;

            uint16_t bx = PIN_PAD_X + col * (PIN_BTN_W + PIN_BTN_GAP);
            uint16_t by = PIN_PAD_Y + row * (PIN_BTN_H + PIN_BTN_GAP);

            if (touch_x >= bx && touch_x < bx + PIN_BTN_W &&
                touch_y >= by && touch_y < by + PIN_BTN_H) {
                return values[idx]; // 0-9 = digit, 10 = delete
            }
        }
    }
    return -1;
}

void drawIdleScreen(FASTEPD* epaper, int16_t offset_x, int16_t offset_y) {
    epaper->setMode(BB_MODE_4BPP);
    epaper->fillScreen(0xf);

    BB_RECT rect;
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);

    const char* line1 = "Sleeping";
    const char* line2 = "Press button";
    const char* line3 = "to wake";

    BB_RECT r1, r2, r3;
    epaper->getStringBox(line1, &r1);
    epaper->getStringBox(line2, &r2);
    epaper->getStringBox(line3, &r3);

    constexpr int16_t spacing = 10;
    uint16_t total_h = r1.h + spacing + r2.h + spacing + r3.h;
    int16_t base_x = DISPLAY_WIDTH / 2;
    int16_t base_y = (DISPLAY_HEIGHT - total_h) / 2;

    int16_t y1 = base_y + offset_y;
    int16_t y2 = y1 + r1.h + spacing;
    int16_t y3 = y2 + r2.h + spacing;

    epaper->setCursor(std::max(0, (int)(base_x - r1.w / 2 + offset_x)), std::max(0, (int)y1));
    epaper->write(line1);
    epaper->setCursor(std::max(0, (int)(base_x - r2.w / 2 + offset_x)), std::max(0, (int)y2));
    epaper->write(line2);
    epaper->setCursor(std::max(0, (int)(base_x - r3.w / 2 + offset_x)), std::max(0, (int)y3));
    epaper->write(line3);

    epaper->fullUpdate(CLEAR_SLOW, false);
}
