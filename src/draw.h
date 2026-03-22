#pragma once

#include <FastEPD.h>
#include <cstddef>
#include <cstdint>

void drawCenteredIconWithText(FASTEPD* epaper, const uint8_t* icon, const char* const* lines, uint8_t line_spacing,
                              uint8_t icon_spacing);

void drawBatteryIndicator(FASTEPD* epaper, uint8_t percentage, bool charging);
void drawGearIcon(FASTEPD* epaper);
void drawIdleScreen(FASTEPD* epaper, int16_t offset_x, int16_t offset_y);
