// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include <lvgl.h>
#include <vector>

namespace helix {
class PrinterState;
}

namespace helix::ui {

/**
 * @brief K2-OpenHost Home: a status line fixed above the widget grid (state,
 * name, clock), and the K2-OpenHost layout put on the grid once.
 *
 * The grid stays the stock one, so the Home remains customisable; the layout
 * only seeds it with the K2-OpenHost widgets (k2oh_job, k2oh_temps, k2oh_cfs,
 * k2oh_light, k2oh_fans) the first time the printer shows up as a K2 on
 * Kalico's CFS (K2ohCfsView::applies()).
 */
class K2ohHomeView {
  public:
    explicit K2ohHomeView(PrinterState& printer_state);
    ~K2ohHomeView();

    /// Builds the status line inside the Home panel (once) and starts following state.
    void create(lv_obj_t* panel);

  private:
    void update_visibility();
    void seed_layout_once();
    void refresh_state();
    void refresh_clock();
    /// The external spool's profile lives in Kalico on the K2: mirror Kalico's
    /// into the screen, and push a profile edited on the screen to Kalico.
    void sync_external_from_kalico();
    void push_external_to_kalico();

    static void on_timer(lv_timer_t* timer);
    static void on_root_deleted(lv_event_t* e);

    PrinterState& printer_state_;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    std::vector<ObserverGuard> observers_;
    bool mirroring_external_ = false; ///< set while writing Kalico's profile in
};

/**
 * @brief Put the K2-OpenHost layout on the Home grid (page 1), when the printer
 * is a K2 on Kalico's CFS. Used once at first start and by the catalog's Reset.
 * @return false (nothing changed) on any other printer
 */
bool k2oh_apply_home_layout();

} // namespace helix::ui
