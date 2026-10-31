// SPDX-License-Identifier: GPL-3.0-or-later
// K2-OpenHost CFS: the slot a print swaps to when the loaded spool runs out.

#include "ui_k2oh_cfs_view.h"

#include "ams_types.h"

#include "../catch_amalgamated.hpp"
#include "hv/json.hpp"

using helix::ui::K2ohCfsView;

namespace {

// Four PETG spools: 1, 2 and 4 back each other up, 3 stands alone.
AmsSystemInfo k2_box(float s1, float s2, float s4) {
    AmsSystemInfo info;
    AmsUnit unit;
    const float remaining[] = {s1, 0.5f, 0.3f, s4};
    for (int i = 0; i < 4; ++i) {
        SlotInfo slot;
        slot.global_index = i;
        slot.material = "PETG";
        slot.remaining_fraction = i == 1 ? s2 : remaining[i];
        unit.slots.push_back(slot);
    }
    info.units.push_back(unit);
    info.endless_spool_group_ids = {0, 0, -1, 0};
    return info;
}

} // namespace

TEST_CASE("K2oh CFS: runout swaps to the lowest remaining spool of the group", "[k2oh][cfs]") {
    const auto info = k2_box(0.85f, 1.0f, 0.09f);
    CHECK(K2ohCfsView::runout_next_slot(info, 3) == 0); // 4 (9%) » 1 (85%) » 2 (100%)
    CHECK(K2ohCfsView::runout_next_slot(info, 1) == 3); // the loaded one first, then the lowest
    CHECK(K2ohCfsView::runout_next_slot(info, 2) == -1); // no group
    CHECK(K2ohCfsView::runout_next_slot(info, -1) == -1); // nothing loaded
}

TEST_CASE("K2oh CFS: a stopped change says what happened and what to do", "[k2oh][cfs]") {
    const auto slots = nlohmann::json::parse(R"([{"index": 2, "external": false},
                                                {"index": 4, "external": true}])");
    auto recovery = nlohmann::json::parse(
        R"({"blocked": false, "automatic": false, "target": null, "step": null, "reason": null})");
    CHECK(K2ohCfsView::recovery_summary(recovery, slots).empty());
    CHECK(K2ohCfsView::recovery_summary(nlohmann::json(), slots).empty());

    recovery["blocked"] = true;
    CHECK(K2ohCfsView::recovery_summary(recovery, slots) ==
          "CFS change incomplete · Load a slot from the CFS page, then Resume");

    recovery["automatic"] = true;
    recovery["target"] = 2;
    recovery["reason"] = "Filament did not reach the head";
    CHECK(K2ohCfsView::recovery_summary(recovery, slots) ==
          "Filament did not reach the head · Resume retries slot 3");

    recovery["target"] = 4;
    CHECK(K2ohCfsView::recovery_summary(recovery, slots) ==
          "Filament did not reach the head · Resume retries the external spool");

    recovery["automatic"] = false; // a target without automatic retry: the user picks
    CHECK(K2ohCfsView::recovery_summary(recovery, slots) ==
          "Filament did not reach the head · Load a slot from the CFS page, then Resume");
}
