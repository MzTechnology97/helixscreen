// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost CFS page (see ui_k2oh_cfs_view.h).

#include "ui_k2oh_cfs_view.h"

#include "ui/ui_lazy_panel_helper.h"
#include "ui_panel_motion.h"

#include "ui_ams_environment_overlay.h"
#include "ui_button.h"
#include "ui_icon.h"
#include "ui_nav_manager.h"
#include "ui_panel_ams.h"
#include "ui_utils.h"

#include "ams_backend.h"
#if HELIX_HAS_CFS
#include "ams_backend_cfs.h"
#endif
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
#include <iterator>
#include <map>
#include <optional>
#include <string>

namespace helix::ui {

namespace {

constexpr int kRingSize = 70;
constexpr int kRingWidth = 7;
// A spool at or under this share is shown in the warning colour.
constexpr int kLowPercent = 15;
// Selection value for the external spool (the bypass row), as the backend's bypass sentinel.
constexpr int kExternal = -2;
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

/// The groups of spools that back each other up (two or more), each in swap
/// order: the loaded one first.
std::map<int, std::vector<const SlotInfo*>> runout_groups(const AmsSystemInfo& info, int loaded_slot) {
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
    for (auto it = groups.begin(); it != groups.end();) {
        auto& members = it->second;
        if (members.size() < 2) {
            it = groups.erase(it);
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
        ++it;
    }
    return groups;
}

/// "PETG: 4 (8%) » 2 (85%)" (the font has no arrow glyph) for each group of spools that back each other up,
/// the loaded one first. Without groups, the generic endless-spool sentence.
std::string runout_text(const AmsSystemInfo& info, int loaded_slot) {
    std::string text;
    for (const auto& [id, members] : runout_groups(info, loaded_slot)) {
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

K2ohCfsView::~K2ohCfsView() {
    if (root_) {
        lv_obj_remove_event_cb_with_user_data(root_, on_root_deleted, this);
    }
}

namespace {

/// Rail highlight for the overlays opened from it: 1 = CFS, 2 = Motion, 0 = back to
/// the bound active-panel look. Local styles, so they win over the bound ones.
void mark_rail(int which) {
    static constexpr const char* kEntries[] = {
        "nav_btn_home",     "nav_btn_print",    "nav_btn_cfs",      "nav_btn_motion",
        "nav_btn_controls", "nav_btn_filament", "nav_btn_settings", "nav_btn_advanced"};
    for (const char* name : kEntries) {
        lv_obj_t* btn = lv_obj_find_by_name(lv_screen_active(), name);
        if (!btn) {
            continue;
        }
        const bool mine = (which == 1 && std::strcmp(name, "nav_btn_cfs") == 0) ||
                          (which == 2 && std::strcmp(name, "nav_btn_motion") == 0);
        if (which == 0) {
            lv_obj_remove_local_style_prop(btn, LV_STYLE_BG_COLOR, LV_PART_MAIN);
            lv_obj_remove_local_style_prop(btn, LV_STYLE_BG_OPA, LV_PART_MAIN);
        } else {
            lv_obj_set_style_bg_color(btn, theme_manager_get_color("primary"), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(btn, mine ? 45 : LV_OPA_TRANSP, LV_PART_MAIN);
        }
        // ui_button paints its labels with the text colour; the opened entry's goes primary.
        if (lv_obj_t* label = lv_obj_find_by_name(btn, "nav_label")) {
            lv_obj_set_style_text_color(label, theme_manager_get_color(mine ? "primary" : "text"),
                                        LV_PART_MAIN);
        }
        if (lv_obj_t* icon = lv_obj_find_by_name(btn, "nav_icon")) {
            ui_icon_set_variant(icon, mine ? "primary" : "muted");
        }
    }
}

void mark_rail_until_closed(lv_obj_t* overlay, int which) {
    if (!overlay) {
        return;
    }
    mark_rail(which);
    NavigationManager::instance().register_overlay_close_callback(overlay, []() { mark_rail(0); });
}

} // namespace

void k2oh_open_cfs_page() {
    auto& ams_panel = get_global_ams_panel();
    if (!ams_panel.are_subjects_initialized()) {
        ams_panel.init_subjects();
    }
    if (lv_obj_t* panel_obj = ams_panel.get_panel()) {
        NavigationManager::instance().register_overlay_instance(panel_obj, &ams_panel);
        NavigationManager::instance().push_overlay(panel_obj);
    }
}

void k2oh_register_nav_callbacks() {
    lv_xml_register_event_cb(nullptr, "k2oh_nav_cfs_clicked", [](lv_event_t* /*e*/) {
        k2oh_open_cfs_page();
        mark_rail_until_closed(get_global_ams_panel().get_panel(), 1);
    });
    lv_xml_register_event_cb(nullptr, "k2oh_nav_print_clicked", [](lv_event_t* /*e*/) {
        NavigationManager::instance().set_active(PanelId::PrintSelect);
    });
    lv_xml_register_event_cb(nullptr, "k2oh_nav_motion_clicked", [](lv_event_t* /*e*/) {
        // The same overlay as Controls > Motion, cached for the process.
        static lv_obj_t* motion = nullptr;
        helix::ui::lazy_create_and_push_overlay<MotionPanel>(get_global_motion_panel, motion,
                                                             lv_screen_active(), "Motion", "K2ohNav");
        mark_rail_until_closed(motion, 2);
    });
}

std::string K2ohCfsView::runout_summary(const AmsSystemInfo& info, int loaded_slot) {
    return runout_text(info, loaded_slot);
}

int K2ohCfsView::runout_next_slot(const AmsSystemInfo& info, int loaded_slot) {
    for (const auto& [id, members] : runout_groups(info, loaded_slot)) {
        if (members.front()->global_index == loaded_slot) {
            return members[1]->global_index;
        }
    }
    return -1;
}

void K2ohCfsView::on_root_deleted(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsView*>(lv_event_get_user_data(e));
    self->slots_observer_.reset();
    self->action_observer_.reset();
    self->current_slot_observer_.reset();
    self->print_state_observer_.reset();
    if (self->stage_timer_) {
        lv_timer_delete(self->stage_timer_);
        self->stage_timer_ = nullptr;
    }
    self->root_ = nullptr;
    self->panel_ = nullptr;
    self->cards_row_ = nullptr;
    self->cards_.clear();
    self->path_rows_.clear();
}

bool K2ohCfsView::applies() {
    AmsBackend* backend = AmsState::instance().get_backend();
    // supports_gate_check() is the Fork dialect (Kalico's box module) on a CFS.
    return backend && backend->get_type() == AmsType::CFS && backend->supports_gate_check();
}

void K2ohCfsView::create(lv_obj_t* panel) {
    if (root_ || !panel) {
        return;
    }
    panel_ = panel;
    root_ = static_cast<lv_obj_t*>(lv_xml_create(panel, "k2oh_cfs_view", nullptr));
    if (!root_) {
        spdlog::error("[K2oh CFS] k2oh_cfs_view.xml did not build");
        return;
    }
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(root_, on_root_deleted, LV_EVENT_DELETE, this);
    cards_row_ = child(root_, "cards_row");

    if (lv_obj_t* back = child(root_, "cfs_back")) {
        lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, this);
    }
    build_path();
    runout_ = std::make_unique<K2ohRunoutView>([this]() {
        runout_->hide();
        set_visible(true);
    });
    runout_->create(panel);
    settings_ = std::make_unique<K2ohCfsSettingsView>(K2ohCfsSettingsView::Actions{
        [this]() {
            settings_->hide();
            set_visible(true);
        },
        [this]() {
            settings_->hide();
            runout_->show();
        },
        [this]() { click_sidebar("btn_settings"); }});
    settings_->create(panel);
    if (lv_obj_t* row = child(root_, "runout_row")) {
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_button_clicked, LV_EVENT_CLICKED, this);
    }
    for (const char* name : {"ext_chip", "bypass_row", "env_chip", "btn_cfs_check_all",
                             "btn_cfs_settings", "btn_load_unload", "btn_rfid", "btn_edit",
                             "btn_more"}) {
        if (lv_obj_t* obj = child(root_, name)) {
            lv_obj_add_event_cb(obj, on_button_clicked, LV_EVENT_CLICKED, this);
        }
    }
    if (lv_obj_t* sw = child(root_, "runout_switch")) {
        lv_obj_add_event_cb(sw, on_runout_changed, LV_EVENT_VALUE_CHANGED, this);
    }
    if (lv_obj_t* sw = child(root_, "bypass_switch_k2oh")) {
        lv_obj_add_event_cb(sw, on_bypass_changed, LV_EVENT_VALUE_CHANGED, this);
    }

    auto& ams = AmsState::instance();
    auto on_change = [](K2ohCfsView* self, int) {
        if (self->visible()) {
            self->refresh();
        }
        if (self->runout_) {
            self->runout_->refresh(); // no-op while hidden
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
    // The page always opens on the CFS view.
    if (runout_) {
        runout_->hide();
    }
    if (settings_) {
        settings_->hide();
    }
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
        lv_obj_set_name(card.root, ("cfs_card_" + std::to_string(i + 1)).c_str());
        lv_obj_add_event_cb(card.root, on_card_clicked, LV_EVENT_CLICKED, this);
        // Ring and text centred in the card (the XML flex placement is not applied here).
        lv_obj_set_flex_align(card.root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
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

    const bool external_ok = info.supports_bypass;
    if (selected_ == kExternal ? !external_ok
                               : selected_ < 0 || selected_ >= static_cast<int>(slots.size())) {
        selected_ = loaded_slot_ >= 0 || (loaded_slot_ == kExternal && external_ok) ? loaded_slot_ : 0;
    }
    // The bypass row is the external spool's card: outlined when selected.
    if (lv_obj_t* row = child(root_, "bypass_row")) {
        lv_obj_set_style_border_width(row, selected_ == kExternal ? 2 : 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, theme_manager_get_color("primary"), LV_PART_MAIN);
    }
    for (auto& card : cards_) {
        lv_obj_set_style_border_color(
            card.root, theme_manager_get_color(card.slot == selected_ ? "primary" : "card_bg"),
            LV_PART_MAIN);
    }

    update_header(info);
    refresh_path(info);
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

namespace {

/// "Loading 2 · to the head 3/5" from Kalico's operation stage; empty when unknown.
std::string operation_stage_text() {
    nlohmann::json op;
#if HELIX_HAS_CFS
    if (auto* cfs = dynamic_cast<printer::AmsBackendCfs*>(AmsState::instance().get_backend())) {
        op = cfs->box_field("operation");
    }
#endif
    if (!op.is_object() || !op.value("active", false) || !op["kind"].is_string() ||
        !op["stage"].is_string()) {
        return "";
    }
    struct Stage {
        const char* id;
        const char* label;
    };
    static constexpr Stage kLoad[] = {{"preparing", "preparing"},
                                      {"feeding_to_buffer", "to the buffer"},
                                      {"feeding_to_printhead", "to the head"},
                                      {"seating", "seating"},
                                      {"verifying", "checking"}};
    static constexpr Stage kUnload[] = {{"preparing", "preparing"},
                                        {"retracting_from_printhead", "out of the head"},
                                        {"retracting_to_cfs", "back to the CFS"},
                                        {"verifying", "checking"}};
    const std::string kind = op["kind"].get<std::string>();
    const std::string stage = op["stage"].get<std::string>();
    const bool load = kind == "load";
    if (!load && kind != "unload") {
        return "";
    }
    const Stage* stages = load ? kLoad : kUnload;
    const size_t count = load ? std::size(kLoad) : std::size(kUnload);
    for (size_t i = 0; i < count; ++i) {
        if (stage != stages[i].id) {
            continue;
        }
        std::string text = lv_tr(load ? "Loading" : "Unloading");
        if (op["slot"].is_number_integer()) {
            text += " " + std::to_string(op["slot"].get<int>() + 1);
        }
        return text + " · " + lv_tr(stages[i].label) + " " + std::to_string(i + 1) + "/" +
               std::to_string(count);
    }
    return "";
}

} // namespace

void K2ohCfsView::update_header(const AmsSystemInfo& info) {
    const bool error = info.action == AmsAction::ERROR;
    const bool busy = info.action != AmsAction::IDLE && !error;
    std::string state = lv_tr("CFS ready");
    if (error || busy) {
        state = info.operation_detail.empty() ? lv_tr(ams_action_to_string(info.action))
                                              : info.operation_detail;
    }
    if (busy) {
        if (const std::string stage = operation_stage_text(); !stage.empty()) {
            state = stage;
        }
        // A stage change alone bumps no AmsState subject: poll it while busy.
        if (!stage_timer_) {
            stage_timer_ = lv_timer_create(
                [](lv_timer_t* t) {
                    auto* self = static_cast<K2ohCfsView*>(lv_timer_get_user_data(t));
                    AmsBackend* backend = AmsState::instance().get_backend();
                    if (backend) { // also ends the timer once the operation is over
                        self->update_header(backend->get_system_info());
                    }
                },
                500, this);
        }
    } else if (stage_timer_) {
        lv_timer_delete(stage_timer_);
        stage_timer_ = nullptr;
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

    // The external spool: its own chip when the box has no bypass row, else the
    // bypass row says what it holds.
    const auto ext = AmsState::instance().get_external_spool_info();
    const std::string ext_spool =
        ext && !ext->material.empty()
            ? ext->material + (ext->brand.empty() ? "" : " " + ext->brand)
            : std::string(lv_tr("not set"));
    set_hidden(child(root_, "ext_chip"), !ext || info.supports_bypass);
    set_text(root_, "ext_label", std::string(lv_tr("External spool")) + ": " + ext_spool);
    set_text(root_, "bypass_detail", ext_spool);

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
    if (!backend || (selected_ < 0 && selected_ != kExternal)) {
        return;
    }
    const bool printing = print_running(printer_state_);
    const bool busy = ams_action_holds_system(static_cast<AmsAction>(
        lv_subject_get_int(AmsState::instance().get_ams_action_subject())));
    if (selected_ == kExternal) {
        // External spool: load = bypass on (T<external>), unload = the head's filament.
        const bool unload = loaded_slot_ == kExternal;
        if (lv_obj_t* btn = child(root_, "btn_load_unload")) {
            ui_button_set_text(btn, (std::string(lv_tr(unload ? "Unload" : "Load")) + " E").c_str());
            ui_button_set_icon(btn, unload ? "tray_arrow_up" : "arrow_down_bold");
            set_enabled(btn, !printing && !busy);
        }
        set_enabled(child(root_, "btn_rfid"), false);
        set_enabled(child(root_, "btn_edit"), true);
        return;
    }
    const SlotInfo slot = backend->get_slot_info(selected_);
    const bool empty = is_empty(slot);
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

void K2ohCfsView::click_sidebar(const char* name) {
    lv_obj_t* sidebar = panel_ ? lv_obj_find_by_name(panel_, "ams_operation_sidebar") : nullptr;
    if (lv_obj_t* btn = sidebar ? lv_obj_find_by_name(sidebar, name) : nullptr) {
        lv_obj_send_event(btn, LV_EVENT_CLICKED, nullptr);
    } else {
        spdlog::warn("[K2oh CFS] sidebar control {} not found", name);
    }
}

void K2ohCfsView::build_path() {
    lv_obj_t* card = child(root_, "path_card");
    if (!card) {
        return;
    }
    static const char* const kTitles[] = {"Slot",       "Buffer",           "Encoder",
                                          "Clog guard", "Printhead sensor", "Nozzle"};
    const lv_font_t* title_font = theme_manager_get_font("font_small");
    const lv_font_t* detail_font = theme_manager_get_font("font_xs");
    static const char* const kIcons[] = {"filament", "arrow_up_down", "sync",
                                         "gauge",    "check_circle",  "printer_3d"};
    for (int i = 0; i < 6; ++i) {
        PathRow r;
        r.row = lv_obj_create(card);
        lv_obj_remove_style_all(r.row);
        lv_obj_set_size(r.row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(r.row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(r.row, 8, LV_PART_MAIN);
        lv_obj_remove_flag(r.row, LV_OBJ_FLAG_CLICKABLE);

        r.dot = lv_obj_create(r.row);
        lv_obj_remove_style_all(r.dot);
        lv_obj_set_size(r.dot, 30, 30);
        lv_obj_set_style_radius(r.dot, i == 5 ? 8 : LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(r.dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(r.dot, 2, LV_PART_MAIN);
        lv_obj_remove_flag(r.dot, LV_OBJ_FLAG_CLICKABLE);
        r.icon = static_cast<lv_obj_t*>(lv_xml_create(r.dot, "icon", nullptr));
        if (r.icon) {
            ui_icon_set_source(r.icon, kIcons[i]);
            ui_icon_set_size(r.icon, "xs");
            lv_obj_center(r.icon);
        }

        lv_obj_t* text = lv_obj_create(r.row);
        lv_obj_remove_style_all(text);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_set_height(text, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
        r.title = lv_label_create(text);
        lv_label_set_text(r.title, lv_tr(kTitles[i]));
        lv_obj_set_style_text_color(r.title, theme_manager_get_color("text"), LV_PART_MAIN);
        lv_label_set_long_mode(r.title, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(r.title, LV_PCT(100));
        if (title_font) {
            lv_obj_set_style_text_font(r.title, title_font, LV_PART_MAIN);
        }
        r.detail = lv_label_create(text);
        lv_label_set_long_mode(r.detail, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(r.detail, LV_PCT(100));
        lv_obj_set_style_text_color(r.detail, theme_manager_get_color("text_muted"), LV_PART_MAIN);
        if (detail_font) {
            lv_obj_set_style_text_font(r.detail, detail_font, LV_PART_MAIN);
        }
        path_rows_.push_back(r);
    }
}

void K2ohCfsView::refresh_path(const AmsSystemInfo& info) {
    if (path_rows_.size() != 6) {
        return;
    }
    nlohmann::json lp = nlohmann::json::object();
#if HELIX_HAS_CFS
    if (auto* cfs = dynamic_cast<printer::AmsBackendCfs*>(AmsState::instance().get_backend())) {
        lp = cfs->box_field("load_path");
    }
#endif
    if (!lp.is_object()) {
        lp = nlohmann::json::object();
    }
    auto obj = [&lp](const char* key) {
        auto it = lp.find(key);
        return it != lp.end() && it->is_object() ? *it : nlohmann::json::object();
    };
    auto flag = [](const nlohmann::json& o, const char* key) {
        auto it = o.find(key);
        return it != o.end() && it->is_boolean() && it->get<bool>();
    };
    auto integer = [](const nlohmann::json& o, const char* key, int fallback) {
        auto it = o.find(key);
        return it != o.end() && it->is_number_integer() ? it->get<int>() : fallback;
    };
    const int source = integer(lp, "source_slot", -1);
    const SlotInfo* spool = nullptr;
    for (const auto& unit : info.units) {
        for (const auto& slot : unit.slots) {
            if (slot.global_index == source) {
                spool = &slot;
            }
        }
    }
    // A source outside the CFS slots is the external spool (bypass).
    const bool external = source >= 0 && !spool;
    const std::optional<SlotInfo> ext_spool =
        external ? AmsState::instance().get_external_spool_info() : std::nullopt;
    if (ext_spool) {
        spool = &*ext_spool;
    }
    const nlohmann::json buffer = obj("buffer");
    const nlohmann::json encoder = obj("encoder");
    const nlohmann::json sensor = obj("printhead_sensor");
    const nlohmann::json clog = obj("clog_detection");
    const bool fed = source >= 0;
    const bool at_head = flag(sensor, "detected");
    const bool sensor_error = sensor.contains("error") && !sensor["error"].is_null();
    const bool clog_on = flag(clog, "enabled");
    const bool clogged = flag(clog, "triggered");
    const int clog_events = integer(clog, "event_count", 0);
    const lv_color_t filament =
        spool ? lv_color_hex(spool->color_rgb) : theme_manager_get_color("primary");

    enum class Look { Idle, Filament, Ok, Bad };
    auto paint = [&](PathRow& r, Look look, const std::string& detail) {
        lv_color_t fg = theme_manager_get_color("text_muted");
        lv_color_t border = theme_manager_get_color("elevated_bg");
        if (look == Look::Filament) {
            fg = border = filament;
        } else if (look == Look::Ok) {
            fg = border = theme_manager_get_color("success");
        } else if (look == Look::Bad) {
            fg = border = theme_manager_get_color("danger");
        }
        lv_obj_set_style_bg_color(r.dot, theme_manager_get_color("elevated_bg"), LV_PART_MAIN);
        lv_obj_set_style_border_color(r.dot, border, LV_PART_MAIN);
        if (r.icon) {
            ui_icon_set_color(r.icon, fg, LV_OPA_COVER);
        }
        lv_label_set_text(r.detail, detail.c_str());
    };

    std::string slot_text = lv_tr("no filament");
    if (external) {
        slot_text = lv_tr("External spool");
        if (spool && !spool->material.empty()) {
            slot_text += " · " + spool->material;
        }
    } else if (fed) {
        slot_text = std::string(lv_tr("Slot")) + " " + std::to_string(source + 1);
        if (spool && !spool->material.empty()) {
            slot_text += " · " + spool->material;
        }
    }
    paint(path_rows_[0], fed ? Look::Filament : Look::Idle, slot_text);
    paint(path_rows_[1], fed ? Look::Filament : Look::Idle,
          lv_tr(flag(buffer, "active") ? "feeding" : "at rest"));
    paint(path_rows_[2], flag(encoder, "active") ? Look::Ok : Look::Idle,
          lv_tr(flag(encoder, "active") ? "moving" : "still"));
    std::string clog_text = lv_tr("off");
    if (clogged) {
        clog_text = lv_tr("clog detected");
    } else if (clog_on) {
        clog_text = clog_events > 0 ? std::string(lv_tr("active")) + " · " +
                                          std::to_string(clog_events) + " " + lv_tr("events")
                                    : lv_tr("active, no events");
    }
    paint(path_rows_[3], clogged ? Look::Bad : clog_on ? Look::Ok : Look::Idle, clog_text);
    paint(path_rows_[4], sensor_error ? Look::Bad : at_head ? Look::Ok : Look::Idle,
          lv_tr(sensor_error ? "sensor error" : at_head ? "filament present" : "empty"));
    if (path_rows_[4].icon) {
        ui_icon_set_source(path_rows_[4].icon, sensor_error ? "alert_circle" : "check_circle");
    }
    const int nozzle = lv_subject_get_int(printer_state_.get_active_extruder_temp_subject());
    paint(path_rows_[5], at_head ? Look::Filament : Look::Idle,
          std::to_string(static_cast<int>(std::lround(nozzle / 10.0))) + "°C");

    // The line behind the dots: filament colour from the slot down to the last
    // point the filament has reached.
    lv_obj_t* fill = child(root_, "path_fill");
    lv_obj_t* line = child(root_, "path_line");
    if (!fill || !line) {
        return;
    }
    lv_obj_update_layout(root_);
    auto centre = [this](int i) {
        return lv_obj_get_y(path_rows_[static_cast<size_t>(i)].row) +
               lv_obj_get_height(path_rows_[static_cast<size_t>(i)].row) / 2;
    };
    const int32_t top = centre(0);
    lv_obj_set_pos(line, 0, top);
    lv_obj_set_height(line, std::max<int32_t>(centre(5) - top, 0));
    const int last = at_head ? 5 : fed ? 2 : -1;
    if (last < 0) {
        lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(fill, 0, top);
    lv_obj_set_height(fill, std::max<int32_t>(centre(last) - top, 0));
    lv_obj_set_style_bg_color(fill, filament, LV_PART_MAIN);
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
    if (std::strcmp(name, "bypass_row") == 0) {
        self->select(kExternal);
        return;
    }
    if (std::strcmp(name, "ext_chip") == 0) {
        if (self->actions_.external_menu) {
            self->actions_.external_menu(target);
        }
        return;
    }
    if (std::strcmp(name, "runout_row") == 0) {
        if (self->runout_) {
            set_hidden(self->root_, true);
            self->runout_->show();
        }
        return;
    }
    if (std::strcmp(name, "env_chip") == 0) {
        get_ams_environment_overlay().show(lv_screen_active(), 0);
        return;
    }
    if (std::strcmp(name, "btn_cfs_check_all") == 0) {
        self->click_sidebar("btn_check_gates");
        return;
    }
    if (std::strcmp(name, "btn_cfs_settings") == 0) {
        if (self->settings_) {
            set_hidden(self->root_, true);
            self->settings_->show();
        }
        return;
    }
    const int slot = self->selected_;
    if (slot == kExternal) {
        if (std::strcmp(name, "btn_load_unload") == 0) {
            if (self->loaded_slot_ == kExternal) {
                if (self->actions_.unload) {
                    self->actions_.unload(-1); // the active filament: the external one
                }
            } else {
                on_bypass_changed(e); // the bypass switch's load, with its guards
            }
        } else if (std::strcmp(name, "btn_edit") == 0 && self->actions_.edit) {
            self->actions_.edit(kExternal); // the external spool's editor, as Spool Info opens it
        } else if (std::strcmp(name, "btn_more") == 0 && self->actions_.external_menu) {
            self->actions_.external_menu(target);
        }
        return;
    }
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

void K2ohCfsView::on_bypass_changed(lv_event_t* e) {
    auto* self = static_cast<K2ohCfsView*>(lv_event_get_user_data(e));
    // The stock switch's handler toggles bypass with its guards (filament loaded,
    // no external slot...) and resyncs ams_bypass_active, which both switches follow.
    lv_obj_t* sidebar =
        self->panel_ ? lv_obj_find_by_name(self->panel_, "ams_operation_sidebar") : nullptr;
    if (lv_obj_t* sw = sidebar ? lv_obj_find_by_name(sidebar, "bypass_switch") : nullptr) {
        lv_obj_send_event(sw, LV_EVENT_VALUE_CHANGED, nullptr);
    } else {
        spdlog::warn("[K2oh CFS] stock bypass switch not found");
    }
}

void K2ohCfsView::on_back_clicked(lv_event_t* /*e*/) {
    NavigationManager::instance().go_back();
}

} // namespace helix::ui
