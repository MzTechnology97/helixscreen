// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ams_sidebar_gating_lifetime.cpp
 * @brief The sidebar's Unload gating subject outlives a printer switch.
 *
 * It is created from the AMS panels' process-once XML registration, so nothing
 * creates it again after a switch; the switch resets its value instead.
 */

#include "ui_ams_sidebar.h"

#include "../lvgl_ui_test_fixture.h"
#include "helix-xml/src/xml/lv_xml.h"
#include "static_subject_registry.h"

#include <lvgl.h>

#include "../catch_amalgamated.hpp"

TEST_CASE_METHOD(LVGLUITestFixture,
                 "AMS sidebar Unload gating survives a printer switch at its default",
                 "[ams][sidebar][gating_lifetime]") {
    helix::ui::AmsOperationSidebar::register_callbacks_static();
    lv_subject_t* unload_disabled = lv_xml_get_subject(nullptr, "ams_sidebar_unload_disabled");
    REQUIRE(unload_disabled != nullptr);
    lv_subject_set_int(unload_disabled, 0);

    // A switch runs every entry with its PrinterSwitch disposition. The
    // subject itself stays registered for the next switch and for exit.
    auto& registry = StaticSubjectRegistry::instance();
    using Scope = StaticSubjectRegistry::Scope;
    REQUIRE(registry.deinit_one("AmsSidebarUnloadGating", Scope::PrinterSwitch));
    CHECK(registry.deinit_one("AmsSidebarUnloadGating", Scope::PrinterSwitch));
    CHECK(lv_xml_get_subject(nullptr, "ams_sidebar_unload_disabled") == unload_disabled);

    // The switch puts it back to the value nothing has refreshed yet: disabled.
    REQUIRE(registry.deinit_one("AmsSidebarUnloadGatingReset", Scope::PrinterSwitch));
    CHECK(lv_subject_get_int(unload_disabled) == 1);
    CHECK(registry.deinit_one("AmsSidebarUnloadGatingReset", Scope::PrinterSwitch));
}
