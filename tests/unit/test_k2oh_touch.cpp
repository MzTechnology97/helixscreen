// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost Touch & Input: GT911 config parsing and the bytes written.

#include "ui_k2oh_touch.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui::k2oh_touch;

namespace {

// Head of the K2 Pro's /proc/gt9xx_config (2026-10-29), padded to 240 bytes.
std::string proc_text() {
    std::string text = "==== GT9XX config init value====\n0x00 0x00 0x00 0x00 \n\n"
                       "==== GT9XX config real value====\n"
                       "0x63 0xE0 0x01 0x20 0x03 0x02 0x0D 0x00 \n"
                       "0x01 0x08 0x3C 0x0F 0x64 0x46 0x03 0x05 \n";
    for (int i = 16; i < 240; ++i) {
        text += "0x00 ";
    }
    return text;
}

} // namespace

TEST_CASE("K2oh touch: reads the real config, not the driver's init copy", "[k2oh][touch]") {
    const auto cfg = parse_proc_config(proc_text());
    REQUIRE(cfg.size() == 240);
    CHECK(cfg[0] == 0x63);
    CHECK(cfg[kTouchLevel] == 100);
    CHECK(cfg[kLeaveLevel] == 70);
    CHECK(cfg[kShake] == 1);
    CHECK(parse_proc_config("no config here").empty());
}

TEST_CASE("K2oh touch: written config has new levels, checksum and fresh flag", "[k2oh][touch]") {
    const auto cfg = parse_proc_config(proc_text());
    const auto out = build_config(cfg, 75, 50, 3);
    REQUIRE(out.size() == kConfigLen);
    CHECK(out[kTouchLevel] == 75);
    CHECK(out[kLeaveLevel] == 50);
    CHECK(out[kShake] == 3);
    CHECK(out[0] == 0x63); // version kept: the chip takes an equal version
    uint8_t sum = 0;
    for (size_t i = 0; i < 185; ++i) {
        sum = static_cast<uint8_t>(sum + out[i]); // bytes 0..183 plus checksum wrap to 0
    }
    CHECK(sum == 0);
    CHECK(out[185] == 1);
    CHECK(build_config(std::vector<uint8_t>(100), 75, 50, 3).empty());
}
