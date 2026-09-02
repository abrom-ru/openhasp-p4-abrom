#pragma once

// Step 7A: config-only HaspService adapter for the "hasp" section.
// Mirrors S3 haspGetConfig/haspSetConfig (openhasp-abrom/src/hasp/hasp.cpp:795-882)
// but only for keys the P4 port actually consumes today:
//   - theme      -> haspThemeId    (hasp_theme.cpp)
//   - startpage  -> haspStartPage  (hasp_page.cpp, new global)
// Other S3 keys (startdim/hue/color1/color2/zifont/pages) are deferred until
// the corresponding runtime knobs exist on P4.
//
// This class has no backend — start/stop are no-ops. It exists purely so the
// ServiceManager routes obj["hasp"] to us in load/save flows.

#include "hasp_service.hpp"

class HaspModule : public HaspService {
public:
    HaspModule() { mode_ = ServiceMode::Manual; }

    const char* name() const override { return "hasp"; }
    bool isRunning() const override { return true; }

    esp_err_t get_config(JsonObject obj) const override;
    esp_err_t set_config(JsonObjectConst obj) override;

protected:
    esp_err_t start_backend() override { return ESP_OK; }
    esp_err_t stop_backend()  override { return ESP_OK; }
    void on_network_up()   override {}
    void on_network_down() override {}
};
