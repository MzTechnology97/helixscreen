// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <lvgl.h>

#include <cstdint>
#include <vector>

namespace helix::ui {

/**
 * @brief K2-OpenHost Touch & Input page: touch controller sensitivity and
 * debounce (Goodix GT911 behind the gt9xxnew_ts driver) and a live test pad.
 *
 * The controller card shows only when the chip's config can be read over I2C
 * (registers 0x8047..0x8100). A change is written the same way (checksum and
 * config-fresh flag included), read back to confirm, and the first write
 * saves the original config next to settings.json for "Restore original".
 * The GT911 keeps an accepted config in its own flash.
 *
 * @param overlay_root The Touch & Input overlay
 */
void k2oh_touch_setup(lv_obj_t* overlay_root);

namespace k2oh_touch {

constexpr size_t kConfigLen = 186; ///< 0x8047..0x8100, checksum and fresh flag included
constexpr size_t kShake = 8;       ///< 0x804F finger shake count (bits 0-3)
constexpr size_t kTouchLevel = 12; ///< 0x8053 screen touch level
constexpr size_t kLeaveLevel = 13; ///< 0x8054 screen leave level

/// The 186-byte config to write: thresholds and shake count replaced,
/// checksum recomputed, fresh flag set. Empty if @p cfg is too short.
std::vector<uint8_t> build_config(const std::vector<uint8_t>& cfg, uint8_t touch_level,
                                  uint8_t leave_level, uint8_t shake);

} // namespace k2oh_touch

} // namespace helix::ui
