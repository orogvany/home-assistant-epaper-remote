#pragma once
#include "config_store.h"

// Start/stop the configuration web server.
// Only active when user is on the Configure screen.
void config_server_start(ConfigStore* config_store);
void config_server_stop();
void config_server_poll(); // Call frequently while active
bool config_server_is_active();
