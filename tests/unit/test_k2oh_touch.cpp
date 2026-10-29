// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost Touch & Input: GT911 config parsing and the bytes written.

#include "ui_k2oh_touch.h"

#include "../catch_amalgamated.hpp"

using namespace helix::ui::k2oh_touch;

namespace {

// Head of the K2 Pro's GT911 config (2026-10-29), padded to 186 bytes.
std::vector<uint8_t> k2_config() {
    std::vector<uint8_t> cfg = {0x63, 0xE0, 0x01, 0x20, 0x03, 0x02, 0x0D, 0x00,
                                0x01, 0x08, 0x3C, 0x0F, 0x64, 0x46, 0x03, 0x05};
    cfg.resize(kConfigLen);
    return cfg;
}

} // namespace

TEST_CASE("K2oh touch: written config has new levels, checksum and fresh flag", "[k2oh][touch]") {
    const auto out = build_config(k2_config(), 75, 50, 3);
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
