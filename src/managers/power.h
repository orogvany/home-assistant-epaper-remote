#pragma once
#include <cstdint>

void power_setup_rtc_timer(uint8_t minutes);
void power_disable_rtc_timer();
void power_off_pms150g();
bool power_was_rtc_wake();
void power_clear_rtc_flag();
