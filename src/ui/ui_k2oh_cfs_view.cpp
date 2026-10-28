// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost CFS page (see ui_k2oh_cfs_view.h).

#include "ui_k2oh_cfs_view.h"

#include "ui_button.h"
#include "ui_icon.h"
#include "ui_nav_manager.h"
#include "ui_utils.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <any>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <string>

namespace helix::ui {

namespace {

constexpr int kRingSize = 84;
constexpr int kRingWidth = 7;
// A spool at or under this share is shown in the warning colour.
constexpr int kLowPercent = 15;
// shortcut: fixed humidity line while Kalico publishes no humidity_limit_pct;
// read the box's limit once it does.
constexpr float kHumidLimitPct = 50.0f;

lv_obj_t* child(lv_obj_t* parent, const char* name) {
    return parent ? lv_obj_find_by_name(parent, name) : nullptr;
}

void set_text(lv_obj_t* parent, const char* name, const std::string& text) {
    if (lv_obj_t* obj = child(parent, name)) {
        lv_label_set_text(obj, text.c_str());
    }
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (!obj) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_enabled(lv_obj_t* obj, bool enabled) {
    if (!obj) {
        return;
    }
    if (enabled) {
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
}

int percent_of(const SlotInfo& slot) {
    const float pct = slot.get_remaining_percent();
    return pct < 0.0f ? -1 : std::clamp(static_cast<int>(std::lround(pct)), 0, 100);
}

bool is_empty(const SlotInfo& slot) {
    return slot.status == SlotStatus::EMPTY || slot.status == SlotStatus::UNKNOWN;
}

/// "PETG: 4 (8%) » 2 (85%)" (the font has no arrow glyph) for each group of spools that back each other up,
/// the loaded one first. Without groups, the generic endless-spool sentence.
std::string runout_text(const AmsSystemInfo& info, int loaded_slot) {
    std::map<int, std::vector<const SlotInfo*>> groups;
    for (const auto& unit : info.units) {
        for (const auto& slot : unit.slots) {
            const auto index = static_cast<size_t>(slot.global_index);
            if (slot.global_index >= 0 && index < info.endless_spool_group_ids.size() &&
                info.endless_spool_group_ids[index] >= 0) {
                groups[info.endless_spool_group_ids[index]].push_back(&slot);
            }
        }
    }
    std::string text;
    for (auto& [id, members] : groups) {
        if (members.size() < 2) {
            continue;
        }
        // shortcut: Kalico's default strategy (lowest_remaining_first) ordering;
        // read the group's own order once a second strategy is in use.
        std::sort(members.begin(), members.end(), [loaded_slot](const SlotInfo* a, const SlotInfo* b) {
            if ((a->global_index == loaded_slot) != (b->global_index == loaded_slot)) {
                return a->global_index == loaded_slot;
            }
            return a->get_remaining_percent() < b->get_remaining_percent();
        });
        std::string line = members.front()->material + ":";
        for (size_t i = 0; i < members.size(); ++i) {
            line += (i ? " » " : " ") + std::to_string(members[i]->global_index + 1);
            if (const int pct = percent_of(*members[i]); pct >= 0) {
                line += " (" + std::to_string(pct) + "%)";
            }
        }
        text += (text.empty() ? "" : "  ·  ") + line;
    }
    if (text.empty()) {
        text = lv_subject_get_string(AmsState::instance().get_endless_text_subject());
    }
    return text;
}

bool print_running(PrinterState& printer_state) {
    return printer_has_job(static_cast<PrintJobState>(
        lv_subject_get_int(printer_state.get_print_state_enum_subject())));
}

} // namespace

K2ohCfsView::K2ohCfsView(PrinterState& printer_state, Actions actions)
    : printer_state_(printer_state), actions_(std::move(actions)) {}

K2ohCfsView::~K2ohCfsView() = default;

bool K2ohCfsView::applies() {
    AmsBackend* backend = AmsState::instance().get_backend();
    // supports_gate_check() is the Fork dialect (Kalico's box module) on a CFS.
    return backend && backend->get_type() == AmsType::CFS && backend->supports_gate_check();
}

void K2ohCfsView::create(lv_obj_t* panel) {
    if (root_ || !panel) {
        return;
    }
    root_ = static_cast<lv_obj_t*>(lv_xml_create(panel, "k2oh_cfs_view", nullptr));
    if (!root_) {
        spdlog::error("[K2oh CFS] k2oh_cfs_view.xml did not build");
        return;
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    cards_row_ = child(root_, "cards_row");

    if (lv_obj_t* back = child(root_, "cfs_back")) {
        lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, this);
    }
    for (const char* name : {"ext_chip", "btn_load_unload", "btn_rfid", "btn_edit", "btn_more"}) {
        if (lv_obj_t* obj = child(root_, name)) {
            lv_obj_add_event_cb(obj, on_button_clicked, LV_EVENT_CLICKED, this);
        }
    }
    if (lv_obj_t* sw = child(root_, "runout_switch")) {
        lv_obj_add_event_cb(sw, on_runout_changed, LV_EVENT_VALUE_CHANGED, this);
    }

    auto& ams = AmsState::instance();
    auto on_change = [](K2ohCfsView* self, int) {
        if (self->visible()) {
            self->refresh();
        }
    };
    slots_observer_ = observe_int_sync<K2ohCfsView>(ams.get_slots_version_subject(), this, on_change,
                                                    ams.get_subjects_lifetime());
    action_observer_ = observe_int_sync<K2ohCfsView>(ams.get_ams_action_subject(), this, on_change,
                                                     ams.get_subjects_lifetime());
    current_slot_observer_ = observe_int_sync<K2ohCfsView>(
        ams.get_current_slot_subject(), this, on_change, ams.get_subjects_lifetime());
    print_state_observer_ = observe_int_sync<K2ohCfsView>(
        printer_state_.get_print_state_enum_subject(), this, on_change);
}

void K2ohCfsView::set_visible(bool visible) {
    set_hidden(root_, !visible);
    if (visible) {
        refresh();
    }
}

bool K2ohCfsView::visible() const {
    return root_ && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void K2ohCfsView::build_cards(int count) {
    if (!cards_row_) {
        return;
    }
    lv_obj_clean(cards_row_);
    cards_.clear();
    const lv_color_t track = theme_manager_get_color("elevated_bg");
    for (int i = 0; i < count; ++i) {
        Card card;
        card.root = static_cast<lv_obj_t*>(lv_xml_create(cards_row_, "k2oh_cfs_card", nullptr));
        if (!card.root) {
            continue;
        }
        lv_obj_add_event_cb(card.root, on_card_clicked, LV_EVENT_CLICKED, this);
        if (lv_obj_t* box = child(card.root, "ring_box")) {
            card.ring = lv_arc_create(box);
            lv_obj_set_size(card.ring, kRingSize, kRingSize);
            lv_obj_center(card.ring);
            lv_obj_move_to_index(card.ring, 0);
            lv_arc_set_rotation(card.ring, 270);
            lv_arc_set_bg_angles(card.ring, 0, 360);
            lv_arc_set_range(card.ring, 0, 100);
            lv_obj_remove_style(card.ring, nullptr, LV_PART_KNOB);
            lv_obj_remove_flag(card.ring, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(card.ring, LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_set_style_arc_width(card.ring, kRingWidth, LV_PART_MAIN);
            lv_obj_set_style_arc_width(card.ring, kRingWidth, LV_PART_INDICATOR);
            lv_obj_set_style_arc_color(card.ring, track, LV_PART_MAIN);
        }
        cards_.push_back(card);
    }
}

void K2ohCfsView::refresh() {
    if (!root_) {
        return;
    }
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }
    const AmsSystemInfo info = backend->get_system_info();

    std::vector<const SlotInfo*> slots;
    for (const auto& unit : info.units) {
        for (const auto& slot : unit.slots) {
            slots.push_back(&slot);
        }
    }
    if (slots.size() != cards_.size()) {
        build_cards(static_cast<int>(slots.size()));
    }

    loaded_slot_ = -1;
    for (const SlotInfo* slot : slots) {
        if (slot->status == SlotStatus::LOADED) {
            loaded_slot_ = slot->global_index;
        }
    }
    if (loaded_slot_ < 0 && info.filament_loaded) {
        loaded_slot_ = info.current_slot;
    }
    const bool printing = print_running(printer_state_);

    for (size_t i = 0; i < cards_.size() && i < slots.size(); ++i) {
        update_card(cards_[i], *slots[i], slots[i]->global_index == loaded_slot_, printing);
    }

    if (selected_ < 0 || selected_ >= static_cast<int>(slots.size())) {
        selected_ = loaded_slot_ >= 0 ? loaded_slot_ : 0;
    }
    for (auto& card : cards_) {
        lv_obj_set_style_border_color(
            card.root, theme_manager_get_color(card.slot == selected_ ? "primary" : "card_bg"),
            LV_PART_MAIN);
    }

    update_header(info);
    update_actions();
}

void K2ohCfsView::update_card(Card& card, const SlotInfo& slot, bool loaded, bool printing) {
    card.slot = slot.global_index;
    lv_obj_t* root = card.root;
    const bool empty = is_empty(slot);
    const bool unknown = !empty && slot.material.empty();
    const int pct = percent_of(slot);
    const bool low = pct >= 0 && pct <= kLowPercent;
    const lv_color_t filament = lv_color_hex(slot.color_rgb);

    set_text(root, "num_label", std::to_string(slot.global_index + 1));

    if (lv_obj_t* core = child(root, "core")) {
        lv_obj_set_style_bg_color(
            core, empty || unknown ? theme_manager_get_color("elevated_bg") : filament, LV_PART_MAIN);
    }
    set_hidden(child(root, "hole"), empty || unknown);
    set_hidden(child(root, "unknown_icon"), !unknown);
    if (card.ring) {
        lv_arc_set_value(card.ring, std::max(pct, 0));
        lv_obj_set_style_arc_color(card.ring, low ? theme_manager_get_color("warning") : filament,
                                   LV_PART_INDICATOR);
    }

    set_hidden(child(root, "in_use_tag"), !loaded);
    set_text(root, "in_use_label", lv_tr(printing ? "IN USE" : "LOADED"));

    if (empty) {
        set_text(root, "material_label", lv_tr("Empty"));
        set_text(root, "brand_label", "");
    } else if (unknown) {
        set_text(root, "material_label", lv_tr("Not recognised"));
        set_text(root, "brand_label", lv_tr("unknown tag"));
    } else {
        set_text(root, "material_label", slot.material);
        set_text(root, "brand_label", slot.brand);
    }

    std::string amount;
    if (slot.remaining_length_m > 0.0f) {
        amount = std::to_string(static_cast<int>(std::lround(slot.remaining_length_m))) + " m";
    }
    if (pct >= 0) {
        amount += (amount.empty() ? "" : " · ") + std::to_string(pct) + "%";
    }
    set_text(root, "amount_label", empty ? "" : amount);
    if (lv_obj_t* label = child(root, "amount_label")) {
        lv_obj_set_style_text_color(label, theme_manager_get_color(low ? "warning" : "text"),
                                    LV_PART_MAIN);
    }

    set_hidden(child(root, "source_chip"), empty || pct < 0);
    set_text(root, "source_label", slot.remaining_estimated ? lv_tr("estimate") : "RFID");
}

void K2ohCfsView::update_header(const AmsSystemInfo& info) {
    const bool error = info.action == AmsAction::ERROR;
    const bool busy = info.action != AmsAction::IDLE && !error;
    std::string state = lv_tr("CFS ready");
    if (error || busy) {
        state = info.operation_detail.empty() ? lv_tr(ams_action_to_string(info.action))
                                              : info.operation_detail;
    }
    set_text(root_, "state_label", state);
    if (lv_obj_t* icon = child(root_, "state_icon")) {
        ui_icon_set_source(icon, error ? "alert_circle" : busy ? "sync" : "check_circle");
        ui_icon_set_variant(icon, error ? "danger" : busy ? "warning" : "success");
    }

    std::optional<EnvironmentData> env;
    for (const auto& unit : info.units) {
        if (unit.environment) {
            env = unit.environment;
            break;
        }
    }
    set_hidden(child(root_, "env_chip"), !env);
    if (env) {
        set_text(root_, "temp_label",
                 std::to_string(static_cast<int>(std::lround(env->temperature_c))) + "°C");
        const bool humid = env->humidity_pct > kHumidLimitPct;
        set_text(root_, "humidity_label",
                 env->has_humidity
                     ? std::to_string(static_cast<int>(std::lround(env->humidity_pct))) + "% · " +
                           lv_tr(humid ? "humid" : "dry")
                     : "");
        if (lv_obj_t* icon = child(root_, "humidity_icon")) {
            ui_icon_set_variant(icon, humid ? "warning" : "success");
            set_hidden(icon, !env->has_humidity);
        }
    }

    const auto ext = AmsState::instance().get_external_spool_info();
    set_hidden(child(root_, "ext_chip"), !ext);
    if (ext) {
        set_text(root_, "ext_label",
                 std::string(lv_tr("External")) + ": " +
                     (ext->material.empty() ? std::string("?") : ext->material));
    }

    set_text(root_, "runout_detail", runout_text(info, loaded_slot_));

    if (lv_obj_t* sw = child(root_, "runout_switch")) {
        syncing_switch_ = true;
        if (info.endless_spool_enabled) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        }
        syncing_switch_ = false;
    }
}

void K2ohCfsView::update_actions() {
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend || selected_ < 0) {
        return;
    }
    const SlotInfo slot = backend->get_slot_info(selected_);
    const bool empty = is_empty(slot);
    const bool printing = print_running(printer_state_);
    const bool busy = ams_action_holds_system(static_cast<AmsAction>(
        lv_subject_get_int(AmsState::instance().get_ams_action_subject())));
    const std::string num = " " + std::to_string(selected_ + 1);

    if (lv_obj_t* btn = child(root_, "btn_load_unload")) {
        const bool unload = selected_ == loaded_slot_;
        ui_button_set_text(btn, (std::string(lv_tr(unload ? "Unload" : "Load")) + num).c_str());
        ui_button_set_icon(btn, unload ? "tray_arrow_up" : "arrow_down_bold");
        set_enabled(btn, !printing && !busy && (unload || !empty));
    }
    set_enabled(child(root_, "btn_rfid"), !busy && !empty);
    set_enabled(child(root_, "btn_edit"), !empty);
}

void K2ohCfsView::select(int slot) {
    selected_ = slot;
    refresh();
}

void K2ohCfsView::on_card_clicked(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsView*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    for (const auto& card : self->cards_) {
        if (card.root == target) {
            self->select(card.slot);
            return;
        }
    }
}

void K2ohCfsView::on_button_clicked(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsView*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* name = lv_obj_get_name(target);
    if (!name) {
        return;
    }
    if (std::strcmp(name, "ext_chip") == 0) {
        if (self->actions_.edit_external) {
            self->actions_.edit_external();
        }
        return;
    }
    const int slot = self->selected_;
    if (slot < 0) {
        return;
    }
    if (std::strcmp(name, "btn_load_unload") == 0) {
        auto& action = slot == self->loaded_slot_ ? self->actions_.unload : self->actions_.load;
        if (action) {
            action(slot);
        }
    } else if (std::strcmp(name, "btn_rfid") == 0) {
        if (AmsBackend* backend = AmsState::instance().get_backend()) {
            const AmsError err = backend->check_gate(slot);
            if (!err.success()) {
                spdlog::warn("[K2oh CFS] RFID reread of slot {} refused: {}", slot + 1,
                             err.technical_msg);
            }
        }
    } else if (std::strcmp(name, "btn_edit") == 0) {
        if (self->actions_.edit) {
            self->actions_.edit(slot);
        }
    } else if (std::strcmp(name, "btn_more") == 0) {
        if (self->actions_.more) {
            self->actions_.more(slot, target);
        }
    }
}

void K2ohCfsView::on_runout_changed(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsView*>(lv_event_get_user_data(e));
    if (self->syncing_switch_) {
        return;
    }
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const bool enable = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (AmsBackend* backend = AmsState::instance().get_backend()) {
        const AmsError err = backend->execute_device_action("toggle_auto_refill", std::any(enable));
        if (!err.success()) {
            spdlog::warn("[K2oh CFS] runout swap {} refused: {}", enable ? "on" : "off",
                         err.technical_msg);
        }
    }
}

void K2ohCfsView::on_back_clicked(lv_event_t* /*e*/) {
    NavigationManager::instance().go_back();
}

} // namespace helix::ui
