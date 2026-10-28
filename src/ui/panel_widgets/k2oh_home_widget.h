// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include "panel_widget.h"

#include <lvgl.h>
#include <string>
#include <vector>

namespace helix {

class PrinterState;

/**
 * @brief K2-OpenHost Home widgets: the job, the temperatures, the CFS spools,
 * the light and the fans, in the K2-OpenHost look.
 *
 * One class for the five ids (k2oh_job, k2oh_temps, k2oh_cfs, k2oh_light,
 * k2oh_fans): they share the helpers and differ only in what they show. Each is
 * an ordinary grid widget, so the Home stays customisable.
 */
class K2ohHomeWidget : public PanelWidget {
  public:
    enum class Kind { Job, Temps, Cfs, Light, Fans };

    K2ohHomeWidget(PrinterState& printer_state, Kind kind, std::string id);
    ~K2ohHomeWidget() override;

    void attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) override;
    void detach() override;
    const char* id() const override {
        return id_.c_str();
    }
    std::string get_component_name() const override {
        return "panel_widget_" + id_;
    }

  protected:
    void on_hooked_root_deleted() override;

  private:
    struct Spool {
        lv_obj_t* root = nullptr;
        lv_obj_t* ring = nullptr;
        lv_obj_t* core = nullptr;
        lv_obj_t* hole = nullptr;
        lv_obj_t* material = nullptr;
        lv_obj_t* percent = nullptr;
    };

    void refresh();
    void refresh_job();
    void refresh_temps();
    void refresh_cfs();
    void refresh_light();
    void refresh_fans();
    void build_spools(int count);
    void release();

    static void on_click(lv_event_t* e);
    static void on_timer(lv_timer_t* timer);
    /// Light tile: a vertical drag sets the brightness (a tap still toggles).
    static void on_light_drag(lv_event_t* e);
    void show_brightness(int pct, bool on);

    PrinterState& printer_state_;
    Kind kind_;
    std::string id_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* parent_screen_ = nullptr;
    lv_timer_t* timer_ = nullptr;
    std::vector<Spool> spools_;
    // Light drag state.
    int32_t drag_start_y_ = 0;
    int drag_start_pct_ = 0;
    int drag_pct_ = 0;
    bool dragging_ = false;
    bool dragged_ = false;
    std::vector<ObserverGuard> observers_;
};

void register_k2oh_home_widgets();

} // namespace helix
