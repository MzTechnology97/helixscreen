// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost Home widgets (see k2oh_home_widget.h).

#include "k2oh_home_widget.h"

#include "ui_button.h"
#include "ui_fan_control_overlay.h"
#include "ui_format_utils.h"
#include "ui_icon.h"
#include "ui_k2oh_cfs_view.h"
#include "ui_nav_manager.h"
#include "ui_panel_ams.h"
#include "ui_panel_print_status.h"
#include "ui_print_tune_overlay.h"

#include "ams_backend.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "filament_database.h"
#include "led/led_controller.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "observer_factory.h"
#include "panel_widget_registry.h"
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

namespace helix {

namespace {

constexpr int kRingSize = 58;
constexpr int kRingWidth = 5;
constexpr int kLowPercent = 15;
constexpr int kPreheatButtons = 3;
// Fan speeds and the end clock have no subject per tick; a slow timer is enough.
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

/// "K2-Citroen AMI_PETG_12h24m.gcode" -> "K2-Citroen AMI PETG 12h24m"
std::string job_title(const char* display) {
    std::string name = display ? display : "";
    if (const auto dot = name.rfind('.'); dot != std::string::npos && dot > 0) {
        name.erase(dot);
    }
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}

void open_fan_control(lv_obj_t* parent_screen) {
    // One overlay for the process, as the fan stack widget caches its own.
    static lv_obj_t* panel = nullptr;
    auto& overlay = get_fan_control_overlay();
    if (!panel && parent_screen) {
        if (!overlay.are_subjects_initialized()) {
            overlay.init_subjects();
        }
        overlay.register_callbacks();
        overlay.set_api(get_moonraker_api());
        panel = overlay.create(parent_screen);
    }
    if (panel) {
        overlay.set_api(get_moonraker_api());
        NavigationManager::instance().register_overlay_instance(panel, &overlay);
        NavigationManager::instance().push_overlay(panel);
    }
}

} // namespace

void register_k2oh_home_widgets() {
    static const std::pair<const char*, K2ohHomeWidget::Kind> kWidgets[] = {
        {"k2oh_job", K2ohHomeWidget::Kind::Job},     {"k2oh_temps", K2ohHomeWidget::Kind::Temps},
        {"k2oh_cfs", K2ohHomeWidget::Kind::Cfs},     {"k2oh_light", K2ohHomeWidget::Kind::Light},
        {"k2oh_fans", K2ohHomeWidget::Kind::Fans},
    };
    for (const auto& [id, kind] : kWidgets) {
        const std::string widget_id = id;
        const auto widget_kind = kind;
        register_widget_factory(widget_id, [widget_id, widget_kind](const std::string&) {
            return std::make_unique<K2ohHomeWidget>(get_printer_state(), widget_kind, widget_id);
        });
    }
}

K2ohHomeWidget::K2ohHomeWidget(PrinterState& printer_state, Kind kind, std::string id)
    : printer_state_(printer_state), kind_(kind), id_(std::move(id)) {}

K2ohHomeWidget::~K2ohHomeWidget() {
    detach();
}

void K2ohHomeWidget::attach(lv_obj_t* widget_obj, lv_obj_t* parent_screen) {
    root_ = widget_obj;
    parent_screen_ = parent_screen;
    if (!root_) {
        return;
    }
    install_delete_hook(root_);
    if (kind_ == Kind::Light) {
        // Icon and labels centred in the tile (the XML flex placement is not applied here).
        lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    }

    for (const char* name : {"thumb_box", "btn_tune", "btn_print_file", "btn_cooldown", "preheat_0",
                             "preheat_1", "preheat_2"}) {
        if (lv_obj_t* obj = child(root_, name)) {
            lv_obj_add_event_cb(obj, on_click, LV_EVENT_CLICKED, this);
        }
    }
    if (kind_ != Kind::Job) {
        lv_obj_add_event_cb(root_, on_click, LV_EVENT_CLICKED, this);
    }
    if (kind_ == Kind::Light) {
        for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED}) {
            lv_obj_add_event_cb(root_, on_light_drag, code, this);
        }
        // The vertical drag is the brightness, not a scroll or a page swipe.
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    }

    auto& ps = printer_state_;
    auto& ams = AmsState::instance();
    auto update = [](K2ohHomeWidget* self, int) { self->refresh(); };
    std::vector<lv_subject_t*> subjects;
    switch (kind_) {
    case Kind::Job:
        subjects = {ps.get_print_state_enum_subject(), ps.get_print_progress_subject(),
                    ps.get_print_layer_current_subject(), ps.get_print_layer_total_subject(),
                    ps.get_print_time_left_subject()};
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
        break;
    case Kind::Temps:
        subjects = {ps.get_active_extruder_temp_subject(), ps.get_active_extruder_target_subject(),
                    ps.get_bed_temp_subject(), ps.get_bed_target_subject(),
                    ps.get_chamber_temp_subject()};
        break;
    case Kind::Cfs:
        for (lv_subject_t* s : {ams.get_slots_version_subject(), ams.get_current_slot_subject()}) {
            observers_.push_back(
                ui::observe_int_sync<K2ohHomeWidget>(s, this, update, ams.get_subjects_lifetime()));
        }
        break;
    case Kind::Light:
        subjects = {ps.get_led_state_subject(), ps.get_led_brightness_subject()};
        break;
    case Kind::Fans:
        subjects = {ps.get_fan_speed_subject(), ps.get_fans_version_subject()};
        break;
    }
    for (lv_subject_t* s : subjects) {
        observers_.push_back(ui::observe_int_sync<K2ohHomeWidget>(s, this, update));
    }
    if (kind_ == Kind::Job || kind_ == Kind::Fans) {
        timer_ = lv_timer_create(on_timer, kTickMs, this);
    }
    refresh();
}

void K2ohHomeWidget::release() {
    observers_.clear();
    if (timer_) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    spools_.clear();
    root_ = nullptr;
    parent_screen_ = nullptr;
}

void K2ohHomeWidget::detach() {
    release();
}

void K2ohHomeWidget::on_hooked_root_deleted() {
    release();
}

void K2ohHomeWidget::refresh() {
    if (!root_) {
        return;
    }
    switch (kind_) {
    case Kind::Job:
        refresh_job();
        break;
    case Kind::Temps:
        refresh_temps();
        break;
    case Kind::Cfs:
        refresh_cfs();
        break;
    case Kind::Light:
        refresh_light();
        break;
    case Kind::Fans:
        refresh_fans();
        break;
    }
}

void K2ohHomeWidget::refresh_job() {
    const auto state =
        static_cast<PrintJobState>(lv_subject_get_int(printer_state_.get_print_state_enum_subject()));
    const bool active = printer_has_job(state);
    set_hidden(child(root_, "job_active"), !active);
    set_hidden(child(root_, "job_idle"), active);
    if (!active) {
        return;
    }
    const int progress =
        std::clamp(lv_subject_get_int(printer_state_.get_print_progress_subject()), 0, 100);
    set_text(root_, "job_name",
             job_title(lv_subject_get_string(printer_state_.get_print_display_filename_subject())));
    set_text(root_, "job_percent", std::to_string(progress) + "%");
    if (lv_obj_t* bar = child(root_, "job_bar")) {
        lv_bar_set_value(bar, progress, LV_ANIM_OFF);
    }
    // The shared label subject holds the English verb; translate it here.
    if (lv_subject_t* verb = lv_xml_get_subject(nullptr, "print_control_primary_label")) {
        if (lv_obj_t* btn = child(root_, "btn_pause")) {
            ui_button_set_text(btn, lv_tr(lv_subject_get_string(verb)));
        }
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
        times = std::string(lv_tr("Left")) + " " + ui::format_print_time(left_s / 60) + "  ·  " +
                lv_tr("Ends") + " " + ui::format_time(&end_tm);
    }
    set_text(root_, "job_times", times);
    if (lv_obj_t* img = child(root_, "thumb")) {
        const char* path = lv_subject_get_string(printer_state_.get_print_thumbnail_path_subject());
        if (path && *path) {
            lv_image_set_src(img, path);
        }
    }
}

void K2ohHomeWidget::refresh_temps() {
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

void K2ohHomeWidget::build_spools(int count) {
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

void K2ohHomeWidget::refresh_cfs() {
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

void K2ohHomeWidget::refresh_light() {
    auto& led = led::LedController::instance();
    const bool reported = !led.status_tracked_strip().empty();
    const bool on = reported ? lv_subject_get_int(printer_state_.get_led_state_subject()) != 0
                             : led.light_is_on();
    if (dragging_) {
        return; // the finger owns the tile until release
    }
    const int pct = std::clamp(lv_subject_get_int(printer_state_.get_led_brightness_subject()), 0, 100);
    show_brightness(on ? pct : 0, on);
}

void K2ohHomeWidget::show_brightness(int pct, bool on) {
    std::string state = lv_tr(on ? "light_lit" : "light_unlit");
    if (on && pct > 0) {
        state += " · " + std::to_string(pct) + "%";
    }
    set_text(root_, "light_state", state);
    if (lv_obj_t* icon = child(root_, "light_icon")) {
        ui_icon_set_source(icon, on ? "lightbulb_on" : "lightbulb_outline");
        ui_icon_set_variant(icon, on ? "warning" : "secondary");
    }
    // Home Assistant style: the tile fills with light from the bottom, edge to
    // edge (the fill reaches past the tile's padding).
    if (lv_obj_t* fill = child(root_, "light_fill")) {
        const int32_t width = lv_obj_get_width(root_);
        const int32_t height = lv_obj_get_height(root_);
        const int32_t pad_bottom = lv_obj_get_style_pad_bottom(root_, LV_PART_MAIN);
        lv_obj_set_size(fill, width, on ? height * pct / 100 : 0);
        lv_obj_align(fill, LV_ALIGN_BOTTOM_MID, 0, pad_bottom);
    }
}

void K2ohHomeWidget::on_light_drag(lv_event_t* e) {
    auto* self = static_cast<K2ohHomeWidget*>(lv_event_get_user_data(e));
    lv_indev_t* indev = lv_indev_active();
    if (!self->root_ || !indev) {
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    // A drag shorter than this is still a tap (toggle).
    constexpr int32_t kDragThreshold = 12;
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED: {
        auto& led = led::LedController::instance();
        const bool reported = !led.status_tracked_strip().empty();
        const bool on = reported ? lv_subject_get_int(self->printer_state_.get_led_state_subject()) != 0
                                 : led.light_is_on();
        self->drag_start_y_ = point.y;
        self->drag_start_pct_ =
            on ? std::clamp(lv_subject_get_int(self->printer_state_.get_led_brightness_subject()), 0,
                            100)
               : 0;
        self->drag_pct_ = self->drag_start_pct_;
        self->dragging_ = false;
        self->dragged_ = false;
        break;
    }
    case LV_EVENT_PRESSING: {
        const int32_t dy = self->drag_start_y_ - point.y; // up = brighter
        if (!self->dragging_ && std::abs(dy) < kDragThreshold) {
            return;
        }
        self->dragging_ = true;
        // The tile's height is the whole 0-100 % range.
        const int32_t height = std::max<int32_t>(lv_obj_get_height(self->root_), 1);
        self->drag_pct_ = std::clamp(self->drag_start_pct_ + static_cast<int>(dy * 100 / height), 0, 100);
        self->show_brightness(self->drag_pct_, self->drag_pct_ > 0);
        break;
    }
    case LV_EVENT_RELEASED:
        if (self->dragging_) {
            self->dragging_ = false;
            self->dragged_ = true; // the click that follows is not a toggle
            auto& led = led::LedController::instance();
            if (!led.selected_strips().empty() && !led.light_command_in_flight()) {
                led.set_brightness_all(self->drag_pct_);
            }
        }
        break;
    default:
        break;
    }
}

void K2ohHomeWidget::refresh_fans() {
    const auto primary = printer_state_.get_fan_state().classify_primary_fans();
    struct FanRow {
        const char* base;
        const std::string* object;
        const char* name;
    };
    const FanRow rows[] = {{"fan_part", &primary.part, "Part"},
                           {"fan_hotend", &primary.hotend, "Hotend"},
                           {"fan_aux", &primary.aux, "Aux"}};
    for (const FanRow& row : rows) {
        const std::string base = row.base;
        const FanInfo* found = nullptr;
        for (const auto& fan : printer_state_.get_fans()) {
            if (!row.object->empty() && fan.object_name == *row.object) {
                found = &fan;
                break;
            }
        }
        set_hidden(child(root_, (base + "_row").c_str()), !found);
        if (!found) {
            continue;
        }
        set_text(root_, (base + "_name").c_str(), lv_tr(row.name));
        set_text(root_, (base + "_value").c_str(), std::to_string(found->speed_percent) + "%");
        if (lv_obj_t* icon = child(root_, (base + "_icon").c_str())) {
            const bool spinning = found->speed_percent > 0;
            ui_icon_set_source(icon, spinning ? "fan" : "fan_off");
            ui_icon_set_variant(icon, spinning ? "primary" : "secondary");
        }
    }
}

void K2ohHomeWidget::on_timer(lv_timer_t* timer) {
    static_cast<K2ohHomeWidget*>(lv_timer_get_user_data(timer))->refresh();
}

void K2ohHomeWidget::on_click(lv_event_t* e) {
    auto* self = static_cast<K2ohHomeWidget*>(lv_event_get_user_data(e));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (!self->root_) {
        return;
    }
    self->record_interaction();
    if (target == self->root_) {
        switch (self->kind_) {
        case Kind::Temps:
            NavigationManager::instance().set_active(PanelId::Controls);
            return;
        case Kind::Cfs:
            ui::k2oh_open_cfs_page();
            return;
        case Kind::Fans:
            open_fan_control(self->parent_screen_);
            return;
        case Kind::Light: {
            if (self->dragged_) {
                self->dragged_ = false; // that was a brightness drag, not a tap
                return;
            }
            auto& led = led::LedController::instance();
            if (led.selected_strips().empty() || led.light_command_in_flight()) {
                return;
            }
            const bool reported = !led.status_tracked_strip().empty();
            const bool on =
                reported ? lv_subject_get_int(self->printer_state_.get_led_state_subject()) != 0
                         : led.light_is_on();
            led.light_set(!on);
            self->refresh_light();
            return;
        }
        case Kind::Job:
            return;
        }
    }
    const char* name = lv_obj_get_name(target);
    if (!name) {
        return;
    }
    if (std::strcmp(name, "thumb_box") == 0) {
        PrintStatusPanel::push_overlay(self->parent_screen_);
    } else if (std::strcmp(name, "btn_tune") == 0) {
        get_print_tune_overlay().show(self->parent_screen_, get_moonraker_api(),
                                          self->printer_state_);
    } else if (std::strcmp(name, "btn_print_file") == 0) {
        NavigationManager::instance().set_active(PanelId::PrintSelect);
    } else if (std::strcmp(name, "btn_cooldown") == 0) {
        if (auto* c = get_temperature_controller()) {
            c->set_target(HeaterType::Nozzle, 0.0, {.toast = true});
            c->set_target(HeaterType::Bed, 0.0, {.toast = true});
        }
    } else if (std::strncmp(name, "preheat_", 8) == 0) {
        const auto mat = filament::find_material(presets::name(name[8] - '0'));
        auto* c = get_temperature_controller();
        if (mat && c) {
            c->set_target(HeaterType::Nozzle, mat->nozzle_recommended(), {.toast = true});
            c->set_target(HeaterType::Bed, mat->bed_temp, {.toast = true});
        }
    }
}

} // namespace helix
