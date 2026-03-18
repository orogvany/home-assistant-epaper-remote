#include "screen.h"
#include "esp_system.h"
#include "widgets/OnOffButton.h"
#include "widgets/Slider.h"

void screen_add_slider(SliderConfig config, Screen* screen) {
    if (screen->widget_count >= MAX_WIDGETS_PER_SCREEN) {
        esp_system_abort("too many widgets configured");
    }

    Rect rect{
        .x = config.pos_x,
        .y = config.pos_y,
        .w = config.width,
        .h = config.height,
    };

    Slider* widget = new (std::nothrow) Slider(config.label, config.icon_on, config.icon_off, rect);
    if (!widget) {
        esp_system_abort("out of memory");
    }

    const uint16_t widget_idx = screen->widget_count++;
    screen->widgets[widget_idx] = widget;
    screen->entity_ids[widget_idx] = config.entity_ref.index;
}

void screen_add_button(ButtonConfig config, Screen* screen) {
    if (screen->widget_count >= MAX_WIDGETS_PER_SCREEN) {
        esp_system_abort("too many widgets configured");
    }

    Rect rect{
        .x = config.pos_x,
        .y = config.pos_y,
        .w = 0,
        .h = 0,
    };

    OnOffButton* widget = new (std::nothrow) OnOffButton(config.label, config.icon_on, config.icon_off, rect);
    if (!widget) {
        esp_system_abort("out of memory");
    }

    const uint16_t widget_idx = screen->widget_count++;
    screen->widgets[widget_idx] = widget;
    screen->entity_ids[widget_idx] = config.entity_ref.index;
}