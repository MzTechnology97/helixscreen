// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost slot editor card (see ui_k2oh_slot_params.h).

#include "ui_k2oh_slot_params.h"

#include "ui_error_reporting.h"
#include "ui_k2oh_cfs_view.h"

#include "ams_backend.h"
#if HELIX_HAS_CFS
#include "ams_backend_cfs.h"
#endif
#include "ams_state.h"
#include "app_globals.h"
#include "i_moonraker_api.h"
#include "lvgl/src/others/translation/lv_translation.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace helix::ui {

namespace {

constexpr double kPaStep = 0.002;
constexpr double kPaStart = 0.020; // first step when the slot has no value yet
constexpr double kPaMax = 1.0;

struct State {
    lv_obj_t* root = nullptr;
    int kalico_slot = -1;
    double pa = 0.0;
    double saved_pa = 0.0;
    bool has_pa = false;
};

lv_obj_t* child(lv_obj_t* parent, const char* name) {
    return parent ? lv_obj_find_by_name(parent, name) : nullptr;
}

void set_text(lv_obj_t* parent, const char* name, const std::string& text) {
    if (lv_obj_t* obj = child(parent, name)) {
        lv_label_set_text(obj, text.c_str());
    }
}

nlohmann::json box_field(const char* key) {
#if HELIX_HAS_CFS
    if (auto* cfs = dynamic_cast<printer::AmsBackendCfs*>(AmsState::instance().get_backend())) {
        return cfs->box_field(key);
    }
#endif
    return nlohmann::json();
}

std::string number(double value, int decimals) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, value);
    return buf;
}

/// Where Kalico took a value from: the slot itself or the filament profile.
std::string source(const nlohmann::json& entry, const char* key) {
    const auto it = entry.find(key);
    if (it == entry.end() || !it->is_string()) {
        return "";
    }
    return lv_tr(it->get<std::string>() == "slot" ? "slot" : "profile");
}

void show_pa(State* st) {
    set_text(st->root, "param_pa", st->has_pa ? number(st->pa, 3) : "—");
    if (lv_obj_t* apply = child(st->root, "btn_pa_apply")) {
        const bool changed = st->has_pa && std::fabs(st->pa - st->saved_pa) > 1e-6;
        if (changed) {
            lv_obj_remove_flag(apply, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(apply, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void on_click(lv_event_t* e) {
    auto* st = static_cast<State*>(lv_event_get_user_data(e));
    const char* name = lv_obj_get_name(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
    if (!st || !name) {
        return;
    }
    if (std::strcmp(name, "btn_pa_minus") == 0 || std::strcmp(name, "btn_pa_plus") == 0) {
        const double step = std::strcmp(name, "btn_pa_plus") == 0 ? kPaStep : -kPaStep;
        st->pa = st->has_pa ? std::clamp(st->pa + step, 0.0, kPaMax) : kPaStart;
        st->pa = std::round(st->pa * 1000.0) / 1000.0;
        st->has_pa = true;
        show_pa(st);
        return;
    }
    if (std::strcmp(name, "btn_pa_apply") == 0) {
        IMoonrakerAPI* api = get_moonraker_api();
        if (!api || st->kalico_slot < 0) {
            return;
        }
        const std::string gcode = "_BOX_SLOT_PA_SET SLOT=" + std::to_string(st->kalico_slot) +
                                  " PRESSURE_ADVANCE=" + number(st->pa, 4);
        spdlog::info("[K2oh Slot] {}", gcode);
        api->execute_gcode(
            gcode, []() {},
            [gcode](const MoonrakerError& err) {
                NOTIFY_WARNING(lv_tr("Pressure advance not saved: {}"), err.message);
                spdlog::warn("[K2oh Slot] {} failed: {}", gcode, err.message);
            });
        // Kalico answers with the saved value in the next status; an error is notified.
        st->saved_pa = st->pa;
        show_pa(st);
    }
}

void on_delete(lv_event_t* e) {
    delete static_cast<State*>(lv_event_get_user_data(e));
}

/// "4 » 2 · lowest remaining first", the slot's runout group in swap order.
std::string runout_text(int kalico_slot) {
    const nlohmann::json groups = box_field("runout_groups");
    if (!groups.is_array()) {
        return "—";
    }
    for (const auto& group : groups) {
        const auto slots = group.value("slots", nlohmann::json::array());
        if (std::find(slots.begin(), slots.end(), kalico_slot) == slots.end()) {
            continue;
        }
        std::string text;
        for (const auto& slot : slots) {
            if (slot.is_number_integer()) {
                text += (text.empty() ? "" : " » ") + std::to_string(slot.get<int>() + 1);
            }
        }
        const std::string strategy = group.value("strategy", "");
        const char* label = strategy == "manual_order"   ? "manual order"
                            : strategy == "slot_order"   ? "slot order"
                                                         : "lowest remaining first";
        return text + " · " + lv_tr(label);
    }
    return lv_tr("no identical spool");
}

} // namespace

void k2oh_add_slot_params(lv_obj_t* form_view, int slot_index) {
    if (!form_view || !K2ohCfsView::applies()) {
        return;
    }
    const nlohmann::json slots = box_field("slots");
    if (!slots.is_array()) {
        return;
    }
    const nlohmann::json* entry = nullptr;
    for (const auto& slot : slots) {
        const bool external = slot.value("external", false);
        if (slot_index == -2 ? external : !external && slot.value("index", -1) == slot_index) {
            entry = &slot;
            break;
        }
    }
    if (!entry) {
        return;
    }
    auto* root = static_cast<lv_obj_t*>(lv_xml_create(form_view, "k2oh_slot_params", nullptr));
    if (!root) {
        return;
    }
    auto* st = new State();
    st->root = root;
    st->kalico_slot = entry->value("index", -1);
    lv_obj_add_event_cb(root, on_delete, LV_EVENT_DELETE, st);

    const std::string name = entry->value("name", "");
    const std::string orca = entry->value("orca_filament_id", "");
    set_text(root, "param_profile",
             name.empty() ? "—" : (orca.empty() ? name : name + " · Orca " + orca));

    if (const auto it = entry->find("pressure_advance"); it != entry->end() && it->is_number()) {
        st->pa = st->saved_pa = it->get<double>();
        st->has_pa = true;
    }
    set_text(root, "param_pa_source", source(*entry, "pressure_advance_source"));
    // Kalico keeps pressure advance in a filament profile: none without one.
    const bool has_profile = !entry->value("material", "").empty();
    for (const char* button : {"btn_pa_minus", "btn_pa_plus", "btn_pa_apply"}) {
        if (lv_obj_t* obj = child(root, button)) {
            lv_obj_add_event_cb(obj, on_click, LV_EVENT_CLICKED, st);
            if (!has_profile) {
                lv_obj_add_state(obj, LV_STATE_DISABLED);
            }
        }
    }
    show_pa(st);

    if (const auto it = entry->find("max_flow"); it != entry->end() && it->is_number()) {
        std::string flow = number(it->get<double>(), 0) + " mm³/s";
        if (const std::string src = source(*entry, "max_flow_source"); !src.empty()) {
            flow += " · " + src;
        }
        set_text(root, "param_flow", flow);
    }
    if (const auto it = entry->find("target_temp"); it != entry->end() && it->is_number()) {
        set_text(root, "param_temp", std::to_string(it->get<int>()) + " °C");
    }
    set_text(root, "param_runout", runout_text(st->kalico_slot));
}

} // namespace helix::ui
