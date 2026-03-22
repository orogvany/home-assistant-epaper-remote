#include "power.h"
#include "boards.h"
#include "constants.h"
#include "esp_log.h"
#include <Arduino.h>
#include <Wire.h>

static const char* TAG = "power";

void power_setup_rtc_timer(uint8_t minutes) {
    // Disable timer first to prevent spurious interrupt from stale value
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x0E); // Timer_Control
    Wire.write(0x03); // TE=0 (disabled), TD=11 (1/60 Hz)
    Wire.endTransmission();

    // Set timer countdown value
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x0F); // Timer register
    Wire.write(minutes);
    Wire.endTransmission();

    // Clear timer flag and enable timer interrupt
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x01); // Control_Status_2
    Wire.write(0x01); // TIE=1 (timer interrupt enable), clear TF
    Wire.endTransmission();

    // Enable timer
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x0E); // Timer_Control
    Wire.write(0x83); // TE=1 (enable), TD=11 (1/60 Hz)
    Wire.endTransmission();

    ESP_LOGI(TAG, "RTC timer set for %d minutes", minutes);
}

void power_off_pms150g() {
    if (!HAS_PMS150G || PWROFF_PIN == 0) return;

    ESP_LOGI(TAG, "Powering off via PMS150G (GPIO %d)", PWROFF_PIN);
    pinMode(PWROFF_PIN, OUTPUT);
    for (int i = 0; i < 5; i++) {
        digitalWrite(PWROFF_PIN, LOW);
        delay(50);
        digitalWrite(PWROFF_PIN, HIGH);
        delay(50);
    }
}

bool power_was_rtc_wake() {
    // Check Timer_Control register - if timer isn't enabled (TE=0),
    // we never set it, so any TF flag is stale from factory/previous firmware
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x0E); // Timer_Control
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)BM8563_ADDR, (uint8_t)1);
    if (!Wire.available()) return false;
    uint8_t timer_ctrl = Wire.read();
    if (!(timer_ctrl & 0x80)) return false; // TE bit not set - timer was never enabled by us

    // Check Control_Status_2 for timer flag (TF)
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x01);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)BM8563_ADDR, (uint8_t)1);
    if (Wire.available()) {
        uint8_t status = Wire.read();
        return (status & 0x04) != 0; // TF bit
    }
    return false;
}

void power_clear_rtc_flag() {
    // Read current value, clear TF bit, write back
    Wire.beginTransmission(BM8563_ADDR);
    Wire.write(0x01);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)BM8563_ADDR, (uint8_t)1);
    if (Wire.available()) {
        uint8_t status = Wire.read();
        status &= ~0x04; // Clear TF
        Wire.beginTransmission(BM8563_ADDR);
        Wire.write(0x01);
        Wire.write(status);
        Wire.endTransmission();
    }
}
