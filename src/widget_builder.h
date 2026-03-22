#pragma once
#include "config_store.h"
#include "screen.h"
#include "store.h"

// Build widgets dynamically from ConfigStore's saved UI devices.
// Returns the number of widgets created.
int build_widgets_from_config(const AppConfig& app, EntityStore* store, Screen* screen);
