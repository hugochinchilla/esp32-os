// Wi-Fi adapter: ESP-IDF's station mode behind net::WifiPort, the network
// time (SNTP), and a KeyStore in NVS.
//
// The radio reports from ESP-IDF's event task; the UI task asks through the
// port. What they share is guarded by a mutex and copied out.
#include "wifi.hpp"

#include <cstring>
#include <ctime>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "mdns.h"
#include "nvs_flash.h"

namespace {

const char *TAG = "wifi";
constexpr int kMaxFound = 2 * net::Manager::kMaxSeen;
constexpr int kTransientRetries = 3;

class Guard {
public:
    explicit Guard(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m_); }

private:
    SemaphoreHandle_t m_;
};

// Where the network was found last time (NVS, with the saved networks; never
// logged): joining goes straight to that channel and access point instead of
// scanning every channel, which keeps the radio's transmitting short (it
// disturbs the analogue video, docs/DECISIONS.md).
struct ApHint {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
};

bool load_hint(ApHint &h)
{
    nvs_handle_t handle;
    if (nvs_open("chinchilla", NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(h);
    const bool ok = nvs_get_blob(handle, "ap_hint", &h, &size) == ESP_OK && size == sizeof(h);
    nvs_close(handle);
    h.ssid[32] = 0;
    return ok;
}

void save_hint(const ApHint *h)
{
    nvs_handle_t handle;
    if (nvs_open("chinchilla", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    if (h != nullptr) {
        nvs_set_blob(handle, "ap_hint", h, sizeof(*h));
    } else {
        nvs_erase_key(handle, "ap_hint");
    }
    nvs_commit(handle);
    nvs_close(handle);
}

net::Security security_of(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:
    case WIFI_AUTH_OWE:
        return net::Security::Open;
    case WIFI_AUTH_ENTERPRISE: // WPA2 Enterprise
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:
        return net::Security::Enterprise;
    default:
        return net::Security::Password;
    }
}

class EspWifi : public net::WifiPort {
public:
    bool begin()
    {
        mutex_ = xSemaphoreCreateMutex();
        esp_err_t err = esp_netif_init();
        if (err == ESP_OK) {
            err = esp_event_loop_create_default();
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "network stack: %s", esp_err_to_name(err));
            return false;
        }
        netif_ = esp_netif_create_default_wifi_sta();
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&config);
        if (err == ESP_OK) {
            // The remembered networks are kept by the core, not by the driver.
            err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        }
        if (err == ESP_OK) {
            err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, this, nullptr);
        }
        if (err == ESP_OK) {
            err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_event, this, nullptr);
        }
        if (err == ESP_OK) {
            err = esp_wifi_set_mode(WIFI_MODE_STA);
        }
        if (err == ESP_OK) {
            err = esp_wifi_start();
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "radio: %s", esp_err_to_name(err));
            return false;
        }
        // No power saving: with it, a remote screen's clicks lag by 100 to
        // 300 ms. The board is mains powered.
        esp_wifi_set_ps(WIFI_PS_NONE);
        esp_wifi_get_mac(WIFI_IF_STA, mac_);
        Guard g(mutex_);
        link_ = net::Link::Idle;
        return true;
    }

    void start_scan() override
    {
        if (mutex_ == nullptr) {
            return;
        }
        {
            Guard g(mutex_);
            if (link_ == net::Link::Off || scanning_) {
                return;
            }
            scanning_ = true;
        }
        // All channels, hidden networks left out; the answer is an event.
        if (esp_wifi_scan_start(nullptr, false) != ESP_OK) {
            Guard g(mutex_);
            scanning_ = false; // busy joining: the old list stays
        }
    }

    bool scanning() override
    {
        if (mutex_ == nullptr) {
            return false;
        }
        Guard g(mutex_);
        return scanning_;
    }

    int results(net::Network *out, int max) override
    {
        if (mutex_ == nullptr) {
            return 0;
        }
        Guard g(mutex_);
        const int n = found_count_ < max ? found_count_ : max;
        memcpy(out, found_, static_cast<size_t>(n) * sizeof(net::Network));
        return n;
    }

    void connect(const char *ssid, const char *password) override
    {
        if (mutex_ == nullptr) {
            return;
        }
        bool was_linked, was_scanning;
        {
            Guard g(mutex_);
            if (link_ == net::Link::Off) {
                return;
            }
            was_linked = link_ != net::Link::Idle;
            was_scanning = scanning_;
            link_ = net::Link::Connecting;
            failure_ = net::Failure::None;
            retries_ = 0;
            scanning_ = false;
            strlcpy(ssid_, ssid, sizeof(ssid_));
        }
        if (was_scanning) {
            esp_wifi_scan_stop();
        }
        if (was_linked) {
            esp_wifi_disconnect(); // its "left" event is ignored while joining
        }
        wifi_config_t config = {};
        // Fixed-size fields: a 32-character name or 64-digit key fills them with
        // no terminator, as ESP-IDF expects (config is zeroed above).
        memcpy(config.sta.ssid, ssid, strnlen(ssid, sizeof(config.sta.ssid)));
        memcpy(config.sta.password, password, strnlen(password, sizeof(config.sta.password)));
        // Every channel, and of several access points with the name, the
        // strongest. With a password, nothing weaker than WPA is accepted.
        config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
        ApHint hint;
        hinted_ = load_hint(hint) && strcmp(hint.ssid, ssid) == 0 && hint.channel >= 1 && hint.channel <= 14;
        if (hinted_) {
            // Straight to last time's channel and access point; if it is not
            // there, the hint is dropped and the next try scans every channel.
            config.sta.scan_method = WIFI_FAST_SCAN;
            config.sta.channel = hint.channel;
            config.sta.bssid_set = true;
            memcpy(config.sta.bssid, hint.bssid, 6);
        }
        config.sta.threshold.authmode = password[0] != 0 ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
        config.sta.pmf_cfg.capable = true;
        config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
        memset(&config, 0, sizeof(config)); // the password does not stay on the stack
        if (err == ESP_OK) {
            err = esp_wifi_connect();
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
            Guard g(mutex_);
            link_ = net::Link::Idle;
            failure_ = net::Failure::Other;
        }
    }

    void disconnect() override
    {
        if (mutex_ == nullptr) {
            return;
        }
        {
            Guard g(mutex_);
            if (link_ == net::Link::Off) {
                return;
            }
            link_ = net::Link::Idle;
            failure_ = net::Failure::None;
        }
        esp_wifi_disconnect();
    }

    void status(net::LinkStatus &out) override
    {
        out = {};
        if (mutex_ == nullptr) {
            return; // Link::Off
        }
        {
            Guard g(mutex_);
            out.link = link_;
            out.failure = failure_;
            memcpy(out.mac, mac_, 6);
            if (link_ == net::Link::Connecting || link_ == net::Link::Connected) {
                strlcpy(out.ssid, ssid_, sizeof(out.ssid));
            }
            if (link_ == net::Link::Connected) {
                memcpy(out.address, &ip_.ip.addr, 4);
                memcpy(out.mask, &ip_.netmask.addr, 4);
                memcpy(out.gateway, &ip_.gw.addr, 4);
            }
        }
        if (out.link == net::Link::Connected) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                out.rssi = ap.rssi;
                out.channel = ap.primary;
            }
            esp_netif_dns_info_t dns;
            if (esp_netif_get_dns_info(netif_, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
                memcpy(out.dns, &dns.ip.u_addr.ip4.addr, 4);
            }
            if (!sntp_started_) {
                // Asked once; it keeps the clock right from then on.
                sntp_started_ = true;
                esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
                esp_netif_sntp_init(&sntp);
            }
        }
    }

    bool utc_time(int64_t &seconds) override
    {
        // The chip's clock starts at 1970; a date after 2023 came from SNTP.
        const time_t now = time(nullptr);
        seconds = static_cast<int64_t>(now);
        return sntp_started_ && now > 1700000000;
    }

private:
    static void on_event(void *self, esp_event_base_t base, int32_t id, void *data)
    {
        static_cast<EspWifi *>(self)->event(base, id, data);
    }

    // On ESP-IDF's event task (a small stack: nothing large here).
    void event(esp_event_base_t base, int32_t id, void *data)
    {
        if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
            static EXT_RAM_BSS_ATTR wifi_ap_record_t records[kMaxFound]; // the event task fills it
            uint16_t count = kMaxFound;
            if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) {
                count = 0;
            }
            Guard g(mutex_);
            if (!scanning_) {
                return; // stopped by a connect: the old list stays
            }
            found_count_ = count;
            for (int i = 0; i < count; i++) {
                net::Network &n = found_[i];
                n = {};
                strlcpy(n.ssid, reinterpret_cast<const char *>(records[i].ssid), sizeof(n.ssid));
                n.rssi = records[i].rssi;
                n.channel = records[i].primary;
                n.security = security_of(records[i].authmode);
            }
            scanning_ = false;
        } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
            const auto *e = static_cast<const wifi_event_sta_disconnected_t *>(data);
            bool again = false, drop_hint = false;
            {
                Guard g(mutex_);
                if (link_ == net::Link::Connected) {
                    link_ = net::Link::Idle; // the link dropped
                    failure_ = net::Failure::None;
                } else if (link_ == net::Link::Connecting && e->reason != WIFI_REASON_ASSOC_LEAVE) {
                    net::Failure why = net::Failure::Other;
                    switch (e->reason) {
                    case WIFI_REASON_AUTH_FAIL:
                    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                    case WIFI_REASON_HANDSHAKE_TIMEOUT:
                    case WIFI_REASON_MIC_FAILURE:
                        why = net::Failure::WrongPassword;
                        break;
                    case WIFI_REASON_NO_AP_FOUND:
                    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
                    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
                    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
                        why = net::Failure::NotFound;
                        drop_hint = hinted_;
                        break;
                    default:
                        break;
                    }
                    // Anything else is often a passing radio problem.
                    if (why == net::Failure::Other && retries_ < kTransientRetries) {
                        retries_++;
                        again = true;
                    } else {
                        link_ = net::Link::Idle;
                        failure_ = why;
                    }
                }
            }
            ESP_LOGI(TAG, "disconnected, reason %d%s", e->reason, again ? ", trying again" : "");
            if (drop_hint) {
                save_hint(nullptr); // the access point moved: scan every channel next time
                hinted_ = false;
            }
            if (again) {
                esp_wifi_connect();
            }
        } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
            // Remember where it was, written only when it changed (no flash
            // write on an ordinary join).
            const auto *e = static_cast<const wifi_event_sta_connected_t *>(data);
            ApHint now = {}, before = {};
            memcpy(now.ssid, e->ssid, e->ssid_len < 32 ? e->ssid_len : 32);
            memcpy(now.bssid, e->bssid, 6);
            now.channel = e->channel;
            if (!load_hint(before) || memcmp(&before, &now, sizeof(now)) != 0) {
                save_hint(&now);
            }
        } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            const auto *e = static_cast<const ip_event_got_ip_t *>(data);
            Guard g(mutex_);
            if (link_ == net::Link::Connecting || link_ == net::Link::Connected) {
                ip_ = e->ip_info;
                link_ = net::Link::Connected;
            }
        } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
            bool drop = false;
            {
                Guard g(mutex_);
                if (link_ == net::Link::Connected) {
                    link_ = net::Link::Idle;
                    failure_ = net::Failure::None;
                    drop = true;
                }
            }
            if (drop) {
                esp_wifi_disconnect();
            }
        }
    }

    SemaphoreHandle_t mutex_ = nullptr;
    esp_netif_t *netif_ = nullptr;
    net::Link link_ = net::Link::Off;
    net::Failure failure_ = net::Failure::None;
    char ssid_[net::kSsidMax + 1] = "";
    uint8_t mac_[6] = {};
    esp_netif_ip_info_t ip_ = {};
    int retries_ = 0;
    bool scanning_ = false;
    bool hinted_ = false; // this join went straight to the remembered channel
    bool sntp_started_ = false;
    net::Network found_[kMaxFound] = {};
    int found_count_ = 0;
};

// Blobs in NVS, namespace "chinchilla".
class NvsStore : public net::KeyStore {
public:
    size_t load(const char *key, void *out, size_t capacity) override
    {
        nvs_handle_t handle;
        if (nvs_open("chinchilla", NVS_READONLY, &handle) != ESP_OK) {
            return 0;
        }
        size_t size = capacity;
        const esp_err_t err = nvs_get_blob(handle, key, out, &size);
        nvs_close(handle);
        return err == ESP_OK ? size : 0;
    }
    bool save(const char *key, const void *data, size_t size) override
    {
        nvs_handle_t handle;
        if (nvs_open("chinchilla", NVS_READWRITE, &handle) != ESP_OK) {
            return false;
        }
        esp_err_t err = nvs_set_blob(handle, key, data, size);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "saving %s: %s", key, esp_err_to_name(err));
        }
        return err == ESP_OK;
    }
};

EspWifi s_wifi;
NvsStore s_store;

} // namespace

bool wifi_begin()
{
    // NVS holds the radio's calibration and our settings. A partition from
    // an older layout is erased and started again.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "settings area: %s", esp_err_to_name(err));
        return false;
    }
    return s_wifi.begin();
}

bool wifi_announce(const char *name, int vnc_port, int web_port)
{
    // Called again whenever what is offered changes (vncd started or
    // stopped): the name stays, the services are announced afresh.
    static bool started = false;
    if (!started) {
        esp_err_t err = mdns_init();
        if (err == ESP_OK) {
            err = mdns_hostname_set(name);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mDNS did not start: %s", esp_err_to_name(err));
            return false;
        }
        mdns_instance_name_set("Chinchilla");
        started = true;
    }
    // Only what was announced is taken back: mDNS logs an error for one
    // that is not there.
    static bool vnc_told = false, web_told = false;
    if (vnc_told) {
        mdns_service_remove("_rfb", "_tcp");
        vnc_told = false;
    }
    if (web_told) {
        mdns_service_remove("_http", "_tcp");
        web_told = false;
    }
    if (vnc_port != 0) {
        // What VNC viewers look for when they list the computers around.
        vnc_told = mdns_service_add("Chinchilla", "_rfb", "_tcp", static_cast<uint16_t>(vnc_port), nullptr, 0) == ESP_OK;
    }
    if (web_port != 0) {
        web_told = mdns_service_add("Chinchilla", "_http", "_tcp", static_cast<uint16_t>(web_port), nullptr, 0) == ESP_OK;
    }
    return true;
}

net::WifiPort &wifi_port() { return s_wifi; }
net::KeyStore &settings_store() { return s_store; }
