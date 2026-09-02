#pragma once

/* Step 7E: HaspTime service — SNTP + POSIX timezone.
 *
 * S3-mirror map (src/sys/net/hasp_time.cpp / .h):
 *   timeSetup()                     -> HaspTime::start_backend()
 *   timeSyncCallback(tv*)           -> HaspTime::sync_cb(tv*)   (static)
 *   time_zone_to_possix(const char*)-> HaspTime::zone_to_posix(const char*)
 *   timeGetConfig / timeSetConfig   -> HaspTime::get_config / set_config
 *
 * NVS layout (namespace = "time"):
 *   zone   (str)  IANA name, default "Etc/Universal"          (S3 "zone")
 *   region (str)  UI grouping only, default "etc"             (S3 "region")
 *   ntp1   (str)  default "pool.ntp.org"                      (S3 "ntp1")
 *   ntp2   (str)  default "time.nist.gov"                     (S3 "ntp2")
 *   ntp3   (str)  default "time.google.com"                   (S3 "ntp3")
 *   enable (bool) default true                                (S3 "enable")
 *   dhcp   (bool) default true — SNTP servers from DHCP opt42 (S3 "dhcp")
 *
 * ServiceMode = KeepAlive: SNTP is (re)started on GOT_IP, stopped on
 * disconnect. Same trigger as HaspMqtt — network events routed by the
 * ServiceManager via on_network_up/on_network_down.
 */

#include "hasp_service.hpp"

#include <string>

class HaspTime : public HaspService {
public:
    HaspTime() { mode_ = ServiceMode::KeepAlive; }

    const char* name() const override { return "time"; }
    bool isRunning() const override { return running_; }

    esp_err_t get_config(JsonObject obj) const override;
    esp_err_t set_config(JsonObjectConst obj) override;

    /* IANA → POSIX TZ. 1-в-1 порт из S3 hasp_time.cpp:68. */
    static std::string zone_to_posix(const char* iana);

protected:
    esp_err_t start_backend() override;
    esp_err_t stop_backend()  override;
    void on_network_up()   override { start_backend(); }
    void on_network_down() override { stop_backend();  }

private:
    esp_err_t load_from_nvs();
    void apply_sntp();  // setenv TZ + esp_sntp_init from current fields

    bool running_ = false;

    std::string zone_   = "Etc/Universal";
    std::string region_ = "etc";
    std::string ntp1_   = "pool.ntp.org";
    std::string ntp2_   = "time.nist.gov";
    std::string ntp3_   = "time.google.com";
    bool        enable_ = true;
    bool        dhcp_   = true;
};
