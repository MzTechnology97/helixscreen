// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <lvgl.h>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @brief K2-OpenHost CFS settings page: every device action the CFS backend
 * declares (switches such as clog detection or unload after print, commands
 * such as refresh RFID or clean nozzle), plus links to the runout swap page
 * and the stock device operations (home, recover, abort).
 *
 * The stock UI reaches none of these on the CFS: the backend lists them in
 * get_device_actions() but declares no device sections.
 */
class K2ohCfsSettingsView {
  public:
    struct Actions {
        std::function<void()> back;
        std::function<void()> open_runout;
        std::function<void()> open_device_operations;
    };

    explicit K2ohCfsSettingsView(Actions actions);
    ~K2ohCfsSettingsView();

    void create(lv_obj_t* panel);
    void show();
    void hide();
    [[nodiscard]] bool visible() const;
    /// Rebuilds the rows from the backend's current actions and values.
    void refresh();

  private:
    lv_obj_t* add_row(lv_obj_t* list, const std::string& title, const std::string& detail);

    static void on_click(lv_event_t* e);
    static void on_toggle(lv_event_t* e);
    static void on_run(lv_event_t* e);
    static void on_link(lv_event_t* e);
    static void on_root_deleted(lv_event_t* e);

    Actions actions_;
    lv_obj_t* root_ = nullptr;
    /// Action ids, indexed by the row widgets' user data.
    std::vector<std::string> ids_;
};

} // namespace helix::ui
