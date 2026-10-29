// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost Touch & Input page (see ui_k2oh_touch.h).

#include "ui_k2oh_touch.h"

#include "ui_error_reporting.h"

#include "app_globals.h"
#include "data_root_resolver.h"
#include "lvgl/src/others/translation/lv_translation.h"
#include "printer_state.h"
#include "theme_manager.h"

#include <spdlog/spdlog.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef __linux__
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace helix::ui {

namespace k2oh_touch {

constexpr size_t kChecksum = 184; // 0x80FF, then 0x8100 config fresh

std::vector<uint8_t> build_config(const std::vector<uint8_t>& cfg, uint8_t touch_level,
                                  uint8_t leave_level, uint8_t shake) {
    if (cfg.size() < kConfigLen) {
        return {};
    }
    std::vector<uint8_t> out(cfg.begin(), cfg.begin() + kConfigLen);
    out[kTouchLevel] = touch_level;
    out[kLeaveLevel] = leave_level;
    out[kShake] = static_cast<uint8_t>((out[kShake] & 0xF0) | (shake & 0x0F));
    uint8_t sum = 0;
    for (size_t i = 0; i < kChecksum; ++i) {
        sum = static_cast<uint8_t>(sum + out[i]);
    }
    out[kChecksum] = static_cast<uint8_t>(~sum + 1);
    out[kChecksum + 1] = 1;
    return out;
}

} // namespace k2oh_touch

namespace {

using namespace k2oh_touch;

constexpr const char* kDriverDir = "/sys/bus/i2c/drivers/gt9xxnew_ts";

struct Level {
    uint8_t touch;
    uint8_t leave;
};
// Low / Normal (stock K2: 100/70) / High / Gloves; lower = lighter touch.
constexpr Level kSensitivity[] = {{120, 90}, {100, 70}, {75, 50}, {55, 38}};
// Off / Light (stock K2: 1) / Strong.
constexpr uint8_t kShakeCount[] = {0, 1, 3};

std::string backup_path() {
    return helix::get_user_config_dir() + "/gt911-config.orig";
}

std::vector<uint8_t> read_backup() {
    std::ifstream in(backup_path());
    std::vector<uint8_t> out;
    std::string token;
    while (in >> token) {
        out.push_back(static_cast<uint8_t>(std::strtoul(token.c_str(), nullptr, 16)));
    }
    return out.size() >= kConfigLen ? out : std::vector<uint8_t>{};
}

/// Keeps the config found before the first K2-OpenHost write; never overwritten.
bool save_backup(const std::vector<uint8_t>& cfg) {
    if (std::filesystem::exists(backup_path())) {
        return true;
    }
    std::ofstream out(backup_path());
    for (const uint8_t b : cfg) {
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%02X ", b);
        out << hex;
    }
    return static_cast<bool>(out);
}

/// The chip's config registers (0x8047..0x8100) over I2C: read into @p data when
/// @p read, else written from it. Not /proc/gt9xx_config: the K2 driver overflows
/// its buffer (kernel oops) when that file is read in chunks under ~2.5 KB.
bool transfer(std::vector<uint8_t>& data, bool read) {
#ifdef __linux__
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(kDriverDir, ec)) {
        const std::string name = entry.path().filename().string(); // "3-0014"
        const auto dash = name.find('-');
        if (dash == std::string::npos || !std::isdigit(static_cast<unsigned char>(name[0]))) {
            continue;
        }
        const std::string dev = "/dev/i2c-" + name.substr(0, dash);
        const auto addr = static_cast<uint16_t>(std::strtoul(name.c_str() + dash + 1, nullptr, 16));
        const int fd = open(dev.c_str(), O_RDWR);
        if (fd < 0) {
            spdlog::warn("[K2oh Touch] open {} failed: {}", dev, std::strerror(errno));
            return false;
        }
        uint8_t reg[2] = {0x80, 0x47};
        std::vector<uint8_t> out(reg, reg + 2);
        if (!read) {
            out.insert(out.end(), data.begin(), data.end());
        }
        data.resize(kConfigLen);
        i2c_msg msgs[2] = {{addr, 0, static_cast<uint16_t>(read ? 2 : out.size()), out.data()},
                           {addr, I2C_M_RD, static_cast<uint16_t>(kConfigLen), data.data()}};
        const int count = read ? 2 : 1;
        i2c_rdwr_ioctl_data xfer{msgs, static_cast<uint32_t>(count)};
        const bool ok = ioctl(fd, I2C_RDWR, &xfer) == count;
        if (!ok) {
            spdlog::warn("[K2oh Touch] I2C {} {} 0x{:02X} failed: {}", read ? "read" : "write", dev,
                         addr, std::strerror(errno));
        }
        close(fd);
        return ok;
    }
#else
    (void)data;
    (void)read;
#endif
    return false;
}

std::vector<uint8_t> read_controller() {
    std::vector<uint8_t> cfg;
    return transfer(cfg, true) ? cfg : std::vector<uint8_t>{};
}

bool write_controller(const std::vector<uint8_t>& cfg) {
    std::vector<uint8_t> data = cfg;
    return transfer(data, false);
}

struct Controller {
    lv_obj_t* card = nullptr;
    std::vector<uint8_t> wanted; // config last written, checked on read-back
};

lv_obj_t* find(lv_obj_t* parent, const char* name) {
    return parent ? lv_obj_find_by_name(parent, name) : nullptr;
}

// Painted on the main state: ui_button's text contrast follows it, the checked state it does not.
void set_checked(lv_obj_t* obj, bool on) {
    if (on) {
        lv_obj_set_style_bg_color(obj, theme_manager_get_color("primary"), LV_PART_MAIN);
    } else {
        lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_COLOR, LV_PART_MAIN);
    }
}

void set_status(Controller* c, const char* text) {
    if (lv_obj_t* label = find(c->card, "touch_ctl_status")) {
        lv_label_set_text(label, text);
    }
}

/// Highlights the presets matching what the chip reports now.
void show_selection(Controller* c, const std::vector<uint8_t>& cfg) {
    char name[16];
    for (int i = 0; i < 4; ++i) {
        std::snprintf(name, sizeof(name), "sens_%d", i);
        if (lv_obj_t* btn = find(c->card, name)) {
            set_checked(btn, cfg.size() > kLeaveLevel && cfg[kTouchLevel] == kSensitivity[i].touch &&
                                 cfg[kLeaveLevel] == kSensitivity[i].leave);
        }
    }
    for (int i = 0; i < 3; ++i) {
        std::snprintf(name, sizeof(name), "deb_%d", i);
        if (lv_obj_t* btn = find(c->card, name)) {
            set_checked(btn, cfg.size() > kShake && (cfg[kShake] & 0x0F) == kShakeCount[i]);
        }
    }
    if (lv_obj_t* restore = find(c->card, "btn_touch_restore")) {
        if (std::filesystem::exists(backup_path())) {
            lv_obj_remove_flag(restore, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(restore, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void on_read_back(lv_timer_t* timer) {
    auto* c = static_cast<Controller*>(lv_timer_get_user_data(timer));
    const std::vector<uint8_t> now = read_controller();
    const bool applied = now.size() > kLeaveLevel && c->wanted.size() > kLeaveLevel &&
                         now[kTouchLevel] == c->wanted[kTouchLevel] &&
                         now[kLeaveLevel] == c->wanted[kLeaveLevel] &&
                         (now[kShake] & 0x0F) == (c->wanted[kShake] & 0x0F);
    spdlog::info("[K2oh Touch] read back: touch {} leave {} shake {} -> {}",
                 now.size() > kLeaveLevel ? now[kTouchLevel] : -1,
                 now.size() > kLeaveLevel ? now[kLeaveLevel] : -1,
                 now.size() > kShake ? now[kShake] : -1, applied ? "applied" : "NOT applied");
    set_status(c, applied ? lv_tr("Saved in the touch controller")
                          : lv_tr("The touch controller did not accept the change"));
    show_selection(c, now);
}

void write_and_check(Controller* c, const std::vector<uint8_t>& cfg) {
    if (get_printer_state().is_print_in_progress()) {
        NOTIFY_WARNING(lv_tr("Change the touch settings when the printer is not printing"));
        return;
    }
    const std::vector<uint8_t> current = read_controller();
    if (cfg.size() < kConfigLen || current.size() < kConfigLen || !save_backup(current)) {
        NOTIFY_WARNING(lv_tr("Touch controller not readable, nothing changed"));
        return;
    }
    c->wanted = cfg;
    spdlog::info("[K2oh Touch] writing touch {} leave {} shake {}", cfg[kTouchLevel],
                 cfg[kLeaveLevel], cfg[kShake]);
    if (!write_controller(cfg)) {
        set_status(c, lv_tr("The touch controller did not accept the change"));
        return;
    }
    set_status(c, lv_tr("Checking..."));
    lv_timer_t* t = lv_timer_create(on_read_back, 300, c);
    lv_timer_set_repeat_count(t, 1);
}

void on_controller_click(lv_event_t* e) {
    auto* c = static_cast<Controller*>(lv_event_get_user_data(e));
    const char* name = lv_obj_get_name(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
    if (!c || !name) {
        return;
    }
    const std::vector<uint8_t> cfg = read_controller();
    if (cfg.size() < kConfigLen) {
        NOTIFY_WARNING(lv_tr("Touch controller not readable, nothing changed"));
        return;
    }
    if (std::strncmp(name, "sens_", 5) == 0) {
        const Level& l = kSensitivity[std::atoi(name + 5)];
        write_and_check(c, build_config(cfg, l.touch, l.leave, cfg[kShake]));
    } else if (std::strncmp(name, "deb_", 4) == 0) {
        write_and_check(c, build_config(cfg, cfg[kTouchLevel], cfg[kLeaveLevel],
                              kShakeCount[std::atoi(name + 4)]));
    } else if (std::strcmp(name, "btn_touch_restore") == 0) {
        const std::vector<uint8_t> orig = read_backup();
        if (!orig.empty()) {
            write_and_check(c, build_config(orig, orig[kTouchLevel], orig[kLeaveLevel], orig[kShake]));
        }
    }
}

void add_controller_card(lv_obj_t* column) {
    const std::vector<uint8_t> cfg = read_controller();
    if (!column || cfg.size() < kConfigLen) {
        return;
    }
    auto* card = static_cast<lv_obj_t*>(lv_xml_create(column, "k2oh_touch_controller", nullptr));
    if (!card) {
        return;
    }
    lv_obj_move_to_index(card, 0);
    auto* c = new Controller{card, {}};
    lv_obj_add_event_cb(
        card, [](lv_event_t* e) { delete static_cast<Controller*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, c);
    for (const char* name : {"sens_0", "sens_1", "sens_2", "sens_3", "deb_0", "deb_1", "deb_2",
                             "btn_touch_restore"}) {
        if (lv_obj_t* btn = find(card, name)) {
            lv_obj_add_event_cb(btn, on_controller_click, LV_EVENT_CLICKED, c);
        }
    }
    show_selection(c, cfg);
    spdlog::info("[K2oh Touch] controller: touch {} leave {} shake {}", cfg[kTouchLevel],
                 cfg[kLeaveLevel], cfg[kShake]);
}

// ---- Test pad: touches, and repeats within 150 ms / 40 px of the last lift ----

constexpr uint32_t kRepeatMs = 150;
constexpr int32_t kRepeatPx = 40;
constexpr uint32_t kMaxMarks = 240; // oldest marks go first

struct Pad {
    lv_obj_t* pad = nullptr;
    lv_obj_t* stats = nullptr;
    int touches = 0;
    int repeats = 0;
    uint32_t released_at = 0;
    lv_point_t last{-1000, -1000};
    lv_point_t trail{-1000, -1000};
};

void show_stats(Pad* p) {
    const std::string text = std::string(lv_tr("Touches")) + " " + std::to_string(p->touches) +
                             "   " + lv_tr("Double") + " " + std::to_string(p->repeats);
    lv_label_set_text(p->stats, text.c_str());
}

void mark(Pad* p, lv_point_t at, int32_t size, lv_color_t color) {
    while (lv_obj_get_child_count(p->pad) >= kMaxMarks) {
        lv_obj_delete(lv_obj_get_child(p->pad, 0));
    }
    lv_obj_t* dot = lv_obj_create(p->pad);
    lv_obj_remove_style_all(dot);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_pos(dot, at.x - size / 2, at.y - size / 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
}

lv_point_t pad_point(Pad* p) {
    lv_point_t pt{};
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t area;
    lv_obj_get_content_coords(p->pad, &area);
    return {pt.x - area.x1, pt.y - area.y1};
}

void on_pad(lv_event_t* e) {
    auto* p = static_cast<Pad*>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        const lv_point_t at = pad_point(p);
        const bool repeat = lv_tick_elaps(p->released_at) < kRepeatMs &&
                            std::abs(at.x - p->last.x) < kRepeatPx &&
                            std::abs(at.y - p->last.y) < kRepeatPx;
        ++p->touches;
        if (repeat) {
            ++p->repeats;
        }
        mark(p, at, 22, theme_manager_get_color(repeat ? "warning" : "success"));
        p->last = p->trail = at;
        show_stats(p);
    } else if (code == LV_EVENT_PRESSING) {
        const lv_point_t at = pad_point(p);
        if (std::abs(at.x - p->trail.x) + std::abs(at.y - p->trail.y) >= 6) {
            mark(p, at, 5, theme_manager_get_color("primary"));
            p->trail = at;
        }
    } else if (code == LV_EVENT_RELEASED) {
        p->released_at = lv_tick_get();
    }
}

void setup_test_pad(lv_obj_t* root) {
    lv_obj_t* pad = find(root, "touch_pad");
    lv_obj_t* stats = find(root, "touch_stats");
    if (!pad || !stats) {
        return;
    }
    auto* p = new Pad{pad, stats};
    lv_obj_add_event_cb(
        pad, [](lv_event_t* e) { delete static_cast<Pad*>(lv_event_get_user_data(e)); },
        LV_EVENT_DELETE, p);
    for (const lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED}) {
        lv_obj_add_event_cb(pad, on_pad, code, p);
    }
    if (lv_obj_t* clear = find(root, "btn_touch_clear")) {
        lv_obj_add_event_cb(
            clear,
            [](lv_event_t* e) {
                auto* p = static_cast<Pad*>(lv_event_get_user_data(e));
                lv_obj_clean(p->pad);
                p->touches = p->repeats = 0;
                show_stats(p);
            },
            LV_EVENT_CLICKED, p);
    }
    show_stats(p);
}

} // namespace

void k2oh_touch_setup(lv_obj_t* overlay_root) {
    if (!overlay_root) {
        return;
    }
    add_controller_card(find(overlay_root, "touch_settings_col"));
    setup_test_pad(overlay_root);
}

} // namespace helix::ui
