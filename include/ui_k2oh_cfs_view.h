// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_observer_guard.h"

#include <functional>
#include <lvgl.h>
#include <vector>

namespace helix {
class PrinterState;
}
struct SlotInfo;
struct AmsSystemInfo;

namespace helix::ui {

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
        std::function<void()> edit_external;
    };

    K2ohCfsView(PrinterState& printer_state, Actions actions);
    ~K2ohCfsView();

    /// True when the active backend is Kalico's CFS, the box this view knows.
    static bool applies();

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

    void build_cards(int count);
    void update_card(Card& card, const SlotInfo& slot, bool loaded, bool printing);
    void update_header(const AmsSystemInfo& info);
    void update_actions();
    void select(int slot);

    static void on_card_clicked(lv_event_t* e);
    static void on_button_clicked(lv_event_t* e);
    static void on_runout_changed(lv_event_t* e);
    static void on_back_clicked(lv_event_t* e);

    PrinterState& printer_state_;
    Actions actions_;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* cards_row_ = nullptr;
    std::vector<Card> cards_;
    int selected_ = -1;
    int loaded_slot_ = -1;
    bool syncing_switch_ = false;

    ObserverGuard slots_observer_;
    ObserverGuard action_observer_;
    ObserverGuard current_slot_observer_;
    ObserverGuard print_state_observer_;
};

} // namespace helix::ui
