// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost CFS settings page (see ui_k2oh_cfs_settings.h).

#include "ui_k2oh_cfs_settings.h"

#include "ui_error_reporting.h"
#include "ui_icon.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <any>
#include <cstring>

namespace helix::ui {

namespace {

// The runout swap has its own page; it is linked, not repeated as a switch.
constexpr const char* kRunoutAction = "toggle_auto_refill";

lv_obj_t* plain(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

void* index_data(size_t i) {
    return reinterpret_cast<void*>(static_cast<intptr_t>(i));
}

size_t data_index(lv_obj_t* obj) {
    return static_cast<size_t>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
}

} // namespace

K2ohCfsSettingsView::K2ohCfsSettingsView(Actions actions) : actions_(std::move(actions)) {}

K2ohCfsSettingsView::~K2ohCfsSettingsView() {
    if (root_) {
        lv_obj_remove_event_cb_with_user_data(root_, on_root_deleted, this);
    }
}

void K2ohCfsSettingsView::on_root_deleted(lv_event_t* e) {
    static_cast<K2ohCfsSettingsView*>(lv_event_get_user_data(e))->root_ = nullptr;
}

void K2ohCfsSettingsView::create(lv_obj_t* panel) {
    if (root_ || !panel) {
        return;
    }
    root_ = static_cast<lv_obj_t*>(lv_xml_create(panel, "k2oh_cfs_settings", nullptr));
    if (!root_) {
        spdlog::error("[K2oh CFS settings] k2oh_cfs_settings.xml did not build");
        return;
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, on_root_deleted, LV_EVENT_DELETE, this);
    if (lv_obj_t* back = lv_obj_find_by_name(root_, "settings_back")) {
        lv_obj_add_event_cb(back, on_click, LV_EVENT_CLICKED, this);
    }
}

void K2ohCfsSettingsView::show() {
    if (!root_) {
        return;
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    refresh();
}

void K2ohCfsSettingsView::hide() {
    if (root_) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool K2ohCfsSettingsView::visible() const {
    return root_ && !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* K2ohCfsSettingsView::add_row(lv_obj_t* list, const std::string& title,
                                       const std::string& detail) {
    lv_obj_t* row = plain(list);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(row, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, theme_manager_get_color("card_bg"), LV_PART_MAIN);

    lv_obj_t* text = plain(row);
    lv_obj_set_height(text, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text, 1);
    lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
    lv_obj_t* t = lv_label_create(text);
    lv_label_set_text(t, title.c_str());
    lv_obj_set_style_text_color(t, theme_manager_get_color("text"), LV_PART_MAIN);
    if (!detail.empty()) {
        lv_obj_t* d = lv_label_create(text);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(d, LV_PCT(100));
        lv_label_set_text(d, detail.c_str());
        lv_obj_set_style_text_color(d, theme_manager_get_color("text_muted"), LV_PART_MAIN);
        if (const lv_font_t* small = theme_manager_get_font("font_small")) {
            lv_obj_set_style_text_font(d, small, LV_PART_MAIN);
        }
    }
    return row;
}

void K2ohCfsSettingsView::refresh() {
    if (!visible()) {
        return;
    }
    lv_obj_t* list = lv_obj_find_by_name(root_, "settings_list");
    if (!list) {
        return;
    }
    lv_obj_clean(list);
    ids_.clear();

    // Link rows first: the runout swap page and the stock device operations.
    struct Link {
        const char* title;
        const char* detail;
        const char* id;
    };
    for (const Link& link :
         {Link{"Auto-swap at spool end", "Switch, automatic or manual sequence", "link:runout"},
          Link{"Device operations", "Home, recover, abort, bypass mode", "link:device"}}) {
        lv_obj_t* row = add_row(list, lv_tr(link.title), lv_tr(link.detail));
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        ids_.push_back(link.id);
        lv_obj_set_user_data(row, index_data(ids_.size() - 1));
        lv_obj_add_event_cb(row, on_link, LV_EVENT_CLICKED, this);
        if (auto* icon = static_cast<lv_obj_t*>(lv_xml_create(row, "icon", nullptr))) {
            ui_icon_set_source(icon, "chevron_right");
            ui_icon_set_size(icon, "sm");
        }
    }

    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend) {
        return;
    }
    for (const auto& action : backend->get_device_actions()) {
        if (action.id == kRunoutAction) {
            continue;
        }
        lv_obj_t* row = add_row(list, lv_tr(action.label.c_str()),
                                lv_tr(action.description.c_str()));
        ids_.push_back(action.id);
        const size_t index = ids_.size() - 1;
        if (action.type == printer::ActionType::TOGGLE) {
            lv_obj_t* sw = lv_switch_create(row);
            lv_obj_set_user_data(sw, index_data(index));
            if (const bool* on = std::any_cast<bool>(&action.current_value); on && *on) {
                lv_obj_add_state(sw, LV_STATE_CHECKED);
            }
            lv_obj_add_event_cb(sw, on_toggle, LV_EVENT_VALUE_CHANGED, this);
            if (!action.enabled) {
                lv_obj_add_state(sw, LV_STATE_DISABLED);
            }
        } else if (action.type == printer::ActionType::BUTTON) {
            lv_obj_t* btn = lv_button_create(row);
            lv_obj_set_size(btn, LV_SIZE_CONTENT, 40);
            lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
            lv_obj_set_style_bg_color(btn, theme_manager_get_color("primary"), LV_PART_MAIN);
            lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text(label, lv_tr("Run"));
            lv_obj_center(label);
            lv_obj_set_user_data(btn, index_data(index));
            lv_obj_add_event_cb(btn, on_run, LV_EVENT_CLICKED, this);
            if (!action.enabled) {
                lv_obj_add_state(btn, LV_STATE_DISABLED);
            }
        }
        if (!action.enabled && !action.disable_reason.empty()) {
            lv_obj_t* why = lv_label_create(row);
            lv_label_set_text(why, action.disable_reason.c_str());
            lv_obj_set_style_text_color(why, theme_manager_get_color("warning"), LV_PART_MAIN);
        }
    }
}

void K2ohCfsSettingsView::on_click(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsSettingsView*>(lv_event_get_user_data(e));
    if (self->actions_.back) {
        self->actions_.back();
    }
}

void K2ohCfsSettingsView::on_link(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsSettingsView*>(lv_event_get_user_data(e));
    const size_t index = data_index(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
    if (index >= self->ids_.size()) {
        return;
    }
    const std::string& id = self->ids_[index];
    if (id == "link:runout" && self->actions_.open_runout) {
        self->actions_.open_runout();
    } else if (id == "link:device" && self->actions_.open_device_operations) {
        self->actions_.open_device_operations();
    }
}

void K2ohCfsSettingsView::on_toggle(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsSettingsView*>(lv_event_get_user_data(e));
    auto* sw = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const size_t index = data_index(sw);
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend || index >= self->ids_.size()) {
        return;
    }
    const bool enable = lv_obj_has_state(sw, LV_STATE_CHECKED);
    const AmsError err = backend->execute_device_action(self->ids_[index], std::any(enable));
    if (!err.success()) {
        notify_ams_error(err);
        // Put the switch back to what the box still says.
        if (enable) {
            lv_obj_remove_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
    }
}

void K2ohCfsSettingsView::on_run(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsSettingsView*>(lv_event_get_user_data(e));
    const size_t index = data_index(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
    AmsBackend* backend = AmsState::instance().get_backend();
    if (!backend || index >= self->ids_.size()) {
        return;
    }
    const AmsError err = backend->execute_device_action(self->ids_[index], std::any());
    if (!err.success()) {
        notify_ams_error(err);
    } else {
        NOTIFY_INFO(lv_tr("Sent"));
    }
}

} // namespace helix::ui
