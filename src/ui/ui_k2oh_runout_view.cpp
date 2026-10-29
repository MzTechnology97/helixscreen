// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost runout swap page (see ui_k2oh_runout_view.h).

#include "ui_k2oh_runout_view.h"

#include "ui_button.h"
#include "ui_error_reporting.h"
#include "ui_icon.h"

#include "ams_backend.h"
#if HELIX_HAS_CFS
#include "ams_backend_cfs.h"
#endif
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <tuple>

namespace helix::ui {

namespace {

lv_obj_t* child(lv_obj_t* parent, const char* name) {
    return parent ? lv_obj_find_by_name(parent, name) : nullptr;
}

nlohmann::json box_field(const char* key) {
#if HELIX_HAS_CFS
    if (auto* cfs = dynamic_cast<printer::AmsBackendCfs*>(AmsState::instance().get_backend())) {
        return cfs->box_field(key);
    }
#endif
    return nlohmann::json();
}

lv_color_t parse_color(const std::string& hex) {
    const std::string digits = !hex.empty() && hex[0] == '#' ? hex.substr(1) : hex;
    return lv_color_hex(static_cast<uint32_t>(std::strtoul(digits.c_str(), nullptr, 16)));
}

const char* strategy_label(const std::string& strategy) {
    if (strategy == "manual_order") {
        return "manual order";
    }
    if (strategy == "lowest_remaining_first") {
        return "lowest remaining first";
    }
    return "slot order";
}

lv_obj_t* plain(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

} // namespace

K2ohRunoutView::K2ohRunoutView(std::function<void()> on_back) : on_back_(std::move(on_back)) {}

K2ohRunoutView::~K2ohRunoutView() {
    if (root_) {
        lv_obj_remove_event_cb_with_user_data(root_, on_root_deleted, this);
    }
}

void K2ohRunoutView::on_root_deleted(lv_event_t* e) {
    static_cast<K2ohRunoutView*>(lv_event_get_user_data(e))->root_ = nullptr;
}

void K2ohRunoutView::create(lv_obj_t* panel) {
    if (root_ || !panel) {
        return;
    }
    root_ = static_cast<lv_obj_t*>(lv_xml_create(panel, "k2oh_runout_view", nullptr));
    if (!root_) {
        spdlog::error("[K2oh Runout] k2oh_runout_view.xml did not build");
        return;
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, on_root_deleted, LV_EVENT_DELETE, this);
    for (const char* name : {"runout_back", "btn_mode_auto", "btn_mode_manual", "btn_save_order"}) {
        if (lv_obj_t* obj = child(root_, name)) {
            lv_obj_add_event_cb(obj, on_click, LV_EVENT_CLICKED, this);
        }
    }
    if (lv_obj_t* sw = child(root_, "swap_switch")) {
        lv_obj_add_event_cb(sw, on_switch, LV_EVENT_VALUE_CHANGED, this);
    }
}

void K2ohRunoutView::show() {
    if (!root_) {
        return;
    }
    dirty_ = false;
    groups_.clear();
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    refresh();
}

void K2ohRunoutView::hide() {
    if (root_) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool K2ohRunoutView::visible() const {
    return root_ && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void K2ohRunoutView::refresh() {
    if (!visible()) {
        return;
    }
    AmsBackend* backend = AmsState::instance().get_backend();
    if (backend) {
        if (lv_obj_t* sw = child(root_, "swap_switch")) {
            syncing_ = true;
            if (backend->get_system_info().endless_spool_enabled) {
                lv_obj_add_state(sw, LV_STATE_CHECKED);
            } else {
                lv_obj_remove_state(sw, LV_STATE_CHECKED);
            }
            syncing_ = false;
        }
    }
    if (dirty_) {
        rebuild_groups(); // keep the user's unsaved order, refresh the shares
        return;
    }
    groups_.clear();
    const nlohmann::json groups = box_field("runout_groups");
    bool any_manual = false;
    if (groups.is_array()) {
        for (const auto& g : groups) {
            if (!g.is_object() || !g.contains("slots") || !g["slots"].is_array()) {
                continue;
            }
            Group group;
            group.material = g.value("material", std::string());
            group.color = g.value("color", std::string());
            group.strategy = g.value("strategy", std::string());
            for (const auto& s : g["slots"]) {
                if (s.is_number_integer()) {
                    group.slots.push_back(s.get<int>());
                }
            }
            any_manual = any_manual || group.strategy == "manual_order";
            groups_.push_back(std::move(group));
        }
    }
    // Manual only while Kalico applies a saved order to a group: an order that
    // misses a spool of the group is stale, and Kalico treats the group as automatic.
    manual_ = any_manual;
    update_mode_buttons();
    rebuild_groups();
}

void K2ohRunoutView::update_mode_buttons() {
    // The chosen mode in the primary colour, the other neutral.
    auto mark = [](lv_obj_t* btn, bool on) {
        if (btn) {
            lv_obj_set_style_bg_color(btn, theme_manager_get_color(on ? "primary" : "elevated_bg"),
                                      LV_PART_MAIN);
        }
    };
    mark(child(root_, "btn_mode_auto"), !manual_);
    mark(child(root_, "btn_mode_manual"), manual_);
    if (lv_obj_t* hint = child(root_, "mode_hint")) {
        lv_label_set_text(hint, lv_tr(manual_ ? "Arrange each group with the arrows, then save."
                                              : "The spool with the least left (RFID) goes first, "
                                                "then slot order."));
    }
    if (lv_obj_t* save = child(root_, "btn_save_order")) {
        if (manual_ && dirty_) {
            lv_obj_remove_flag(save, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(save, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void K2ohRunoutView::rebuild_groups() {
    lv_obj_t* list = child(root_, "groups_list");
    if (!list) {
        return;
    }
    lv_obj_clean(list);
    AmsBackend* backend = AmsState::instance().get_backend();
    const AmsSystemInfo info = backend ? backend->get_system_info() : AmsSystemInfo{};
    int loaded = -1;
    for (const auto& unit : info.units) {
        for (const auto& slot : unit.slots) {
            if (slot.status == SlotStatus::LOADED) {
                loaded = slot.global_index;
            }
        }
    }

    if (groups_.empty()) {
        lv_obj_t* empty = lv_label_create(list);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_color(empty, theme_manager_get_color("text_muted"), LV_PART_MAIN);
        lv_label_set_text(empty, lv_tr("No spool to swap to: it takes two identical spools "
                                       "(same material and colour)."));
        return;
    }

    for (size_t g = 0; g < groups_.size(); ++g) {
        const Group& group = groups_[g];
        lv_obj_t* card = plain(list);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, theme_manager_get_color("card_bg"), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_gap(card, 6, LV_PART_MAIN);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

        lv_obj_t* head = plain(card);
        lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(head, 8, LV_PART_MAIN);
        lv_obj_t* swatch = plain(head);
        lv_obj_set_size(swatch, 18, 18);
        lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(swatch, parse_color(group.color), LV_PART_MAIN);
        lv_obj_t* title = lv_label_create(head);
        lv_label_set_text(title, group.material.c_str());
        lv_obj_set_style_text_color(title, theme_manager_get_color("text"), LV_PART_MAIN);
        lv_obj_t* strat = lv_label_create(head);
        lv_label_set_text(
            strat,
            (std::string("· ") + lv_tr(strategy_label(manual_ ? "manual_order" : group.strategy)))
                .c_str());
        lv_obj_set_style_text_color(strat, theme_manager_get_color("text_muted"), LV_PART_MAIN);

        // The sequence: one spool tile per slot, in the order they are used,
        // joined by an arrow.
        lv_obj_t* row = plain(card);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(row, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_top(row, 4, LV_PART_MAIN);
        const lv_color_t text_color = theme_manager_get_color("text");
        const lv_color_t muted = theme_manager_get_color("text_muted");
        const lv_color_t primary = theme_manager_get_color("primary");
        const lv_color_t track = theme_manager_get_color("elevated_bg");
        for (size_t i = 0; i < group.slots.size(); ++i) {
            const int slot = group.slots[i];
            const SlotInfo info_slot = backend ? backend->get_slot_info(slot) : SlotInfo{};
            const float raw = info_slot.get_remaining_percent();
            const int pct = raw < 0 ? -1 : std::clamp(static_cast<int>(std::lround(raw)), 0, 100);
            const lv_color_t filament = lv_color_hex(info_slot.color_rgb);
            const bool in_use = slot == loaded;

            if (i > 0) {
                lv_obj_t* link = plain(row);
                lv_obj_set_size(link, 34, 34);
                lv_obj_set_style_radius(link, LV_RADIUS_CIRCLE, LV_PART_MAIN);
                lv_obj_set_style_bg_opa(link, LV_OPA_COVER, LV_PART_MAIN);
                lv_obj_set_style_bg_color(link, track, LV_PART_MAIN);
                if (auto* icon = static_cast<lv_obj_t*>(lv_xml_create(link, "icon", nullptr))) {
                    ui_icon_set_source(icon, "chevron_right");
                    ui_icon_set_size(icon, "sm");
                    ui_icon_set_color(icon, primary, LV_OPA_COVER);
                    lv_obj_center(icon);
                }
            }

            lv_obj_t* tile = plain(row);
            lv_obj_set_size(tile, 132, LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(tile, 6, LV_PART_MAIN);
            lv_obj_set_style_pad_gap(tile, 4, LV_PART_MAIN);
            lv_obj_set_style_radius(tile, 12, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(tile, track, LV_PART_MAIN);
            lv_obj_set_style_border_width(tile, in_use ? 2 : 0, LV_PART_MAIN);
            lv_obj_set_style_border_color(tile, primary, LV_PART_MAIN);

            // Position in the sequence ("1st", "2nd"...) or "IN USE".
            lv_obj_t* pos = lv_label_create(tile);
            lv_label_set_text(pos, in_use ? lv_tr("IN USE")
                                          : (std::to_string(i + 1) + lv_tr(".")).c_str());
            lv_obj_set_style_text_color(pos, in_use ? primary : muted, LV_PART_MAIN);

            lv_obj_t* box = plain(tile);
            lv_obj_set_size(box, 66, 66);
            lv_obj_t* ring = lv_arc_create(box);
            lv_obj_set_size(ring, 66, 66);
            lv_obj_center(ring);
            lv_arc_set_rotation(ring, 270);
            lv_arc_set_bg_angles(ring, 0, 360);
            lv_arc_set_range(ring, 0, 100);
            lv_arc_set_value(ring, std::max(pct, 0));
            lv_obj_remove_style(ring, nullptr, LV_PART_KNOB);
            lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
            lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(ring, theme_manager_get_color("card_bg"), LV_PART_MAIN);
            lv_obj_set_style_arc_color(
                ring, pct >= 0 && pct <= 15 ? theme_manager_get_color("warning") : filament,
                LV_PART_INDICATOR);
            lv_obj_t* core = plain(box);
            lv_obj_set_size(core, 50, 50);
            lv_obj_center(core);
            lv_obj_set_style_radius(core, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(core, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(core, filament, LV_PART_MAIN);
            lv_obj_set_style_border_width(core, 3, LV_PART_MAIN);
            lv_obj_set_style_border_color(core, track, LV_PART_MAIN);
            lv_obj_t* hole = plain(core);
            lv_obj_set_size(hole, 14, 14);
            lv_obj_center(hole);
            lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(hole, track, LV_PART_MAIN);
            lv_obj_t* badge = plain(box);
            lv_obj_set_size(badge, 24, 24);
            lv_obj_align(badge, LV_ALIGN_TOP_LEFT, -2, -2);
            lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(badge, theme_manager_get_color("card_bg"), LV_PART_MAIN);
            lv_obj_t* num = lv_label_create(badge);
            lv_label_set_text(num, std::to_string(slot + 1).c_str());
            lv_obj_set_style_text_color(num, text_color, LV_PART_MAIN);
            lv_obj_center(num);

            lv_obj_t* amount = lv_label_create(tile);
            std::string text;
            if (info_slot.remaining_length_m > 0.0f) {
                text = std::to_string(static_cast<int>(std::lround(info_slot.remaining_length_m))) +
                       " m";
            }
            if (pct >= 0) {
                text += (text.empty() ? "" : " · ") + std::to_string(pct) + "%";
            }
            lv_label_set_text(amount, text.c_str());
            lv_obj_set_style_text_color(
                amount, pct >= 0 && pct <= 15 ? theme_manager_get_color("warning") : text_color,
                LV_PART_MAIN);

            if (!manual_) {
                continue;
            }
            lv_obj_t* moves = plain(tile);
            lv_obj_set_size(moves, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(moves, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(moves, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            for (int delta : {-1, +1}) {
                lv_obj_t* btn = lv_button_create(moves);
                lv_obj_set_size(btn, 52, 36);
                lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
                lv_obj_set_style_bg_color(btn, primary, LV_PART_MAIN);
                lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
                if (auto* icon = static_cast<lv_obj_t*>(lv_xml_create(btn, "icon", nullptr))) {
                    ui_icon_set_source(icon, delta < 0 ? "arrow_left_bold" : "arrow_right_bold");
                    ui_icon_set_size(icon, "sm");
                    ui_icon_set_color(icon, lv_color_white(), LV_OPA_COVER);
                    lv_obj_center(icon);
                }
                lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(
                                              (g << 16) | (i << 8) | (delta > 0 ? 1 : 0))));
                lv_obj_add_event_cb(btn, on_move, LV_EVENT_CLICKED, this);
                const bool usable = delta < 0 ? i > 0 : i + 1 < group.slots.size();
                if (!usable) {
                    lv_obj_add_state(btn, LV_STATE_DISABLED);
                    lv_obj_set_style_bg_color(btn, theme_manager_get_color("card_bg"),
                                              LV_PART_MAIN | LV_STATE_DISABLED);
                }
            }
        }
    }
}

void K2ohRunoutView::move(size_t group, size_t index, int delta) {
    if (group >= groups_.size()) {
        return;
    }
    auto& slots = groups_[group].slots;
    const long target = static_cast<long>(index) + delta;
    if (index >= slots.size() || target < 0 || target >= static_cast<long>(slots.size())) {
        return;
    }
    std::swap(slots[index], slots[static_cast<size_t>(target)]);
    dirty_ = true;
    update_mode_buttons();
    rebuild_groups();
}

void K2ohRunoutView::save_order() {
    std::string order;
    for (const auto& group : groups_) {
        for (int slot : group.slots) {
            order += (order.empty() ? "" : ",") + std::to_string(slot);
        }
    }
    if (order.empty()) {
        return;
    }
    send("_BOX_SET_RUNOUT_ORDER ORDER=" + order);
    dirty_ = false;
    update_mode_buttons();
}

void K2ohRunoutView::send(const std::string& gcode) {
    IMoonrakerAPI* api = get_moonraker_api();
    if (!api) {
        return;
    }
    spdlog::info("[K2oh Runout] {}", gcode);
    api->execute_gcode(
        gcode, []() {},
        [gcode](const MoonrakerError& err) {
            NOTIFY_WARNING(lv_tr("Runout order not saved: {}"), err.message);
            spdlog::warn("[K2oh Runout] {} failed: {}", gcode, err.message);
        });
}

void K2ohRunoutView::on_click(lv_event_t* e) {
    auto* self = static_cast<K2ohRunoutView*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* name = lv_obj_get_name(target);
    if (!name) {
        return;
    }
    if (std::strcmp(name, "runout_back") == 0) {
        if (self->on_back_) {
            self->on_back_();
        }
    } else if (std::strcmp(name, "btn_mode_auto") == 0) {
        self->manual_ = false;
        self->dirty_ = false;
        self->send("_BOX_SET_RUNOUT_ORDER ORDER=AUTO");
        self->update_mode_buttons();
        self->rebuild_groups();
    } else if (std::strcmp(name, "btn_mode_manual") == 0) {
        self->manual_ = true;
        // The current sequence is the starting point; saving it makes it manual.
        self->dirty_ = true;
        self->update_mode_buttons();
        self->rebuild_groups();
    } else if (std::strcmp(name, "btn_save_order") == 0) {
        self->save_order();
    }
}

void K2ohRunoutView::on_move(lv_event_t* e) {
    auto* self = static_cast<K2ohRunoutView*>(lv_event_get_user_data(e));
    auto* btn = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const auto packed =
        static_cast<uintptr_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(btn)));
    const size_t group = (packed >> 16) & 0xFFFF;
    const size_t index = (packed >> 8) & 0xFF;
    const int delta = (packed & 1) ? 1 : -1;
    // Rebuilding deletes this button: defer past the event.
    lv_async_call(
        [](void* data) {
            auto* args = static_cast<std::tuple<K2ohRunoutView*, size_t, size_t, int>*>(data);
            auto [view, g, i, d] = *args;
            delete args;
            if (view->root_) {
                view->move(g, i, d);
            }
        },
        new std::tuple<K2ohRunoutView*, size_t, size_t, int>(self, group, index, delta));
}

void K2ohRunoutView::on_switch(lv_event_t* e) {
    auto* self = static_cast<K2ohRunoutView*>(lv_event_get_user_data(e));
    if (self->syncing_) {
        return;
    }
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const bool enable = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (AmsBackend* backend = AmsState::instance().get_backend()) {
        const AmsError err = backend->execute_device_action("toggle_auto_refill", std::any(enable));
        if (!err.success()) {
            spdlog::warn("[K2oh Runout] runout swap {} refused: {}", enable ? "on" : "off",
                         err.technical_msg);
        }
    }
}

} // namespace helix::ui
