#include "draw.h"
#include "assets/icons.h"
#include "assets/Montserrat_Regular_26.h"
#include "boards.h"
#include <FastEPD.h>
#include <cstddef>
#include <cstdint>
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

void drawBatteryIndicator(FASTEPD* epaper, uint8_t percentage, bool charging) {
    char label[8];
    snprintf(label, sizeof(label), "%d%%", percentage);

    BB_RECT text_rect;
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);
    epaper->getStringBox(label, &text_rect);

    // Extra space for lightning bolt when charging
    uint16_t bolt_w = charging ? 14 : 0;
    uint16_t total_w = BATT_ICON_W + BATT_TIP_W + 6 + text_rect.w + bolt_w;
    uint16_t x = DISPLAY_WIDTH - BATT_MARGIN - total_w;
    uint16_t y = BATT_MARGIN;

    // Clear the area
    epaper->fillRect(x - 4, y - 2, total_w + 8, std::max((int)BATT_ICON_H, text_rect.h) + 4, 0xf);

    // Battery outline
    epaper->drawRect(x, y, BATT_ICON_W, BATT_ICON_H, BBEP_BLACK);
    epaper->fillRect(x + BATT_ICON_W, y + 3, BATT_TIP_W, BATT_ICON_H - 6, BBEP_BLACK);

    // Fill level
    uint16_t fill_w = (BATT_ICON_W - 2 * BATT_BORDER) * percentage / 100;
    if (fill_w > 0) {
        epaper->fillRect(x + BATT_BORDER, y + BATT_BORDER, fill_w, BATT_ICON_H - 2 * BATT_BORDER, BBEP_BLACK);
    }

    // Percentage text
    uint16_t text_x = x + BATT_ICON_W + BATT_TIP_W + 6;
    epaper->setCursor(text_x, y + (BATT_ICON_H + text_rect.h) / 2 - 2);
    epaper->write(label);

    // Lightning bolt when charging
    if (charging) {
        uint16_t bx = text_x + text_rect.w + 4; // Bolt x start
        uint16_t by = y;                          // Bolt y start
        // Draw a small lightning bolt shape (7px wide, 14px tall)
        epaper->drawLine(bx + 5, by,     bx + 1, by + 6, BBEP_BLACK);
        epaper->drawLine(bx + 6, by,     bx + 2, by + 6, BBEP_BLACK);
        epaper->drawLine(bx + 1, by + 6, bx + 4, by + 6, BBEP_BLACK);
        epaper->drawLine(bx + 4, by + 6, bx,     by + 13, BBEP_BLACK);
        epaper->drawLine(bx + 5, by + 6, bx + 1, by + 13, BBEP_BLACK);
    }
}

void drawGearIcon(FASTEPD* epaper) {
    uint16_t x = DISPLAY_WIDTH - GEAR_MARGIN - GEAR_ICON_SIZE;
    uint16_t y = DISPLAY_HEIGHT - GEAR_MARGIN - GEAR_ICON_SIZE;
    epaper->loadBMP(cog_outline, x, y, 0xf, BBEP_BLACK);
}

bool isGearIconTouched(uint16_t touch_x, uint16_t touch_y) {
    uint16_t x = DISPLAY_WIDTH - GEAR_MARGIN - GEAR_ICON_SIZE;
    uint16_t y = DISPLAY_HEIGHT - GEAR_MARGIN - GEAR_ICON_SIZE;
    // Generous touch area (icon + margin)
    return touch_x >= (x - 10) && touch_x <= DISPLAY_WIDTH &&
           touch_y >= (y - 10) && touch_y <= DISPLAY_HEIGHT;
}

// Settings menu items — returns 0-based index or -1 for back/none
// 0 = Configure WiFi, 1 = Configure HA, 2 = About, -1 = Back
int getSettingsMenuItemTouched(uint16_t touch_x, uint16_t touch_y) {
    // Back button — top-left area
    if (touch_x < 100 && touch_y < 80) return -1;

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
    const char* items[] = {"Configure WiFi", "Configure HA", "About"};
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

void drawHaSetupScreen(FASTEPD* epaper, const char* device_ip) {
    epaper->setFont(Montserrat_Regular_26);
    epaper->setTextColor(BBEP_BLACK);

    epaper->setCursor(SETTINGS_ITEM_MARGIN, 50);
    epaper->write("< Back");

    BB_RECT rect;
    const char* line1a = "Home Assistant";
    epaper->getStringBox(line1a, &rect);
    epaper->setCursor(std::max(0, ((int)DISPLAY_WIDTH - rect.w) / 2), 150);
    epaper->write(line1a);

    const char* line1b = "Setup";
    epaper->getStringBox(line1b, &rect);
    epaper->setCursor(std::max(0, ((int)DISPLAY_WIDTH - rect.w) / 2), 190);
    epaper->write(line1b);

    const char* line2 = "Open in browser:";
    epaper->getStringBox(line2, &rect);
    epaper->setCursor(std::max(0, ((int)DISPLAY_WIDTH - rect.w) / 2), 300);
    epaper->write(line2);

    char url[64];
    snprintf(url, sizeof(url), "http://%s", device_ip);
    epaper->getStringBox(url, &rect);
    epaper->setCursor(std::max(0, ((int)DISPLAY_WIDTH - rect.w) / 2), 360);
    epaper->write(url);
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
