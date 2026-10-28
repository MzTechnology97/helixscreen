// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost Home (see ui_k2oh_home_view.h).

#include "ui_k2oh_home_view.h"

#include "ui_button.h"
#include "ui_format_utils.h"
#include "ui_icon.h"
#include "ui_k2oh_cfs_view.h"
#include "ui_nav_manager.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "filament_database.h"
#include "led/led_controller.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "preset_materials.h"
#include "printer_state.h"
#include "temperature_controller.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace helix::ui {

namespace {

constexpr int kRingSize = 58;
constexpr int kRingWidth = 5;
constexpr int kLowPercent = 15;
constexpr int kPreheatButtons = 3;
// Clock and fan speeds have no subject that moves with them; a slow tick is enough.
constexpr uint32_t kTickMs = 2000;

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

std::string degrees(int deci) {
    return std::to_string(static_cast<int>(std::lround(deci / 10.0)));
}

PrintJobState job_state(PrinterState& ps) {
    return static_cast<PrintJobState>(lv_subject_get_int(ps.get_print_state_enum_subject()));
}

/// "K2-Citroen AMI_PETG_12h24m.gcode" -> "K2-Citroen AMI PETG 12h24m"
std::string job_title(const char* display) {
    std::string name = display ? display : "";
    if (const auto dot = name.rfind('.'); dot != std::string::npos && dot > 0) {
        name.erase(dot);
    }
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

} // namespace

K2ohHomeView::K2ohHomeView(PrinterState& printer_state, Actions actions)
    : printer_state_(printer_state), actions_(std::move(actions)) {}

K2ohHomeView::~K2ohHomeView() {
    if (timer_) {
        lv_timer_delete(timer_);
    }
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
    // Under the floating e-stop, which follows the widget grid in the panel.
    lv_obj_move_to_index(root_, 1);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    for (const char* name : {"thumb_box", "btn_tune", "temps_card", "cfs_card", "light_tile",
                             "fans_card", "btn_print_file", "btn_cooldown", "preheat_0", "preheat_1",
                             "preheat_2"}) {
        if (lv_obj_t* obj = child(root_, name)) {
            lv_obj_add_event_cb(obj, on_click, LV_EVENT_CLICKED, this);
        }
    }
    for (int i = 0; i < kPreheatButtons; ++i) {
        const std::string name = "preheat_" + std::to_string(i);
        lv_obj_t* btn = child(root_, name.c_str());
        if (!btn) {
            continue;
        }
        if (i >= presets::PRESET_COUNT) {
            set_hidden(btn, true);
            continue;
        }
        std::string label = presets::name(i);
        if (const auto mat = filament::find_material(label)) {
            label += "  " + std::to_string(mat->nozzle_recommended()) + "/" +
                     std::to_string(mat->bed_temp);
        }
        ui_button_set_text(btn, label.c_str());
    }

    auto& ps = printer_state_;
    auto& ams = AmsState::instance();
    auto job = [](K2ohHomeView* self, int) { self->refresh_job(); };
    auto temps = [](K2ohHomeView* self, int) { self->refresh_temps(); };
    auto cfs = [](K2ohHomeView* self, int) {
        self->update_visibility();
        self->refresh_cfs();
    };
    for (lv_subject_t* s :
         {ps.get_print_state_enum_subject(), ps.get_print_progress_subject(),
          ps.get_print_layer_current_subject(), ps.get_print_layer_total_subject(),
          ps.get_print_time_left_subject()}) {
        observers_.push_back(observe_int_sync<K2ohHomeView>(s, this, job));
    }
    for (lv_subject_t* s :
         {ps.get_active_extruder_temp_subject(), ps.get_active_extruder_target_subject(),
          ps.get_bed_temp_subject(), ps.get_bed_target_subject(), ps.get_chamber_temp_subject()}) {
        observers_.push_back(observe_int_sync<K2ohHomeView>(s, this, temps));
    }
    for (lv_subject_t* s : {ams.get_slots_version_subject(), ams.get_ams_type_subject(),
                            ams.get_current_slot_subject()}) {
        observers_.push_back(
            observe_int_sync<K2ohHomeView>(s, this, cfs, ams.get_subjects_lifetime()));
    }
    observers_.push_back(observe_int_sync<K2ohHomeView>(
        ps.get_led_state_subject(), this,
        [](K2ohHomeView* self, int) { self->refresh_light_and_fans(); }));

    timer_ = lv_timer_create(on_timer, kTickMs, this);
    update_visibility();
}

void K2ohHomeView::update_visibility() {
    if (!root_) {
        return;
    }
    const bool k2oh = K2ohCfsView::applies();
    const bool was_hidden = lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
    set_hidden(root_, !k2oh);
    set_hidden(child(panel_, "carousel_host"), k2oh);
    // The floating e-stop moves into the status line instead of over the cards.
    // shortcut: not put back if the box stops being Kalico's; that needs a restart.
    lv_obj_t* estop = child(panel_, "estop_fab");
    if (estop && k2oh) {
        lv_obj_set_size(estop, 36, 36);
        lv_obj_set_style_translate_x(estop, -10, LV_PART_MAIN);
        lv_obj_set_style_translate_y(estop, 8, LV_PART_MAIN);
    }
    if (k2oh && was_hidden) {
        refresh_job();
        refresh_temps();
        refresh_cfs();
        refresh_light_and_fans();
        refresh_clock();
    }
}

void K2ohHomeView::refresh_job() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    const PrintJobState state = job_state(printer_state_);
    const bool active = printer_has_job(state);
    set_hidden(child(root_, "job_active"), !active);
    set_hidden(child(root_, "job_idle"), active);

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
    set_text(root_, "state_label", chip);
    if (lv_obj_t* obj = child(root_, "state_icon")) {
        ui_icon_set_source(obj, icon);
        ui_icon_set_variant(obj, variant);
    }

    if (!active) {
        return;
    }
    set_text(root_, "job_name",
             job_title(lv_subject_get_string(printer_state_.get_print_display_filename_subject())));
    set_text(root_, "job_percent", std::to_string(progress) + "%");
    // The shared label subject holds the English verb; translate it here.
    if (lv_subject_t* verb = lv_xml_get_subject(nullptr, "print_control_primary_label")) {
        if (lv_obj_t* btn = child(root_, "btn_pause")) {
            ui_button_set_text(btn, lv_tr(lv_subject_get_string(verb)));
        }
    }
    if (lv_obj_t* bar = child(root_, "job_bar")) {
        lv_bar_set_value(bar, progress, LV_ANIM_OFF);
    }
    const int layer = lv_subject_get_int(printer_state_.get_print_layer_current_subject());
    const int layers = lv_subject_get_int(printer_state_.get_print_layer_total_subject());
    set_text(root_, "job_layer",
             layers > 0 ? std::string(lv_tr("layer")) + " " + std::to_string(layer) + " / " +
                              std::to_string(layers)
                        : "");

    const int left_s = lv_subject_get_int(printer_state_.get_print_time_left_subject());
    std::string times;
    if (left_s > 0) {
        const std::time_t end = std::time(nullptr) + left_s;
        std::tm end_tm{};
        localtime_r(&end, &end_tm);
        times = std::string(lv_tr("Left")) + " " + format_print_time(left_s / 60) + "  ·  " +
                lv_tr("Ends") + " " + format_time(&end_tm);
    }
    set_text(root_, "job_times", times);

    if (lv_obj_t* img = child(root_, "thumb")) {
        const char* path = lv_subject_get_string(printer_state_.get_print_thumbnail_path_subject());
        if (path && *path) {
            lv_image_set_src(img, path);
        }
    }
}

void K2ohHomeView::refresh_temps() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    struct Row {
        const char* value;
        const char* target;
        lv_subject_t* temp;
        lv_subject_t* goal;
    };
    const Row rows[] = {
        {"nozzle_value", "nozzle_target", printer_state_.get_active_extruder_temp_subject(),
         printer_state_.get_active_extruder_target_subject()},
        {"bed_value", "bed_target", printer_state_.get_bed_temp_subject(),
         printer_state_.get_bed_target_subject()},
        {"chamber_value", nullptr, printer_state_.get_chamber_temp_subject(), nullptr},
    };
    for (const Row& row : rows) {
        const int temp = lv_subject_get_int(row.temp);
        const int goal = row.goal ? lv_subject_get_int(row.goal) : 0;
        set_text(root_, row.value, degrees(temp) + "°");
        if (lv_obj_t* label = child(root_, row.value)) {
            // Green once a heater is on and within 2 °C of its target.
            const bool at_target = goal > 0 && std::abs(temp - goal) <= 20;
            lv_obj_set_style_text_color(
                label,
                theme_manager_get_color(goal <= 0 ? "text" : at_target ? "success" : "warning"),
                LV_PART_MAIN);
        }
        if (row.target) {
            set_text(root_, row.target, goal > 0 ? "/ " + degrees(goal) + "°" : "");
        }
    }
}

void K2ohHomeView::build_spools(int count) {
    lv_obj_t* row = child(root_, "spools_row");
    if (!row) {
        return;
    }
    lv_obj_clean(row);
    spools_.clear();
    const lv_color_t track = theme_manager_get_color("elevated_bg");
    for (int i = 0; i < count; ++i) {
        Spool s;
        s.root = lv_obj_create(row);
        lv_obj_remove_style_all(s.root);
        lv_obj_set_height(s.root, LV_PCT(100));
        lv_obj_set_flex_grow(s.root, 1);
        lv_obj_set_flex_flow(s.root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_radius(s.root, 10, LV_PART_MAIN);
        lv_obj_set_style_border_width(s.root, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s.root, theme_manager_get_color("primary"), LV_PART_MAIN);
        lv_obj_set_style_pad_gap(s.root, 2, LV_PART_MAIN);
        lv_obj_add_flag(s.root, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_remove_flag(s.root, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* box = lv_obj_create(s.root);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, kRingSize, kRingSize);
        lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);

        s.ring = lv_arc_create(box);
        lv_obj_set_size(s.ring, kRingSize, kRingSize);
        lv_obj_center(s.ring);
        lv_arc_set_rotation(s.ring, 270);
        lv_arc_set_bg_angles(s.ring, 0, 360);
        lv_arc_set_range(s.ring, 0, 100);
        lv_obj_remove_style(s.ring, nullptr, LV_PART_KNOB);
        lv_obj_remove_flag(s.ring, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s.ring, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_style_arc_width(s.ring, kRingWidth, LV_PART_MAIN);
        lv_obj_set_style_arc_width(s.ring, kRingWidth, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(s.ring, track, LV_PART_MAIN);

        s.core = lv_obj_create(box);
        lv_obj_remove_style_all(s.core);
        lv_obj_set_size(s.core, kRingSize - 16, kRingSize - 16);
        lv_obj_center(s.core);
        lv_obj_set_style_radius(s.core, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s.core, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_add_flag(s.core, LV_OBJ_FLAG_EVENT_BUBBLE);

        s.hole = lv_obj_create(s.core);
        lv_obj_remove_style_all(s.hole);
        lv_obj_set_size(s.hole, 12, 12);
        lv_obj_center(s.hole);
        lv_obj_set_style_radius(s.hole, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s.hole, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s.hole, theme_manager_get_color("card_bg"), LV_PART_MAIN);
        lv_obj_add_flag(s.hole, LV_OBJ_FLAG_EVENT_BUBBLE);

        s.material = lv_label_create(s.root);
        lv_obj_set_style_text_color(s.material, theme_manager_get_color("text"), LV_PART_MAIN);
        s.percent = lv_label_create(s.root);
        lv_obj_set_style_text_color(s.percent, theme_manager_get_color("text_muted"), LV_PART_MAIN);
        spools_.push_back(s);
    }
}

void K2ohHomeView::refresh_cfs() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
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
    if (slots.size() != spools_.size()) {
        build_spools(static_cast<int>(slots.size()));
    }
    for (size_t i = 0; i < slots.size() && i < spools_.size(); ++i) {
        const SlotInfo& slot = *slots[i];
        Spool& s = spools_[i];
        const bool empty = slot.status == SlotStatus::EMPTY || slot.status == SlotStatus::UNKNOWN;
        const bool unknown = !empty && slot.material.empty();
        const float raw = slot.get_remaining_percent();
        const int pct = raw < 0 ? -1 : std::clamp(static_cast<int>(std::lround(raw)), 0, 100);
        const bool low = pct >= 0 && pct <= kLowPercent;
        const lv_color_t filament = lv_color_hex(slot.color_rgb);

        lv_obj_set_style_border_opa(
            s.root, slot.status == SlotStatus::LOADED ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_arc_set_value(s.ring, std::max(pct, 0));
        lv_obj_set_style_arc_color(s.ring, low ? theme_manager_get_color("warning") : filament,
                                   LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(
            s.core, empty || unknown ? theme_manager_get_color("elevated_bg") : filament,
            LV_PART_MAIN);
        set_hidden(s.hole, empty || unknown);

        std::string label = std::to_string(slot.global_index + 1) + " ";
        label += empty ? lv_tr("Empty") : unknown ? "?" : slot.material;
        lv_label_set_text(s.material, label.c_str());
        lv_label_set_text(s.percent, pct >= 0 ? (std::to_string(pct) + "%").c_str() : "");
        lv_obj_set_style_text_color(
            s.percent, theme_manager_get_color(low ? "warning" : "text_muted"), LV_PART_MAIN);
    }

    std::string env;
    for (const auto& unit : info.units) {
        if (unit.environment && unit.environment->has_humidity) {
            env = std::to_string(static_cast<int>(std::lround(unit.environment->temperature_c))) +
                  "°C · " +
                  std::to_string(static_cast<int>(std::lround(unit.environment->humidity_pct))) +
                  "%";
            break;
        }
    }
    set_text(root_, "cfs_env", env);
}

void K2ohHomeView::refresh_light_and_fans() {
    if (!root_ || lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    auto& led = led::LedController::instance();
    const bool reported = !led.status_tracked_strip().empty();
    const bool on = reported ? lv_subject_get_int(printer_state_.get_led_state_subject()) != 0
                             : led.light_is_on();
    set_text(root_, "light_state", lv_tr(on ? "On" : "Off"));
    if (lv_obj_t* icon = child(root_, "light_icon")) {
        ui_icon_set_source(icon, on ? "lightbulb_on" : "lightbulb_outline");
        ui_icon_set_variant(icon, on ? "warning" : "secondary");
    }

    const auto primary = printer_state_.get_fan_state().classify_primary_fans();
    struct FanRow {
        const char* label;
        const std::string* object;
        const char* name;
    };
    const FanRow rows[] = {{"fan_part", &primary.part, "Part"},
                           {"fan_hotend", &primary.hotend, "Hotend"},
                           {"fan_aux", &primary.aux, "Aux"}};
    for (const FanRow& row : rows) {
        std::string text;
        for (const auto& fan : printer_state_.get_fans()) {
            if (!row.object->empty() && fan.object_name == *row.object) {
                text = std::string(lv_tr(row.name)) + "  " + std::to_string(fan.speed_percent) + "%";
                break;
            }
        }
        set_text(root_, row.label, text);
    }
}

void K2ohHomeView::refresh_clock() {
    if (!root_) {
        return;
    }
    const std::time_t now = std::time(nullptr);
    std::tm now_tm{};
    localtime_r(&now, &now_tm);
    set_text(root_, "clock_label", format_time(&now_tm));
}

void K2ohHomeView::on_timer(lv_timer_t* timer) {
    auto* self = static_cast<K2ohHomeView*>(lv_timer_get_user_data(timer));
    if (self->root_ && !lv_obj_has_flag(self->root_, LV_OBJ_FLAG_HIDDEN)) {
        self->refresh_clock();
        self->refresh_light_and_fans();
        self->refresh_job();
    }
}

void K2ohHomeView::on_click(lv_event_t* e) {
    auto* self = static_cast<K2ohHomeView*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    const char* name = lv_obj_get_name(target);
    if (!name) {
        return;
    }
    const auto call = [](const std::function<void()>& fn) {
        if (fn) {
            fn();
        }
    };
    if (std::strcmp(name, "thumb_box") == 0) {
        call(self->actions_.open_print_status);
    } else if (std::strcmp(name, "btn_tune") == 0) {
        call(self->actions_.open_tune);
    } else if (std::strcmp(name, "cfs_card") == 0) {
        call(self->actions_.open_cfs);
    } else if (std::strcmp(name, "temps_card") == 0 || std::strcmp(name, "fans_card") == 0) {
        NavigationManager::instance().set_active(PanelId::Controls);
    } else if (std::strcmp(name, "btn_print_file") == 0) {
        NavigationManager::instance().set_active(PanelId::PrintSelect);
    } else if (std::strcmp(name, "light_tile") == 0) {
        auto& led = led::LedController::instance();
        if (led.selected_strips().empty() || led.light_command_in_flight()) {
            return;
        }
        const bool reported = !led.status_tracked_strip().empty();
        const bool on = reported
                            ? lv_subject_get_int(self->printer_state_.get_led_state_subject()) != 0
                            : led.light_is_on();
        led.light_set(!on);
        self->refresh_light_and_fans();
    } else if (std::strcmp(name, "btn_cooldown") == 0) {
        if (auto* c = get_temperature_controller()) {
            c->set_target(HeaterType::Nozzle, 0.0, {.toast = true});
            c->set_target(HeaterType::Bed, 0.0, {.toast = true});
        }
    } else if (std::strncmp(name, "preheat_", 8) == 0) {
        const int slot = name[8] - '0';
        const auto mat = filament::find_material(presets::name(slot));
        auto* c = get_temperature_controller();
        if (mat && c) {
            c->set_target(HeaterType::Nozzle, mat->nozzle_recommended(), {.toast = true});
            c->set_target(HeaterType::Bed, mat->bed_temp, {.toast = true});
        }
    }
}

} // namespace helix::ui
