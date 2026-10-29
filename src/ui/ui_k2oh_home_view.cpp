// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost Home status line and layout (see ui_k2oh_home_view.h).

#include "ui_k2oh_home_view.h"

#include "ui_format_utils.h"
#include "ui_icon.h"
#include "theme_manager.h"
#include "ui_k2oh_cfs_view.h"

#include "ams_backend.h"
#if HELIX_HAS_CFS
#include "ams_backend_cfs.h"
#endif
#include "ams_state.h"
#include "app_globals.h"
#include "i_moonraker_api.h"
#include "config.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "panel_widget_config.h"
#include "panel_widget_manager.h"
#include "printer_state.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <string>

namespace helix::ui {

namespace {

constexpr int kStatusHeight = 40;
constexpr uint32_t kTickMs = 10000;

/// Kalico's external spool entry (box.slots[] with external: true).
struct ExternalProfile {
    bool valid = false;
    int index = -1;
    std::string material;
    std::string brand;
    std::string name;
    uint32_t color = 0;
    int temp = 0;
};

std::string text_field(const nlohmann::json& obj, const char* key) {
    const auto it = obj.find(key);
    return it != obj.end() && it->is_string() ? it->get<std::string>() : std::string();
}

ExternalProfile kalico_external() {
    ExternalProfile p;
#if HELIX_HAS_CFS
    auto* cfs = dynamic_cast<printer::AmsBackendCfs*>(AmsState::instance().get_backend());
    const nlohmann::json slots = cfs ? cfs->box_field("slots") : nlohmann::json();
    if (!slots.is_array()) {
        return p;
    }
    for (const auto& slot : slots) {
        if (!slot.is_object() || !slot.value("external", false)) {
            continue;
        }
        p.valid = true;
        p.index = slot.value("index", -1);
        p.material = text_field(slot, "material");
        p.brand = text_field(slot, "brand");
        p.name = text_field(slot, "name");
        std::string hex = text_field(slot, "color");
        if (!hex.empty() && hex[0] == '#') {
            hex.erase(0, 1);
        }
        p.color = hex.size() == 6 ? static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16)) : 0;
        if (const auto it = slot.find("target_temp"); it != slot.end() && it->is_number()) {
            p.temp = it->get<int>();
        }
        break;
    }
#endif
    return p;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

/// Same spool as far as Kalico keeps it (material is stored uppercase there).
bool same_profile(const ExternalProfile& k, const SlotInfo& s) {
    return upper(k.material) == upper(s.material) && k.brand == s.brand && k.name == s.spool_name &&
           (k.color & 0xFFFFFFu) == (s.color_rgb & 0xFFFFFFu);
}
constexpr const char* kSeededFlag = "/k2oh/home_layout_seeded";

struct Placement {
    const char* id;
    int col, row, colspan, rowspan;
};
// The K2-OpenHost Home on the 6x4 grid: job and temperatures on top, CFS,
// light and fans below.
constexpr Placement kLayout[] = {
    {"k2oh_job", 0, 0, 4, 2},   {"k2oh_temps", 4, 0, 2, 2}, {"k2oh_cfs", 0, 2, 3, 2},
    {"k2oh_light", 3, 2, 1, 2}, {"k2oh_fans", 4, 2, 2, 2},
};

lv_obj_t* child(lv_obj_t* parent, const char* name) {
    return parent ? lv_obj_find_by_name(parent, name) : nullptr;
}

} // namespace

K2ohHomeView::K2ohHomeView(PrinterState& printer_state) : printer_state_(printer_state) {}

K2ohHomeView::~K2ohHomeView() {
    if (timer_) {
        lv_timer_delete(timer_);
    }
    if (root_) {
        lv_obj_remove_event_cb_with_user_data(root_, on_root_deleted, this);
    }
}

void K2ohHomeView::on_root_deleted(lv_event_t* e) {
    auto* self = static_cast<K2ohHomeView*>(lv_event_get_user_data(e));
    self->observers_.clear();
    if (self->timer_) {
        lv_timer_delete(self->timer_);
        self->timer_ = nullptr;
    }
    self->root_ = nullptr;
    self->panel_ = nullptr;
}

void K2ohHomeView::create(lv_obj_t* panel) {
    if (root_ || !panel) {
        return;
    }
    panel_ = panel;
    root_ = static_cast<lv_obj_t*>(lv_xml_create(panel, "k2oh_home_view", nullptr));
    if (!root_) {
        spdlog::error("[K2oh Home] k2oh_home_view.xml did not build");
        return;
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_FLOATING);
    // Only its icons take touches: the line itself must not swallow the e-stop's.
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, on_root_deleted, LV_EVENT_DELETE, this);

    auto& ps = printer_state_;
    auto& ams = AmsState::instance();
    auto state = [](K2ohHomeView* self, int) { self->refresh_state(); };
    for (lv_subject_t* s : {ps.get_print_state_enum_subject(), ps.get_print_progress_subject()}) {
        observers_.push_back(observe_int_sync<K2ohHomeView>(s, this, state));
    }
    observers_.push_back(observe_int_sync<K2ohHomeView>(
        ams.get_slots_version_subject(), this,
        [](K2ohHomeView* self, int) { self->sync_external_from_kalico(); },
        ams.get_subjects_lifetime()));
    observers_.push_back(observe_int_sync<K2ohHomeView>(
        ams.get_external_spool_color_subject(), this,
        [](K2ohHomeView* self, int) { self->push_external_to_kalico(); },
        ams.get_subjects_lifetime()));
    auto k2oh = [](K2ohHomeView* self, int) { self->update_visibility(); };
    for (lv_subject_t* s : {ams.get_ams_type_subject(), ams.get_slots_version_subject()}) {
        observers_.push_back(
            observe_int_sync<K2ohHomeView>(s, this, k2oh, ams.get_subjects_lifetime()));
    }
    timer_ = lv_timer_create(on_timer, kTickMs, this);
    update_visibility();
}

void K2ohHomeView::update_visibility() {
    if (!root_ || !panel_) {
        return;
    }
    const bool k2oh = K2ohCfsView::applies();
    const bool was_hidden = lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    if (k2oh == !was_hidden) {
        return;
    }
    if (!k2oh) {
        // shortcut: the K2-OpenHost Home is not taken back if the box stops being
        // Kalico's mid-session (it would need the e-stop and padding restored).
        return;
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
    // The grid sits below the status line.
    lv_obj_set_style_pad_top(panel_, kStatusHeight, LV_PART_MAIN);
    lv_obj_set_pos(root_, 0, -kStatusHeight);
    // The floating e-stop moves into the status line instead of over the cards.
    if (lv_obj_t* estop = child(panel_, "estop_fab")) {
        // Always at hand on the K2-OpenHost Home (stock shows it only while a job
        // holds the machine); the press still goes through the E-Stop confirmation.
        if (lv_subject_t* visible = lv_xml_get_subject(nullptr, "estop_visible")) {
            lv_obj_remove_from_subject(estop, visible);
        }
        lv_obj_remove_flag(estop, LV_OBJ_FLAG_HIDDEN);
        // Above the status line, which is created later and would take the press.
        lv_obj_move_foreground(estop);
        lv_obj_set_size(estop, 34, 34);
        lv_obj_set_ext_click_area(estop, 10); // a small button, an easy target
        lv_obj_set_style_translate_x(estop, -10, LV_PART_MAIN);
        lv_obj_set_style_translate_y(estop, 3 - kStatusHeight, LV_PART_MAIN);
    }
    seed_layout_once();
    refresh_state();
    refresh_clock();
}

void K2ohHomeView::seed_layout_once() {
    Config* config = Config::get_instance();
    if (!config || config->get<bool>(kSeededFlag, false)) {
        return;
    }
    auto& widgets = PanelWidgetManager::instance().get_widget_config("home");
    if (widgets.pages().empty()) {
        return;
    }
    auto& entries = widgets.page_entries_mut(0);
    for (auto& entry : entries) {
        entry.disable_and_unplace();
    }
    for (const Placement& p : kLayout) {
        auto it = std::find_if(entries.begin(), entries.end(),
                               [&p](const PanelWidgetEntry& e) { return e.id == p.id; });
        if (it == entries.end()) {
            entries.push_back(PanelWidgetEntry{p.id, true, nlohmann::json::object()});
            it = entries.end() - 1;
        }
        it->enabled = true;
        it->col = p.col;
        it->row = p.row;
        it->colspan = p.colspan;
        it->rowspan = p.rowspan;
    }
    widgets.save();
    config->set<bool>(kSeededFlag, true);
    config->save();
    spdlog::info("[K2oh Home] K2-OpenHost Home layout applied (still editable)");
    PanelWidgetManager::instance().notify_config_changed("home");
}

void K2ohHomeView::refresh_state() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const auto state =
        static_cast<PrintJobState>(lv_subject_get_int(printer_state_.get_print_state_enum_subject()));
    const int progress =
        std::clamp(lv_subject_get_int(printer_state_.get_print_progress_subject()), 0, 100);
    std::string chip = lv_tr("Ready");
    const char* icon = "check_circle";
    const char* variant = "success";
    if (state == PrintJobState::PRINTING) {
        chip = std::string(lv_tr("Printing")) + " · " + std::to_string(progress) + "%";
        icon = "printer_3d";
        variant = "primary";
    } else if (state == PrintJobState::PAUSED) {
        chip = std::string(lv_tr("Paused")) + " · " + std::to_string(progress) + "%";
        icon = "pause";
        variant = "warning";
    } else if (state == PrintJobState::ERROR) {
        chip = lv_tr("Error");
        icon = "alert_circle";
        variant = "danger";
    }
    if (lv_obj_t* label = child(root_, "state_label")) {
        lv_label_set_text(label, chip.c_str());
    }
    if (lv_obj_t* obj = child(root_, "state_icon")) {
        ui_icon_set_source(obj, icon);
        ui_icon_set_variant(obj, variant);
    }
    // The chip is tinted with the state colour (the XML sets the tint's opacity).
    if (lv_obj_t* obj = child(root_, "state_chip")) {
        lv_obj_set_style_bg_color(obj, theme_manager_get_color(variant), LV_PART_MAIN);
    }
}

void K2ohHomeView::refresh_clock() {
    if (!root_) {
        return;
    }
    const std::time_t now = std::time(nullptr);
    std::tm now_tm{};
    localtime_r(&now, &now_tm);
    if (lv_obj_t* label = child(root_, "clock_label")) {
        lv_label_set_text(label, format_time(&now_tm).c_str());
    }
}

void K2ohHomeView::on_timer(lv_timer_t* timer) {
    auto* self = static_cast<K2ohHomeView*>(lv_timer_get_user_data(timer));
    if (self->root_ && !lv_obj_has_flag(self->root_, LV_OBJ_FLAG_HIDDEN)) {
        self->refresh_clock();
    }
}

void K2ohHomeView::sync_external_from_kalico() {
    if (!K2ohCfsView::applies()) {
        return;
    }
    const ExternalProfile k = kalico_external();
    if (!k.valid) {
        return;
    }
    auto& ams = AmsState::instance();
    SlotInfo next = ams.get_external_spool_info().value_or(SlotInfo{});
    if (same_profile(k, next)) {
        return;
    }
    next.material = k.material;
    next.brand = k.brand;
    next.spool_name = k.name;
    next.color_rgb = k.color;
    next.nozzle_temp_min = k.temp;
    next.nozzle_temp_max = k.temp;
    next.slot_index = -2;
    next.global_index = -2;
    spdlog::info("[K2oh Home] external spool from Kalico: {} {} {}", k.brand, k.material, k.name);
    mirroring_external_ = true;
    ams.set_external_spool_info_in_memory(next);
    mirroring_external_ = false;
}

void K2ohHomeView::push_external_to_kalico() {
    if (mirroring_external_ || !K2ohCfsView::applies()) {
        return;
    }
    const ExternalProfile k = kalico_external();
    const auto spool = AmsState::instance().get_external_spool_info();
    if (!k.valid || k.index < 0 || !spool || spool->material.empty() || same_profile(k, *spool)) {
        return;
    }
#if HELIX_HAS_CFS
    const std::string gcode = printer::AmsBackendCfs::slot_set_gcode(
        k.index, spool->material, spool->color_rgb, spool->brand, spool->spool_name,
        spool->spoolman_id);
    IMoonrakerAPI* api = get_moonraker_api();
    if (gcode.empty() || !api) {
        return;
    }
    spdlog::info("[K2oh Home] external spool to Kalico: {}", gcode);
    api->execute_gcode(
        gcode, []() {},
        [gcode](const MoonrakerError& err) {
            spdlog::warn("[K2oh Home] {} failed: {}", gcode, err.message);
        });
#endif
}

} // namespace helix::ui
