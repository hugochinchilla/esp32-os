// Bluetooth keyboards and mice on the ESP32-S3 (backlog item 51): the
// chip's Bluetooth LE radio as the host of HID devices, with the NimBLE
// stack. The core's manager (core/input/bluetooth.hpp) says whom to
// connect; this listens, connects, pairs ("Just Works", no code; NimBLE
// keeps the keys in NVS), asks the device what its reports look like,
// and from then on turns each report into keys and mouse movements
// (core/input/hid.hpp).
//
// Two tasks come here. The UI task asks through the port. NimBLE's own
// task (core 0, beside the network) runs every callback below, and the
// reports arrive on it; what both read is behind one mutex, held for a
// few copies and never across a call into NimBLE.
#include "bluetooth.hpp"

#include <cstring>
#include <new>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "core/input/hid.hpp"
#include "core/msg/queue.hpp"

extern "C" void ble_store_config_init(void);

namespace {

const char *TAG = "bluetooth";

constexpr uint16_t kHidService = 0x1812, kReport = 0x2A4D, kReportMap = 0x2A4B;
constexpr uint16_t kReportReference = 0x2908, kClientConfig = 0x2902;
const ble_uuid16_t kHidServiceUuid = {{BLE_UUID_TYPE_16}, kHidService};

// Listening takes the radio from Wi-Fi: at 30 ms in every 100 a full
// update of the remote screen took 3.3 to 3.7 s instead of 2.0 (board
// batch 19). So in the background it is 20 ms in every 200; a device that
// calls advertises every 20 to 100 ms for many seconds and is still heard
// within a few. The 30 in 100 is kept for when somebody is waiting.
constexpr uint16_t kBackgroundInterval = 320, kBackgroundWindow = 32; // of 0.625 ms
constexpr uint16_t kSearchInterval = 160, kSearchWindow = 48;
constexpr int32_t kConnectMs = 10000;
// How often a connected device may speak: every 7.5 ms (units of 1.25 ms),
// the shortest there is, so that each of a mouse's 125 reports a second
// has a turn of its own. NimBLE's own choice is 30 to 50 ms. (The board,
// 2026-11-19: at 11.25 ms the pointer still jumped; the gaps were Wi-Fi's,
// see share_radio.)
constexpr uint16_t kLinkIntervalMin = 6, kLinkIntervalMax = 6;
// A device not heard for this long is no longer listed.
constexpr int64_t kHeardUs = 8 * 1000000ll;

constexpr int kMaxConns = bt::Manager::kMaxLinks;
constexpr int kMaxHeard = bt::Manager::kMaxFound;
constexpr int kMaxReports = 10;
constexpr size_t kMaxMap = 1024;
constexpr size_t kMaxReport = 64;

struct Guard {
    explicit Guard(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(m_); }
    SemaphoreHandle_t m_;
};

bt::Address address_of(const ble_addr_t &a)
{
    bt::Address out = {};
    for (int i = 0; i < 6; i++) {
        out.bytes[i] = a.val[5 - i];
    }
    out.random = (a.type & 1) != 0;
    return out;
}

ble_addr_t ble_address(const bt::Address &a)
{
    ble_addr_t out = {};
    out.type = a.random ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
    for (int i = 0; i < 6; i++) {
        out.val[i] = a.bytes[5 - i];
    }
    return out;
}

/* ---- Input on its way to the UI task ------------------------------------------- */

// Keys and mouse reports become messages to the UI task. A mouse sends 125
// reports a second and the queue holds 32 messages, so movements are added
// up here and fetched by one message at a time: a desktop busy for a moment
// gets one long movement afterwards instead of a full queue. A report that
// changes a button, and every key, is a message of its own, waited for
// (a key that never came up would stay down).
class MergedInput : public ports::InputSink {
public:
    static constexpr uint32_t kWaitMs = 250;

    void begin(ports::InputSink &target)
    {
        target_ = &target;
        mutex_ = xSemaphoreCreateMutex();
    }

    // NimBLE's task.
    void key(uint16_t usage, bool pressed) override
    {
        if (!msg::post(proc::kNone, &run_key, this, usage, pressed ? 1 : 0, kWaitMs)) {
            lost_ = lost_ + 1;
        }
    }
    void mouse(const ports::MouseEvent &e) override
    {
        bool ask = false;
        const bool button = e.buttons != buttons_;
        buttons_ = e.buttons;
        {
            Guard g(mutex_);
            x_ += e.x;
            y_ += e.y;
            wheel_ += e.wheel;
            pan_ += e.wheel_x;
            if (!button) {
                ask = !asked_;
                asked_ = true;
            }
        }
        // The buttons travel in the message: the movement so far is taken
        // with them, in order.
        if (button) {
            if (!msg::post(proc::kNone, &run_buttons, this, e.buttons, 0, kWaitMs)) {
                lost_ = lost_ + 1;
            }
        } else if (ask && !msg::post(proc::kNone, &run_motion, this)) {
            Guard g(mutex_);
            asked_ = false; // no room: the movement waits for the next report
        }
    }
    uint32_t lost() const { return lost_; }

private:
    // The UI task.
    static void run_key(const msg::Message &m)
    {
        static_cast<MergedInput *>(m.context)->target_->key(static_cast<uint16_t>(m.a), m.b != 0);
    }
    static void run_motion(const msg::Message &m) { static_cast<MergedInput *>(m.context)->deliver(false, 0); }
    static void run_buttons(const msg::Message &m)
    {
        static_cast<MergedInput *>(m.context)->deliver(true, static_cast<uint8_t>(m.a));
    }
    void deliver(bool button, uint8_t buttons)
    {
        int32_t x, y, wheel, pan;
        {
            Guard g(mutex_);
            x = x_, y = y_, wheel = wheel_, pan = pan_;
            x_ = y_ = wheel_ = pan_ = 0;
            if (!button) {
                asked_ = false;
            }
        }
        if (button) {
            sent_buttons_ = buttons;
        } else if (x == 0 && y == 0 && wheel == 0 && pan == 0) {
            return; // a message with buttons took it already
        }
        const auto fit = [](int32_t v, int32_t most) { return v < -most ? -most : (v > most ? most : v); };
        ports::MouseEvent e{false, static_cast<int16_t>(fit(x, 32767)), static_cast<int16_t>(fit(y, 32767)),
                            sent_buttons_, static_cast<int8_t>(fit(wheel, 127))};
        e.wheel_x = static_cast<int8_t>(fit(pan, 127));
        target_->mouse(e);
    }

    ports::InputSink *target_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    int32_t x_ = 0, y_ = 0, wheel_ = 0, pan_ = 0; // not yet fetched (mutex)
    bool asked_ = false;                           // a run_motion message is on its way (mutex)
    uint8_t buttons_ = 0;                          // NimBLE's task: as the last report said
    uint8_t sent_buttons_ = 0;                     // the UI task: as last delivered
    volatile uint32_t lost_ = 0;
};

/* ---- The radio ------------------------------------------------------------------ */

class Radio : public bt::Port {
public:
    bool begin(ports::InputSink &sink, bool wifi_rests)
    {
        wifi_rests_ = wifi_rests;
        mutex_ = xSemaphoreCreateMutex();
        input_.begin(sink);
        const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const esp_err_t err = nimble_port_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "the stack did not start: %s", esp_err_to_name(err));
            return false;
        }
        ble_hs_cfg.sync_cb = &on_sync;
        ble_hs_cfg.reset_cb = &on_reset;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        // Pairing with no code ("Just Works"), keys kept (bonding).
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
        ble_hs_cfg.sm_bonding = 1;
        ble_hs_cfg.sm_mitm = 0;
        ble_hs_cfg.sm_sc = 1;
        ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_store_config_init();
        nimble_port_freertos_init(&host_task);
        started_ = true;
        const size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "started: internal RAM free %u -> %u (%d used so far)", static_cast<unsigned>(before),
                 static_cast<unsigned>(after), static_cast<int>(before) - static_cast<int>(after));
        return true;
    }

    uint32_t lost() const { return input_.lost(); }

    /* -- The port: the UI task -- */

    bool on() override { return started_; }

    void listen(bt::Listen how) override
    {
        const bool restart = how != listen_ && how != bt::Listen::Off && listen_ != bt::Listen::Off;
        listen_ = how;
        if (restart && synced_ && ble_gap_disc_active()) {
            ble_gap_disc_cancel(); // started again below, at the other pace
        }
        apply_scan();
    }

    int found(bt::Found *out, int max) override
    {
        const int64_t now = esp_timer_get_time();
        Guard g(mutex_);
        int n = 0;
        for (Heard &h : heard_) {
            if (h.used && now - h.at_us > kHeardUs) {
                h.used = false;
            }
            if (h.used && n < max) {
                out[n++] = h.found;
            }
        }
        return n;
    }

    bool connect(const bt::Address &address) override
    {
        if (!synced_ || connecting_) {
            return false;
        }
        int slot = -1;
        ble_addr_t peer = ble_address(address);
        {
            Guard g(mutex_);
            for (int i = 0; i < kMaxConns; i++) {
                if (conns_[i].used && bt::same(conns_[i].link.address, address)) {
                    return false;
                }
                if (!conns_[i].used && slot < 0) {
                    slot = i;
                }
            }
            if (slot < 0) {
                return false;
            }
            Conn &c = conns_[slot];
            c = Conn{};
            c.used = true;
            c.handle = BLE_HS_CONN_HANDLE_NONE;
            c.link.address = address;
            c.link.state = bt::LinkState::Connecting;
            for (const Heard &h : heard_) {
                if (h.used && bt::same(h.found.address, address)) {
                    memcpy(c.link.name, h.found.name, sizeof(c.link.name));
                    peer.type = h.type; // as it advertised
                }
            }
            connecting_ = true;
        }
        // Not while listening: the radio does one or the other.
        ble_gap_disc_cancel();
        ble_gap_conn_params link = {};
        link.scan_itvl = 0x0010; // NimBLE's own, while it looks for the device
        link.scan_window = 0x0010;
        link.itvl_min = kLinkIntervalMin;
        link.itvl_max = kLinkIntervalMax;
        link.latency = BLE_GAP_INITIAL_CONN_LATENCY;
        link.supervision_timeout = BLE_GAP_INITIAL_SUPERVISION_TIMEOUT; // 2.56 s
        link.min_ce_len = BLE_GAP_INITIAL_CONN_MIN_CE_LEN;
        link.max_ce_len = BLE_GAP_INITIAL_CONN_MAX_CE_LEN;
        const int rc = ble_gap_connect(own_address_type_, &peer, kConnectMs, &link, &on_gap,
                                       reinterpret_cast<void *>(static_cast<intptr_t>(slot)));
        if (rc != 0) {
            ESP_LOGW(TAG, "connect did not start: %d", rc);
            {
                Guard g(mutex_);
                conns_[slot].used = false;
                connecting_ = false;
            }
            apply_scan();
            return false;
        }
        return true;
    }

    void disconnect(const bt::Address &address) override
    {
        uint16_t handle = BLE_HS_CONN_HANDLE_NONE;
        bool found = false;
        {
            Guard g(mutex_);
            for (const Conn &c : conns_) {
                if (c.used && bt::same(c.link.address, address)) {
                    handle = c.handle;
                    found = true;
                }
            }
        }
        if (!found) {
            return;
        }
        if (handle == BLE_HS_CONN_HANDLE_NONE) {
            ble_gap_conn_cancel(); // still on its way: the connect event frees it
        } else {
            ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    }

    int links(bt::Link *out, int max) override
    {
        Guard g(mutex_);
        int n = 0;
        for (const Conn &c : conns_) {
            if (c.used && n < max) {
                out[n++] = c.link;
            }
        }
        return n;
    }

    void forget(const bt::Address &address) override
    {
        disconnect(address);
        if (!synced_) {
            return;
        }
        // The keys are kept under the address the device gave when pairing.
        ble_addr_t peer = ble_address(address);
        ble_store_util_delete_peer(&peer);
    }

private:
    struct Heard {
        bt::Found found;
        int64_t at_us;
        uint8_t type; // the address's kind as NimBLE gave it
        bool used;
    };
    struct Report {
        uint16_t value;     // the handle its notifications carry
        uint16_t end;       // the last handle that belongs to it
        uint16_t reference; // its Report Reference descriptor
        uint16_t config;    // its Client Characteristic Configuration descriptor
        uint8_t id;
        uint8_t type;       // 1 input, 2 output, 3 feature
    };
    struct Conn {
        bool used;          // (mutex)
        bt::Link link;      // (mutex)
        uint16_t handle;    // (mutex) BLE_HS_CONN_HANDLE_NONE until connected
        // The rest only NimBLE's task touches.
        uint16_t service_start, service_end;
        uint16_t map_handle;
        Report reports[kMaxReports];
        int report_count;
        int open;           // the report whose end the next characteristic tells; -1 none
        int cursor;         // the report the request under way is about
        size_t map_size;
        bool layout_known;
        hid::Decoder decoder;
    };

    static Radio &self();

    Conn *by_handle(uint16_t handle)
    {
        for (Conn &c : conns_) {
            if (c.used && c.handle == handle) {
                return &c;
            }
        }
        return nullptr;
    }
    uint8_t *map_of(const Conn &c) { return maps_[&c - conns_]; }

    void set_state(Conn &c, bt::LinkState state)
    {
        Guard g(mutex_);
        c.link.state = state;
    }

    // Either task. Listens if it is wanted and no connection is being made.
    void apply_scan()
    {
        if (!synced_) {
            return;
        }
        const bt::Listen how = listen_;
        const bool want = how != bt::Listen::Off && !connecting_;
        if (want && !ble_gap_disc_active()) {
            ble_gap_disc_params params = {};
            params.itvl = how == bt::Listen::Search ? kSearchInterval : kBackgroundInterval;
            params.window = how == bt::Listen::Search ? kSearchWindow : kBackgroundWindow;
            params.passive = 0;           // asks each device for its name
            params.filter_duplicates = 0; // heard again and again, so we know it is still there
            const int rc = ble_gap_disc(own_address_type_, BLE_HS_FOREVER, &params, &on_gap, nullptr);
            if (rc != 0 && rc != BLE_HS_EALREADY) {
                ESP_LOGW(TAG, "listening did not start: %d", rc);
            }
        } else if (!want && ble_gap_disc_active()) {
            ble_gap_disc_cancel();
        }
    }

    /* -- Everything below: NimBLE's task -- */

    static void host_task(void *)
    {
        nimble_port_run();
        nimble_port_freertos_deinit();
    }
    static void on_reset(int reason) { ESP_LOGW(TAG, "the stack reset itself: %d", reason); }
    static void on_sync()
    {
        Radio &r = self();
        int rc = ble_hs_util_ensure_addr(0);
        if (rc == 0) {
            rc = ble_hs_id_infer_auto(0, &r.own_address_type_);
        }
        if (rc != 0) {
            ESP_LOGE(TAG, "no address of our own: %d", rc);
            return;
        }
        r.synced_ = true;
        r.apply_scan();
    }

    void heard(const ble_gap_disc_desc &d)
    {
        const bool connectable =
            d.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND || d.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
        const bool reply = d.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
        if (!connectable && !reply) {
            return;
        }
        ble_hs_adv_fields fields = {};
        if (d.length_data > 0 && ble_hs_adv_parse_fields(&fields, d.data, d.length_data) != 0) {
            return;
        }
        const bt::Address address = address_of(d.addr);
        const int64_t now = esp_timer_get_time();
        Guard g(mutex_);
        Heard *slot = nullptr;
        for (Heard &h : heard_) {
            if (h.used && bt::same(h.found.address, address)) {
                slot = &h;
            }
        }
        if (slot == nullptr) {
            if (reply) {
                return; // an answer to a question about something not listed
            }
            // A free place, or the one heard longest ago.
            slot = &heard_[0];
            for (Heard &h : heard_) {
                if (!h.used) {
                    slot = &h;
                    break;
                }
                if (h.at_us < slot->at_us) {
                    slot = &h;
                }
            }
            *slot = Heard{};
            slot->used = true;
            slot->found.address = address;
        }
        bt::Found &f = slot->found;
        slot->type = d.addr.type;
        f.rssi = d.rssi;
        if (fields.name != nullptr && fields.name_len > 0) {
            const size_t n = fields.name_len < bt::kNameMax ? fields.name_len : bt::kNameMax;
            memcpy(f.name, fields.name, n);
            f.name[n] = 0;
        }
        if (connectable) {
            slot->at_us = now;
            // What it says it is. A device calling its own host by name
            // (a directed advertisement) says nothing: nothing changes.
            if (d.length_data > 0) {
                bool hid = false;
                uint8_t kinds = 0;
                for (int i = 0; i < fields.num_uuids16; i++) {
                    hid = hid || ble_uuid_u16(&fields.uuids16[i].u) == kHidService;
                }
                if (fields.appearance_is_present) {
                    // 0x03C0 to 0x03FF: human interface devices; 0x03C1 a
                    // keyboard, 0x03C2 a mouse.
                    hid = hid || (fields.appearance & 0xFFC0) == 0x03C0;
                    kinds = fields.appearance == 0x03C1 ? hid::kKeyboard : (fields.appearance == 0x03C2 ? hid::kMouse : 0);
                }
                f.hid = hid;
                f.kinds = kinds;
                f.pairing = (fields.flags & (BLE_HS_ADV_F_DISC_LTD | BLE_HS_ADV_F_DISC_GEN)) != 0;
            }
        }
    }

    // The connection ends here, whatever stage it was at: the disconnect
    // event frees its place.
    void give_up(Conn &c, const char *why)
    {
        ESP_LOGW(TAG, "%s: %s", c.link.name[0] != 0 ? c.link.name : "device", why);
        ble_gap_terminate(c.handle, BLE_ERR_REM_USER_CONN_TERM);
    }

    // Wi-Fi and Bluetooth take turns at the one radio. With Wi-Fi's power
    // saving off (wifi.cpp: a remote screen's clicks lag with it) Wi-Fi
    // keeps its whole share of every turn, and a mouse was heard in bursts
    // 40 to 50 ms apart: the pointer jumped (the board, 2026-11-19; with
    // Wi-Fi off it did not). So while a Bluetooth device is connected Wi-Fi
    // rests between the access point's beacons unless it has something to
    // send, and is put back as it was when the last device goes.
    // `wifi_rests = false` in bluetooth.ini leaves Wi-Fi alone.
    void share_radio()
    {
        int connected = 0;
        {
            Guard g(mutex_);
            for (const Conn &c : conns_) {
                connected += c.used && c.handle != BLE_HS_CONN_HANDLE_NONE ? 1 : 0;
            }
        }
        const bool rest = wifi_rests_ && connected > 0;
        if (rest != wifi_resting_) {
            wifi_resting_ = rest;
            const esp_err_t err = esp_wifi_set_ps(rest ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
            ESP_LOGI(TAG, "Wi-Fi %s (%s)", rest ? "rests between beacons" : "never rests", esp_err_to_name(err));
        }
    }

    // The pace of a connection, in the log: the board check reads it.
    void log_pace(uint16_t handle, const char *when)
    {
        ble_gap_conn_desc d = {};
        if (ble_gap_conn_find(handle, &d) == 0) {
            ESP_LOGI(TAG, "%s: every %u us, may skip %u, lost after %u ms", when,
                     static_cast<unsigned>(d.conn_itvl) * 1250u, static_cast<unsigned>(d.conn_latency),
                     static_cast<unsigned>(d.supervision_timeout) * 10u);
        }
    }

    static int on_gap(ble_gap_event *event, void *arg)
    {
        Radio &r = self();
        switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            r.heard(event->disc);
            return 0;
        case BLE_GAP_EVENT_CONNECT: {
            Conn &c = r.conns_[reinterpret_cast<intptr_t>(arg)];
            bool up = event->connect.status == 0;
            {
                Guard g(r.mutex_);
                r.connecting_ = false;
                if (up) {
                    c.handle = event->connect.conn_handle;
                    c.link.state = bt::LinkState::Securing;
                } else {
                    c.used = false;
                }
            }
            if (up) {
                ESP_LOGI(TAG, "%s: connected", c.link.name[0] != 0 ? c.link.name : "device");
                r.log_pace(c.handle, "connected");
                const int rc = ble_gap_security_initiate(c.handle);
                if (rc != 0 && rc != BLE_HS_EALREADY) {
                    r.give_up(c, "security did not start");
                }
            } else {
                ESP_LOGI(TAG, "connect failed: %d", event->connect.status);
            }
            r.share_radio();
            r.apply_scan();
            return 0;
        }
        case BLE_GAP_EVENT_CONN_UPDATE:
            // The device asked for another pace and got it (asking is its
            // right, and it is granted as asked: NimBLE's default).
            if (event->conn_update.status == 0) {
                r.log_pace(event->conn_update.conn_handle, "pace changed");
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT: {
            Conn *c = r.by_handle(event->disconnect.conn.conn_handle);
            if (c != nullptr) {
                ESP_LOGI(TAG, "%s: disconnected (%d)", c->link.name[0] != 0 ? c->link.name : "device",
                         event->disconnect.reason);
                c->decoder.release_all(r.input_); // what it held down comes up
                Guard g(r.mutex_);
                c->used = false;
            }
            r.share_radio();
            r.apply_scan();
            return 0;
        }
        case BLE_GAP_EVENT_ENC_CHANGE: {
            Conn *c = r.by_handle(event->enc_change.conn_handle);
            if (c == nullptr) {
                return 0;
            }
            if (event->enc_change.status != 0) {
                // Our keys and its keys no longer match (it was paired
                // again elsewhere, or reset): ours go, and the next time
                // it is paired afresh.
                ble_gap_conn_desc desc;
                if (ble_gap_conn_find(c->handle, &desc) == 0) {
                    ble_store_util_delete_peer(&desc.peer_id_addr);
                }
                ESP_LOGW(TAG, "encryption failed: %d", event->enc_change.status);
                r.give_up(*c, "pairing failed");
            } else if (c->link.state == bt::LinkState::Securing) {
                r.set_state(*c, bt::LinkState::Reading);
                if (ble_gattc_disc_svc_by_uuid(c->handle, &kHidServiceUuid.u, &on_service, nullptr) != 0) {
                    r.give_up(*c, "cannot ask for its services");
                }
            }
            return 0;
        }
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // It pairs anew although we have keys for it: the old ones go.
            ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_NOTIFY_RX: {
            Conn *c = r.by_handle(event->notify_rx.conn_handle);
            if (c == nullptr || !c->layout_known) {
                return 0;
            }
            for (int i = 0; i < c->report_count; i++) {
                const Report &report = c->reports[i];
                if (report.value == event->notify_rx.attr_handle && report.type == 1) {
                    uint8_t data[kMaxReport];
                    uint16_t size = 0;
                    if (ble_hs_mbuf_to_flat(event->notify_rx.om, data, sizeof(data), &size) == 0) {
                        c->decoder.report(report.id, data, size, r.input_);
                    }
                    break;
                }
            }
            return 0;
        }
        default:
            return 0; // a device asking for other connection timings is given them
        }
    }

    /* -- Asking a device what its reports look like, one request after another -- */

    static int on_service(uint16_t handle, const ble_gatt_error *error, const ble_gatt_svc *service, void *)
    {
        Radio &r = self();
        Conn *c = r.by_handle(handle);
        if (c == nullptr) {
            return 0;
        }
        if (error->status == 0) {
            if (c->service_end == 0) {
                c->service_start = service->start_handle;
                c->service_end = service->end_handle;
            }
            return 0;
        }
        if (error->status != BLE_HS_EDONE || c->service_end == 0) {
            r.give_up(*c, "not a keyboard or a mouse (no HID service)");
            return 0;
        }
        c->open = -1;
        if (ble_gattc_disc_all_chrs(handle, c->service_start, c->service_end, &on_characteristic, nullptr) != 0) {
            r.give_up(*c, "cannot ask for its characteristics");
        }
        return 0;
    }

    static int on_characteristic(uint16_t handle, const ble_gatt_error *error, const ble_gatt_chr *chr, void *)
    {
        Radio &r = self();
        Conn *c = r.by_handle(handle);
        if (c == nullptr) {
            return 0;
        }
        if (error->status == 0) {
            if (c->open >= 0) {
                c->reports[c->open].end = static_cast<uint16_t>(chr->def_handle - 1);
                c->open = -1;
            }
            const uint16_t uuid = ble_uuid_u16(&chr->uuid.u);
            if (uuid == kReportMap) {
                c->map_handle = chr->val_handle;
            } else if (uuid == kReport && c->report_count < kMaxReports) {
                c->open = c->report_count++;
                // Without a Report Reference it is taken for the one input report.
                c->reports[c->open] = {chr->val_handle, c->service_end, 0, 0, 0, 1};
            }
            return 0;
        }
        if (error->status != BLE_HS_EDONE) {
            r.give_up(*c, "its characteristics cannot be read");
            return 0;
        }
        c->cursor = 0;
        r.next_descriptors(*c);
        return 0;
    }

    void next_descriptors(Conn &c)
    {
        for (; c.cursor < c.report_count; c.cursor++) {
            const Report &report = c.reports[c.cursor];
            if (report.end > report.value &&
                ble_gattc_disc_all_dscs(c.handle, report.value, report.end, &on_descriptor, nullptr) == 0) {
                return;
            }
        }
        c.cursor = 0;
        next_reference(c);
    }

    static int on_descriptor(uint16_t handle, const ble_gatt_error *error, uint16_t, const ble_gatt_dsc *dsc, void *)
    {
        Radio &r = self();
        Conn *c = r.by_handle(handle);
        if (c == nullptr || c->cursor >= c->report_count) {
            return 0;
        }
        if (error->status == 0) {
            const uint16_t uuid = ble_uuid_u16(&dsc->uuid.u);
            if (uuid == kReportReference) {
                c->reports[c->cursor].reference = dsc->handle;
            } else if (uuid == kClientConfig) {
                c->reports[c->cursor].config = dsc->handle;
            }
            return 0;
        }
        c->cursor++; // done, or it would not say: the next report
        r.next_descriptors(*c);
        return 0;
    }

    void next_reference(Conn &c)
    {
        for (; c.cursor < c.report_count; c.cursor++) {
            if (c.reports[c.cursor].reference != 0 &&
                ble_gattc_read(c.handle, c.reports[c.cursor].reference, &on_reference, nullptr) == 0) {
                return;
            }
        }
        c.map_size = 0;
        if (c.map_handle == 0 || ble_gattc_read_long(c.handle, c.map_handle, 0, &on_map, nullptr) != 0) {
            give_up(c, "it has no report map");
        }
    }

    static int on_reference(uint16_t handle, const ble_gatt_error *error, ble_gatt_attr *attr, void *)
    {
        Radio &r = self();
        Conn *c = r.by_handle(handle);
        if (c == nullptr || c->cursor >= c->report_count || error->status == BLE_HS_EDONE) {
            return 0;
        }
        uint8_t two[2];
        uint16_t size = 0;
        if (error->status == 0 && attr != nullptr && ble_hs_mbuf_to_flat(attr->om, two, sizeof(two), &size) == 0 &&
            size == 2) {
            c->reports[c->cursor].id = two[0];
            c->reports[c->cursor].type = two[1];
        }
        c->cursor++;
        r.next_reference(*c);
        return 0;
    }

    static int on_map(uint16_t handle, const ble_gatt_error *error, ble_gatt_attr *attr, void *)
    {
        Radio &r = self();
        Conn *c = r.by_handle(handle);
        if (c == nullptr) {
            return 0;
        }
        if (error->status == 0 && attr != nullptr) {
            uint16_t size = 0;
            const size_t room = kMaxMap - c->map_size;
            if (room > 0 && ble_hs_mbuf_to_flat(attr->om, r.map_of(*c) + c->map_size,
                                                static_cast<uint16_t>(room), &size) == 0) {
                c->map_size += size;
            }
            return 0;
        }
        if (error->status != BLE_HS_EDONE || !c->decoder.set_layout(r.map_of(*c), c->map_size)) {
            r.give_up(*c, "its reports are not a keyboard's or a mouse's");
            return 0;
        }
        c->layout_known = true;
        c->cursor = 0;
        r.next_subscription(*c);
        return 0;
    }

    void next_subscription(Conn &c)
    {
        static const uint8_t kNotify[2] = {1, 0};
        for (; c.cursor < c.report_count; c.cursor++) {
            const Report &report = c.reports[c.cursor];
            if (report.type == 1 && report.config != 0 &&
                ble_gattc_write_flat(c.handle, report.config, kNotify, sizeof(kNotify), &on_subscribed, nullptr) == 0) {
                return;
            }
        }
        {
            Guard g(mutex_);
            c.link.kinds = c.decoder.layout().kinds();
            c.link.state = bt::LinkState::Ready;
        }
        ESP_LOGI(TAG, "%s: ready, a %s, %d reports, map of %u bytes", c.link.name[0] != 0 ? c.link.name : "device",
                 bt::kinds_text(c.link.kinds), c.report_count, static_cast<unsigned>(c.map_size));
    }

    static int on_subscribed(uint16_t handle, const ble_gatt_error *, ble_gatt_attr *, void *)
    {
        Radio &r = self();
        Conn *c = r.by_handle(handle);
        if (c != nullptr && c->link.state == bt::LinkState::Reading) {
            c->cursor++;
            r.next_subscription(*c);
        }
        return 0;
    }

    SemaphoreHandle_t mutex_ = nullptr;
    MergedInput input_;
    bool wifi_rests_ = true;    // the setting
    bool wifi_resting_ = false; // NimBLE's task: what Wi-Fi was last told
    volatile bool started_ = false, synced_ = false;
    volatile bt::Listen listen_ = bt::Listen::Off;
    volatile bool connecting_ = false; // (mutex to change)
    uint8_t own_address_type_ = 0;
    Heard heard_[kMaxHeard] = {};
    Conn conns_[kMaxConns] = {};
    uint8_t maps_[kMaxConns][kMaxMap] = {};
};

// Made when Bluetooth starts, in external RAM (nothing in it is touched
// from an interrupt): a board with Bluetooth turned off gives it no memory.
Radio *s_radio = nullptr;

Radio &Radio::self() { return *s_radio; }

// The port of a board whose Bluetooth is off or did not start.
class NoRadio : public bt::Port {
public:
    bool on() override { return false; }
    void listen(bt::Listen) override {}
    int found(bt::Found *, int) override { return 0; }
    bool connect(const bt::Address &) override { return false; }
    void disconnect(const bt::Address &) override {}
    int links(bt::Link *, int) override { return 0; }
    void forget(const bt::Address &) override {}
};
NoRadio s_none;

} // namespace

bool bluetooth_begin(ports::InputSink &sink, bool wifi_rests)
{
    if (s_radio != nullptr) {
        return s_radio->on();
    }
    void *room = heap_caps_calloc(1, sizeof(Radio), MALLOC_CAP_SPIRAM);
    if (room == nullptr) {
        ESP_LOGE(TAG, "no memory for the radio's tables");
        return false;
    }
    s_radio = new (room) Radio;
    return s_radio->begin(sink, wifi_rests);
}
bt::Port &bluetooth_port()
{
    if (s_radio != nullptr) {
        return *s_radio;
    }
    return s_none;
}
uint32_t bluetooth_lost_events() { return s_radio != nullptr ? s_radio->lost() : 0; }
