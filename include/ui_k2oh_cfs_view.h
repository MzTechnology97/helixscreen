// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_k2oh_cfs_settings.h"
#include "ui_k2oh_runout_view.h"
#include "ui_observer_guard.h"

#include <functional>
#include <memory>
#include <lvgl.h>
#include <string>
#include <vector>

namespace helix {
class PrinterState;
}
struct SlotInfo;
struct AmsSystemInfo;

namespace helix::ui {

/// Opens the multi-filament panel (the K2-OpenHost CFS page on Kalico's box).
void k2oh_open_cfs_page();

/// Registers the XML callbacks of the K2-OpenHost navigation entries
/// (k2oh_nav_cfs_clicked). Call before navigation_bar.xml is parsed.
void k2oh_register_nav_callbacks();

/**
 * @brief K2-OpenHost CFS page: one card per spool, status chips, the runout
 * swap and the actions of the selected spool.
 *
 * Lives inside the AMS panel and takes its place when the box is Kalico's CFS
 * (Fork dialect). The actions go through the panel (load with preheat, unload,
 * slot editor, context menu), so they keep its guards.
 */
class K2ohCfsView {
  public:
    struct Actions {
        std::function<void(int)> load;
        std::function<void(int)> unload;
        std::function<void(int)> edit;
        std::function<void(int, lv_obj_t*)> more;
        /// The stock external spool (bypass) menu, anchored near @p anchor.
        std::function<void(lv_obj_t*)> external_menu;
    };

    K2ohCfsView(PrinterState& printer_state, Actions actions);
    ~K2ohCfsView();

    /// True when the active backend is Kalico's CFS, the box this view knows.
    static bool applies();

    /// "PETG: 4 (6%) » 2 (85%)": the spools that back each other up, the loaded
    /// one first; the generic endless-spool sentence when there are no groups.
    static std::string runout_summary(const AmsSystemInfo& info, int loaded_slot);

    /// Builds the view inside @p panel (once), hidden.
    void create(lv_obj_t* panel);
    void set_visible(bool visible);
    [[nodiscard]] bool visible() const;
    void refresh();

  private:
    struct Card {
        lv_obj_t* root = nullptr;
        lv_obj_t* ring = nullptr;
        int slot = -1;
    };
    struct PathRow {
        lv_obj_t* row = nullptr;
        lv_obj_t* dot = nullptr;
        lv_obj_t* icon = nullptr;
        lv_obj_t* title = nullptr;
        lv_obj_t* detail = nullptr;
    };

    void build_cards(int count);
    void build_path();
    void refresh_path(const AmsSystemInfo& info);
    /// Sends a click to the hidden stock sidebar button @p name (it keeps its guards).
    void click_sidebar(const char* name);
    void update_card(Card& card, const SlotInfo& slot, bool loaded, bool printing);
    void update_header(const AmsSystemInfo& info);
    void update_actions();
    void select(int slot);

    static void on_card_clicked(lv_event_t* e);
    static void on_button_clicked(lv_event_t* e);
    static void on_runout_changed(lv_event_t* e);
    static void on_bypass_changed(lv_event_t* e);
    static void on_back_clicked(lv_event_t* e);
    /// The panel deleted the tree: stop the observers that would reach it.
    static void on_root_deleted(lv_event_t* e);

    PrinterState& printer_state_;
    Actions actions_;
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* cards_row_ = nullptr;
    std::vector<PathRow> path_rows_;
    /// The runout swap page, opened from the runout row.
    std::unique_ptr<K2ohRunoutView> runout_;
    /// The CFS settings page, opened from the gear button.
    std::unique_ptr<K2ohCfsSettingsView> settings_;
    std::vector<Card> cards_;
    int selected_ = -1;
    int loaded_slot_ = -1;
    bool syncing_switch_ = false;

    ObserverGuard slots_observer_;
    ObserverGuard action_observer_;
    ObserverGuard current_slot_observer_;
    ObserverGuard print_state_observer_;
    /// Re-reads Kalico's operation stage while a load or unload runs.
    lv_timer_t* stage_timer_ = nullptr;
};

} // namespace helix::ui
