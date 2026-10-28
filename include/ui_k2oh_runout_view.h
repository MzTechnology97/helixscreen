// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <lvgl.h>
#include <string>
#include <vector>

namespace helix::ui {

/**
 * @brief K2-OpenHost runout swap page: the swap switch, automatic or manual
 * sequence, and the order of each group of identical spools.
 *
 * Kalico decides the groups (same material and colour). Automatic sends
 * `_BOX_SET_RUNOUT_ORDER ORDER=AUTO` (lowest RFID share first, then slot
 * order); manual lets the user arrange each group and saves the order with
 * `_BOX_SET_RUNOUT_ORDER ORDER=<slots>`.
 */
class K2ohRunoutView {
  public:
    explicit K2ohRunoutView(std::function<void()> on_back);
    ~K2ohRunoutView();

    void create(lv_obj_t* panel);
    void show();
    void hide();
    [[nodiscard]] bool visible() const;
    /// Re-reads Kalico's groups; keeps unsaved manual edits.
    void refresh();

  private:
    struct Group {
        std::string material;
        std::string color;
        std::string strategy;
        std::vector<int> slots;
    };

    void rebuild_groups();
    void update_mode_buttons();
    void move(size_t group, size_t index, int delta);
    void save_order();
    void send(const std::string& gcode);

    static void on_click(lv_event_t* e);
    static void on_move(lv_event_t* e);
    static void on_switch(lv_event_t* e);
    static void on_root_deleted(lv_event_t* e);

    std::function<void()> on_back_;
    lv_obj_t* root_ = nullptr;
    std::vector<Group> groups_;
    bool manual_ = false;
    bool dirty_ = false;
    bool syncing_ = false;
};

} // namespace helix::ui
