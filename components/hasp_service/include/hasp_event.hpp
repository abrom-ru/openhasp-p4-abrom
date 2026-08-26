#pragma once
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(HASP_EVENT);

enum {
    HASP_EVENT_CONNECTED,       // WiFi (or net) has IP
    HASP_EVENT_DISCONNECTED,    // link lost
    HASP_EVENT_TIME_UPDATED,    // SNTP / manual time set
    HASP_EVENT_DEEP_SLEEP,         // device is going to sleep
    HASP_EVENT_WAKE_UP,            // device woke up from sleep
    HASP_EVENT_FACTORY_RESET,      // user requested factory reset
    HASP_EVENT_REBOOT,             // user requested reboot
    HASP_EVENT_SERVICE_STARTED,    // service started (wifi, mqtt, http, …
    HASP_EVENT_SERVICE_STOPPED,    // service stopped
    HASP_EVENT_SERVICE_CONFIG_CHANGED, // service config changed
};