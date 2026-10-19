// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_canvas_setter_ceilings.cpp
 * @brief Canvas setters hold an over-range value at their ceiling.
 *
 * The filament path canvas sizes its per-slot arrays by AMS_MAX_SLOTS, so a
 * slot count past it must stop at the ceiling rather than index past the end.
 */

#include "ui_filament_path_canvas.h"
#include "ui_spool_canvas.h"

#include "../lvgl_ui_test_fixture.h"
#include "ams_types.h"
#include "src/ui/ui_filament_path_internal.h"

#include <string>

#include "../catch_amalgamated.hpp"

using helix::AMS_MAX_SLOTS;
using helix::ui::fpath::get_data;

TEST_CASE_METHOD(LVGLUITestFixture, "FilamentPath: the canvas takes every lane up to the ceiling",
                 "[ams][slot_ceiling][filament_path]") {
    SECTION("set from C++") {
        lv_obj_t* path = ui_filament_path_canvas_create(test_screen());
        REQUIRE(path != nullptr);
        ui_filament_path_canvas_set_slot_count(path, AMS_MAX_SLOTS);
        CHECK(get_data(path)->slot_count == AMS_MAX_SLOTS);
        ui_filament_path_canvas_set_slot_count(path, AMS_MAX_SLOTS + 1);
        CHECK(get_data(path)->slot_count == AMS_MAX_SLOTS);
        lv_obj_delete(path);
    }
    SECTION("set from XML") {
        ui_filament_path_canvas_register();
        const std::string count = std::to_string(AMS_MAX_SLOTS + 1);
        const char* attrs[] = {"slot_count", count.c_str(), nullptr};
        auto* path =
            static_cast<lv_obj_t*>(lv_xml_create(test_screen(), "filament_path_canvas", attrs));
        REQUIRE(path != nullptr);
        CHECK(get_data(path)->slot_count == AMS_MAX_SLOTS);
        lv_obj_delete(path);
    }
}

TEST_CASE_METHOD(LVGLUITestFixture, "FilamentPath: a segment past the nozzle stops at the nozzle",
                 "[ams][filament_path]") {
    lv_obj_t* path = ui_filament_path_canvas_create(test_screen());
    REQUIRE(path != nullptr);
    ui_filament_path_canvas_set_filament_segment(path, PATH_SEGMENT_COUNT + 3);
    CHECK(get_data(path)->filament_segment == PATH_SEGMENT_COUNT - 1);
    lv_obj_delete(path);
}

TEST_CASE_METHOD(LVGLUITestFixture, "SpoolCanvas: a fill level over full reads as full",
                 "[ams][spool_canvas]") {
    lv_obj_t* spool = ui_spool_canvas_create(test_screen(), 64);
    REQUIRE(spool != nullptr);
    ui_spool_canvas_set_fill_level(spool, 1.5f);
    CHECK(ui_spool_canvas_get_fill_level(spool) == 1.0f);
    lv_obj_delete(spool);
}
