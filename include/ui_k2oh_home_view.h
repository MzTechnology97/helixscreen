// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include <functional>
#include <lvgl.h>
#include <vector>

namespace helix {
class PrinterState;
}

namespace helix::ui {

/**
 * @brief K2-OpenHost Home: status line, the job card (progress and controls
 * while printing, print and preheat when idle), temperatures, the CFS spools,
 * the light and the fans.
 *
 * Lives inside the Home panel and covers its widget grid while the printer is
 * a K2 on Kalico's CFS (K2ohCfsView::applies()).
 */
class K2ohHomeView {
  public:
    struct Actions {
        std::function<void()> open_cfs;
        std::function<void()> open_print_status;
        std::function<void()> open_tune;
    };

    K2ohHomeView(PrinterState& printer_state, Actions actions);
    ~K2ohHomeView();

    /// Builds the view inside the Home panel (once) and starts following state.
    void create(lv_obj_t* panel);

  private:
    struct Spool {
        lv_obj_t* root = nullptr;
        lv_obj_t* ring = nullptr;
        lv_obj_t* core = nullptr;
        lv_obj_t* hole = nullptr;
        lv_obj_t* material = nullptr;
        lv_obj_t* percent = nullptr;
    };

    void update_visibility();
    void refresh_job();
    void refresh_temps();
    void refresh_cfs();
    void refresh_light_and_fans();
    void refresh_clock();
    void build_spools(int count);

    static void on_click(lv_event_t* e);
    static void on_timer(lv_timer_t* timer);

    PrinterState& printer_state_;
    Actions actions_;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* root_ = nullptr;
    std::vector<Spool> spools_;
    lv_timer_t* timer_ = nullptr;

    std::vector<ObserverGuard> observers_;
};

} // namespace helix::ui
