#include "hasp_service_manager.hpp"
#include "esp_log.h"
#include <cstring>

static const char* TAG = "HASP_MGR";

static ServiceManager* s_default_mgr = nullptr;

ServiceManager* ServiceManager::default_instance() { return s_default_mgr; }
void ServiceManager::set_default_instance(ServiceManager* mgr) { s_default_mgr = mgr; }

void ServiceManager::add(HaspService* svc)
{
    if (svc) services_.push_back(svc);
}

esp_err_t ServiceManager::startAll()
{
    for (auto* s : services_) {
        esp_err_t err = s->start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start %s: %s", s->name(), esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t ServiceManager::stopAll()
{
    // stop in reverse order
    for (auto it = services_.rbegin(); it != services_.rend(); ++it) {
        (*it)->stop();
    }
    return ESP_OK;
}

HaspService* ServiceManager::get(const char* name) const
{
    for (auto* s : services_) {
        if (strcmp(s->name(), name) == 0) return s;
    }
    return nullptr;
}

esp_err_t ServiceManager::get_config(JsonObject obj) const
{
    for (auto* s : services_) {
        s->get_config(obj);          // each writes into obj[name()]
    }
    return ESP_OK;
}

esp_err_t ServiceManager::set_config(JsonObjectConst obj)
{
    for (auto* s : services_) {
        // Only call set_config if this service has a key in the document
        if (obj[s->name()].is<JsonObjectConst>()) {
            s->set_config(obj);
        }
    }
    return ESP_OK;
}