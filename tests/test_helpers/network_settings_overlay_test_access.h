// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_overlay_network_settings.h"

#include "wifi_backend.h"

#include <cstring>
#include <string>
#include <vector>

/**
 * @brief Reaches the overlay's cached modal pointers.
 *
 * These are the pointers #1341 crashed on: raw lv_obj_t* the async connect
 * callbacks null-check and then walk. A test needs to see them directly,
 * because "the pointer is null" is the whole invariant the fix installs — the
 * dangling case is indistinguishable from the live one at the call site.
 */
class NetworkSettingsOverlayTestAccess {
  public:
    static lv_obj_t*& password_modal(NetworkSettingsOverlay& o) {
        return o.password_modal_;
    }
    static lv_obj_t*& hidden_network_modal(NetworkSettingsOverlay& o) {
        return o.hidden_network_modal_;
    }
    static lv_obj_t*& test_modal(NetworkSettingsOverlay& o) {
        return o.test_modal_;
    }
    static lv_obj_t*& step_widget(NetworkSettingsOverlay& o) {
        return o.step_widget_;
    }
    /// Arm the production DELETE handler on an arbitrary object, exactly as
    /// show_password_modal() and friends do.
    static void watch(NetworkSettingsOverlay& o, lv_obj_t* modal) {
        lv_obj_add_event_cb(modal, NetworkSettingsOverlay::on_modal_deleted, LV_EVENT_DELETE, &o);
    }
    /// The overlay's manager pointer — tests point it at a locally owned
    /// WiFiManager instead of the process-global singleton, before create().
    static std::shared_ptr<helix::WiFiManager>& wifi_manager(NetworkSettingsOverlay& o) {
        return o.wifi_manager_;
    }
    /// The wifi_connected subject (0/1) — the observable a transport refresh
    /// must move.
    static lv_subject_t& wifi_connected(NetworkSettingsOverlay& o) {
        return o.wifi_connected_;
    }
    /// Apply a status read's answer the way the async read does.
    static void apply_wifi_status(NetworkSettingsOverlay& o,
                                  const WifiBackend::ConnectionStatus& status) {
        o.apply_wifi_status(status);
    }
    /// The SSID the password modal is about. The handlers read it rather than
    /// taking it as an argument, so a test has to seed it.
    static void set_current_ssid(NetworkSettingsOverlay& o, const std::string& ssid) {
        strncpy(o.current_ssid_, ssid.c_str(), sizeof(o.current_ssid_) - 1);
        o.current_ssid_[sizeof(o.current_ssid_) - 1] = '\0';
    }
    /// The production handler behind the password modal's Connect button.
    static void password_connect_clicked(NetworkSettingsOverlay& o) {
        o.handle_password_connect_clicked();
    }
    /// Open the hidden-network modal, exactly as the "Add other network"
    /// button does.
    static void add_other_clicked(NetworkSettingsOverlay& o) {
        o.handle_add_other_clicked();
    }
    /// Drive the hidden-network modal's Connect button.
    static void hidden_connect_clicked(NetworkSettingsOverlay& o) {
        o.handle_hidden_connect_clicked();
    }
    /// Build the scan rows synchronously, with no network connected — the click
    /// handler reads per-row data only this path attaches.
    static void populate(NetworkSettingsOverlay& o, const std::vector<WiFiNetwork>& networks) {
        o.build_network_list(networks, WifiBackend::ConnectionStatus{});
    }
    /// What a completed scan does: the rows are built once the connection
    /// read lands.
    static void scan_completed(NetworkSettingsOverlay& o,
                               const std::vector<WiFiNetwork>& networks) {
        o.populate_network_list(networks);
    }
    static void update_wifi_status(NetworkSettingsOverlay& o) {
        o.update_wifi_status();
    }
    static void forget_clicked(NetworkSettingsOverlay& o) {
        o.handle_network_settings_forget();
    }
    /// The Forget dialog's confirm button, for `ssid`.
    static void forget_confirmed(NetworkSettingsOverlay& o, const std::string& ssid) {
        o.pending_forget_ssid_ = ssid;
        o.handle_network_forget_confirm();
    }
    /// Drive the production WLAN toggle handler for THIS overlay, from a switch
    /// left in `checked`. The XML trampoline resolves the overlay through the
    /// process-wide accessor, so an instance-scoped test needs its own event.
    static void wlan_toggle_changed(NetworkSettingsOverlay& o, lv_obj_t* sw, bool checked) {
        if (checked) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(
            sw,
            [](lv_event_t* e) {
                static_cast<NetworkSettingsOverlay*>(lv_event_get_user_data(e))
                    ->handle_wlan_toggle_changed(e);
            },
            LV_EVENT_VALUE_CHANGED, &o);
        lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
    }
    /// Tap a row built by populate(), on THIS overlay. The row's XML click
    /// callback goes to the process-wide overlay, so the tap rides an event
    /// code of its own.
    static void network_item_clicked(NetworkSettingsOverlay& o, lv_obj_t* item) {
        static const auto tap = static_cast<lv_event_code_t>(lv_event_register_id());
        lv_obj_add_event_cb(
            item,
            [](lv_event_t* e) {
                static_cast<NetworkSettingsOverlay*>(lv_event_get_user_data(e))
                    ->handle_network_item_clicked(e);
            },
            tap, &o);
        lv_obj_send_event(item, tap, nullptr);
    }
    static const std::string& pending_forget_ssid(NetworkSettingsOverlay& o) {
        return o.pending_forget_ssid_;
    }
    static std::string ssid(NetworkSettingsOverlay& o) {
        return o.ssid_buffer_;
    }
    static std::string ip(NetworkSettingsOverlay& o) {
        return o.ip_buffer_;
    }
    static std::string mac(NetworkSettingsOverlay& o) {
        return o.mac_buffer_;
    }
    static lv_obj_t* networks_list(NetworkSettingsOverlay& o) {
        return o.networks_list_;
    }
};
