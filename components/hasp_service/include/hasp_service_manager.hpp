#pragma once

#include "hasp_service.hpp"
#include <vector>
#include <ArduinoJson.h>

class ServiceManager {
public:
    void add(HaspService* svc);

    esp_err_t startAll();
    esp_err_t stopAll();

    HaspService* get(const char* name) const;

    // Collect config from every service into obj
    esp_err_t get_config(JsonObject obj) const;

    // Apply config – each service only looks at obj[name()]
    esp_err_t set_config(JsonObjectConst obj);

private:
    std::vector<HaspService*> services_;
};