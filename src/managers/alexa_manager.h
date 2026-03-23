#pragma once

#include "config_store.h"
#include "store.h"
#include <FastEPD.h>

struct AlexaManagerArgs {
    EntityStore* store;
    ConfigStore* config_store;
};

void alexa_manager_task(void* arg);
