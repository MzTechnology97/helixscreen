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
 * @brief K2-OpenHost print status: progress ring with the thumbnail, times,
 * the spool in use and the next one, temperatures, speed/flow/fan and the
 * controls.
 *
 * Lives inside the print status overlay's content and covers it on a K2 with
 * Kalico's CFS (K2ohCfsView::applies()); the overlay header stays.
 */
class K2ohPrintView {
  public:
    struct Actions {
        std::function<void()> tune;
        std::function<void()> open_cfs;
    };

    K2ohPrintView(PrinterState& printer_state, Actions actions);
    ~K2ohPrintView();

    /// Builds the view inside @p content (once) and starts following state.
    void create(lv_obj_t* content);
    /// Shows it (and hides the stock content) while K2ohCfsView::applies().
    void update_visibility();

  private:
    void refresh();
    static void on_click(lv_event_t* e);
    static void on_timer(lv_timer_t* timer);

    PrinterState& printer_state_;
    Actions actions_;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* arc_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    std::vector<ObserverGuard> observers_;
};

} // namespace helix::ui
