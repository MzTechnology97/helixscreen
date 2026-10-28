// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost print status (see ui_k2oh_print_view.h).

#include "ui_k2oh_print_view.h"

#include "ui_button.h"
#include "ui_format_utils.h"
#include "ui_k2oh_cfs_view.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "printer_state.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>

namespace helix::ui {

namespace {

constexpr int kArcSize = 220;
constexpr int kArcWidth = 12;
// Elapsed time and the end clock move every second without a subject per tick.
constexpr uint32_t kTickMs = 5000;

lv_obj_t* child(lv_obj_t* parent, const char* name) {
    return parent ? lv_obj_find_by_name(parent, name) : nullptr;
}

void set_text(lv_obj_t* parent, const char* name, const std::string& text) {
    if (lv_obj_t* obj = child(parent, name)) {
        lv_label_set_text(obj, text.c_str());
    }
}

std::string degrees(int deci) {
    return std::to_string(static_cast<int>(std::lround(deci / 10.0))) + "°";
}

} // namespace

K2ohPrintView::K2ohPrintView(PrinterState& printer_state, Actions actions)
    : printer_state_(printer_state), actions_(std::move(actions)) {}

K2ohPrintView::~K2ohPrintView() {
    if (timer_) {
        lv_timer_delete(timer_);
    }
    if (root_) {
        lv_obj_remove_event_cb_with_user_data(root_, on_root_deleted, this);
    }
}

void K2ohPrintView::on_root_deleted(lv_event_t* e) {
    auto* self = static_cast<K2ohPrintView*>(lv_event_get_user_data(e));
    self->observers_.clear();
    if (self->timer_) {
        lv_timer_delete(self->timer_);
        self->timer_ = nullptr;
    }
    self->root_ = nullptr;
    self->arc_ = nullptr;
    self->content_ = nullptr;
}

void K2ohPrintView::create(lv_obj_t* content) {
    if (root_ || !content) {
        return;
    }
    content_ = content;
    root_ = static_cast<lv_obj_t*>(lv_xml_create(content, "k2oh_print_view", nullptr));
    if (!root_) {
        spdlog::error("[K2oh Print] k2oh_print_view.xml did not build");
        return;
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, on_root_deleted, LV_EVENT_DELETE, this);

    if (lv_obj_t* box = child(root_, "ring_box")) {
        arc_ = lv_arc_create(box);
        lv_obj_set_size(arc_, kArcSize, kArcSize);
        lv_obj_center(arc_);
        lv_obj_move_to_index(arc_, 0);
        lv_arc_set_rotation(arc_, 270);
        lv_arc_set_bg_angles(arc_, 0, 360);
        lv_arc_set_range(arc_, 0, 100);
        lv_obj_remove_style(arc_, nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(arc_, kArcWidth, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc_, kArcWidth, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc_, theme_manager_get_color("elevated_bg"), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc_, theme_manager_get_color("primary"), LV_PART_INDICATOR);
    }
    for (const char* name : {"btn_tune", "slot_card", "factors_row"}) {
        if (lv_obj_t* obj = child(root_, name)) {
            lv_obj_add_event_cb(obj, on_click, LV_EVENT_CLICKED, this);
        }
    }

    auto& ps = printer_state_;
    auto& ams = AmsState::instance();
    auto on_change = [](K2ohPrintView* self, int) { self->refresh(); };
    for (lv_subject_t* s :
         {ps.get_print_progress_subject(), ps.get_print_layer_current_subject(),
          ps.get_print_time_left_subject(), ps.get_active_extruder_temp_subject(),
          ps.get_active_extruder_target_subject(), ps.get_bed_temp_subject(),
          ps.get_bed_target_subject(), ps.get_chamber_temp_subject(), ps.get_speed_factor_subject(),
          ps.get_flow_factor_subject(), ps.get_fan_speed_subject(), ps.get_print_start_phase_subject()}) {
        observers_.push_back(observe_int_sync<K2ohPrintView>(s, this, on_change));
    }
    for (lv_subject_t* s : {ams.get_slots_version_subject(), ams.get_current_slot_subject()}) {
        observers_.push_back(
            observe_int_sync<K2ohPrintView>(s, this, on_change, ams.get_subjects_lifetime()));
    }
    timer_ = lv_timer_create(on_timer, kTickMs, this);
    update_visibility();
}

void K2ohPrintView::update_visibility() {
    if (!root_ || !content_) {
        return;
    }
    const bool k2oh = K2ohCfsView::applies();
    // The stock content stays built (the panel keeps driving it); it is only covered.
    const uint32_t count = lv_obj_get_child_count(content_);
    for (uint32_t i = 0; i < count; ++i) {
        lv_obj_t* obj = lv_obj_get_child(content_, static_cast<int32_t>(i));
        if (obj == root_) {
            continue;
        }
        if (k2oh) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (k2oh) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        refresh();
    } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void K2ohPrintView::refresh() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    auto& ps = printer_state_;
    const int progress = std::clamp(lv_subject_get_int(ps.get_print_progress_subject()), 0, 100);
    if (arc_) {
        lv_arc_set_value(arc_, progress);
    }
    set_text(root_, "percent", std::to_string(progress) + "%");
    const int layer = lv_subject_get_int(ps.get_print_layer_current_subject());
    const int layers = lv_subject_get_int(ps.get_print_layer_total_subject());
    set_text(root_, "layer",
             layers > 0 ? std::string(lv_tr("layer")) + " " + std::to_string(layer) + " / " +
                              std::to_string(layers)
                        : "");
    if (lv_obj_t* img = child(root_, "thumb")) {
        const char* path = lv_subject_get_string(ps.get_print_thumbnail_path_subject());
        if (path && *path) {
            lv_image_set_src(img, path);
        }
    }

    // The start macro's own progress line, between "not started" (0) and "done" (10).
    if (lv_obj_t* msg = child(root_, "start_message")) {
        const int phase = lv_subject_get_int(ps.get_print_start_phase_subject());
        if (phase > 0 && phase < 10) {
            lv_obj_remove_flag(msg, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(msg, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const int elapsed_s = lv_subject_get_int(ps.get_print_elapsed_subject());
    const int left_s = lv_subject_get_int(ps.get_print_time_left_subject());
    set_text(root_, "elapsed", format_print_time(elapsed_s / 60));
    set_text(root_, "left", left_s > 0 ? format_print_time(left_s / 60) : "—");
    std::string ends = "—";
    if (left_s > 0) {
        const std::time_t end = std::time(nullptr) + left_s;
        std::tm end_tm{};
        localtime_r(&end, &end_tm);
        ends = format_time(&end_tm);
    }
    set_text(root_, "ends", ends);

    const int nozzle_t = lv_subject_get_int(ps.get_active_extruder_target_subject());
    const int bed_t = lv_subject_get_int(ps.get_bed_target_subject());
    set_text(root_, "nozzle",
             degrees(lv_subject_get_int(ps.get_active_extruder_temp_subject())) +
                 (nozzle_t > 0 ? " / " + degrees(nozzle_t) : ""));
    set_text(root_, "bed",
             degrees(lv_subject_get_int(ps.get_bed_temp_subject())) +
                 (bed_t > 0 ? " / " + degrees(bed_t) : ""));
    set_text(root_, "chamber", degrees(lv_subject_get_int(ps.get_chamber_temp_subject())));
    set_text(root_, "speed", std::to_string(lv_subject_get_int(ps.get_speed_factor_subject())) + "%");
    set_text(root_, "flow", std::to_string(lv_subject_get_int(ps.get_flow_factor_subject())) + "%");
    set_text(root_, "fan", std::to_string(lv_subject_get_int(ps.get_fan_speed_subject())) + "%");

    // The shared label subject holds the English verb; translate it here.
    if (lv_subject_t* verb = lv_xml_get_subject(nullptr, "print_control_primary_label")) {
        if (lv_obj_t* btn = child(root_, "btn_pause")) {
            ui_button_set_text(btn, lv_tr(lv_subject_get_string(verb)));
        }
    }

    // The spool feeding the print, and where the runout swap goes next.
    lv_obj_t* slot_card = child(root_, "slot_card");
    AmsBackend* backend = AmsState::instance().get_backend();
    const AmsSystemInfo info = backend ? backend->get_system_info() : AmsSystemInfo{};
    const SlotInfo* loaded = nullptr;
    for (const auto& unit : info.units) {
        for (const auto& slot : unit.slots) {
            if (slot.status == SlotStatus::LOADED) {
                loaded = &slot;
            }
        }
    }
    if (slot_card) {
        if (loaded) {
            lv_obj_remove_flag(slot_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(slot_card, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (!loaded) {
        return;
    }
    if (lv_obj_t* swatch = child(root_, "slot_swatch")) {
        lv_obj_set_style_bg_color(swatch, lv_color_hex(loaded->color_rgb), LV_PART_MAIN);
    }
    std::string title = std::string(lv_tr("Slot")) + " " + std::to_string(loaded->global_index + 1) +
                        " · " + loaded->material;
    if (!loaded->brand.empty()) {
        title += " " + loaded->brand;
    }
    set_text(root_, "slot_title", title);
    std::string detail;
    if (loaded->remaining_length_m > 0.0f) {
        detail = std::to_string(static_cast<int>(std::lround(loaded->remaining_length_m))) + " m";
    }
    if (const float pct = loaded->get_remaining_percent(); pct >= 0.0f) {
        detail += (detail.empty() ? "" : " · ") + std::to_string(static_cast<int>(std::lround(pct))) + "%";
    }
    const std::string next = K2ohCfsView::runout_summary(info, loaded->global_index);
    if (!next.empty()) {
        detail += (detail.empty() ? "" : "  ·  ") + next;
    }
    set_text(root_, "slot_detail", detail);
}

void K2ohPrintView::on_timer(lv_timer_t* timer) {
    static_cast<K2ohPrintView*>(lv_timer_get_user_data(timer))->refresh();
}

void K2ohPrintView::on_click(lv_event_t* e) {
    auto* self = static_cast<K2ohPrintView*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* name = lv_obj_get_name(target);
    if (!name) {
        return;
    }
    if ((std::strcmp(name, "btn_tune") == 0 || std::strcmp(name, "factors_row") == 0) &&
        self->actions_.tune) {
        self->actions_.tune();
    } else if (std::strcmp(name, "slot_card") == 0 && self->actions_.open_cfs) {
        self->actions_.open_cfs();
    }
}

} // namespace helix::ui
