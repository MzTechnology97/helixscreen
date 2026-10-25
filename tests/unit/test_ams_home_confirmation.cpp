// SPDX-License-Identifier: GPL-3.0-or-later
#include "../lvgl_test_fixture.h"
#include "../lvgl_ui_test_fixture.h"
#include "ams_backend_afc.h"
#include "ams_backend_cfs.h"
#include "ams_backend_toolchanger.h"
#include "app_globals.h"
#include "filament_op_router.h"
#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"
#include "standard_macros.h"
#include "test_helpers/cfs_test_access.h"
#include "test_helpers/filament_panel_harness.h"
#include "test_helpers/filament_panel_test_access.h"
#include "test_helpers/lane_material_backend.h"
#include "test_helpers/printer_state_test_access.h"
#include "test_helpers/toolchanger_test_access.h"
#include "test_helpers/update_queue_test_access.h"

#include <algorithm>
#include <string>

#include "../catch_amalgamated.hpp"

namespace {

/// Captures dispatched gcode and lets a test drive the homed answer.
/// Overrides BOTH execute_gcode forms so neither falls through to the base.
///
/// fail_next_gcode simulates a command failure with no live api_ to carry a
/// real async MoonrakerError: with api_ null, ensure_homed_then()'s only
/// failure signal for the (fixture-driven) G28/payload dispatch is the
/// AmsError these overrides return, which it translates into the
/// MoonrakerError passed to on_error. Deliberately NOT a stored
/// std::function<void(const MoonrakerError&)> callback invoked directly by
/// the override -- the override has no way to receive ensure_homed_then's
/// on_error as a parameter (the 1-arg/2-arg execute_gcode virtuals predate
/// on_error and can't grow it without breaking ~20 other fixtures), so the
/// return-value channel is what's actually reachable here.
class HomingProbeBackend : public AmsBackendAfc {
  public:
    HomingProbeBackend() : AmsBackendAfc(nullptr, nullptr) {}

    AmsError execute_gcode(const std::string& gcode) override {
        if (fail_next_gcode) {
            return AmsError(AmsResult::COMMAND_FAILED, "boom", "boom");
        }
        captured.push_back(gcode);
        return AmsErrorHelper::success();
    }
    AmsError execute_gcode(const std::string& gcode, std::function<void()> on_complete) override {
        if (fail_next_gcode) {
            return AmsError(AmsResult::COMMAND_FAILED, "boom", "boom");
        }
        captured.push_back(gcode);
        if (on_complete) {
            on_complete();
        }
        return AmsErrorHelper::success();
    }
    bool toolhead_homed() const override {
        return homed;
    }

    bool homed = true;
    bool fail_next_gcode = false;
    std::vector<std::string> captured;
};

} // namespace

TEST_CASE("ensure_homed_then dispatches directly when already homed", "[ams][homing]") {
    LVGLTestFixture fixture;
    HomingProbeBackend backend;
    backend.homed = true;

    backend.ensure_homed_then("CHANGE_TOOL LANE=lane1");

    REQUIRE(backend.captured.size() == 1);
    CHECK(backend.captured[0] == "CHANGE_TOOL LANE=lane1");
}

TEST_CASE("ensure_homed_then sends G28 before the payload when unhomed", "[ams][homing]") {
    LVGLTestFixture fixture;
    HomingProbeBackend backend;
    backend.homed = false;

    backend.ensure_homed_then("CHANGE_TOOL LANE=lane1");

    REQUIRE(backend.captured.size() == 2);
    CHECK(backend.captured[0] == "G28");
    CHECK(backend.captured[1] == "CHANGE_TOOL LANE=lane1");
}

TEST_CASE("ensure_homed_then skip_homing bypasses the home entirely", "[ams][homing]") {
    LVGLTestFixture fixture;
    HomingProbeBackend backend;
    backend.homed = false;

    backend.ensure_homed_then("BOX_LOAD", nullptr, nullptr, MoonrakerAPI::AMS_OPERATION_TIMEOUT_MS,
                              /*skip_homing=*/true);

    REQUIRE(backend.captured.size() == 1);
    CHECK(backend.captured[0] == "BOX_LOAD");
}

TEST_CASE("ensure_homed_then reports G28 failure through on_error", "[ams][homing]") {
    LVGLTestFixture fixture;
    HomingProbeBackend backend;
    backend.homed = false;
    backend.fail_next_gcode = true;

    std::string seen;
    backend.ensure_homed_then("CHANGE_TOOL LANE=lane1", nullptr,
                              [&seen](const MoonrakerError& e) { seen = e.message; });

    CHECK(seen == "boom");
    CHECK(backend.captured.empty());
}

// =====================================================================
// A tool changer mount on an unhomed printer
// =====================================================================
// dispatch_operation() stamps SELECTING before it calls ensure_homed_then(),
// so any gap between the two leaves the backend busy with nothing on the way.
// With no confirmation to wait on, the G28 and the swap go out in the same
// call and the operation is never parked.
class ToolChangerHomingProbeBackend : public AmsBackendToolChanger {
  public:
    ToolChangerHomingProbeBackend() : AmsBackendToolChanger(nullptr, nullptr) {}

    AmsError execute_gcode(const std::string& gcode) override {
        captured.push_back(gcode);
        return AmsErrorHelper::success();
    }
    AmsError execute_gcode(const std::string& gcode, std::function<void()> on_complete) override {
        captured.push_back(gcode);
        if (on_complete) {
            on_complete();
        }
        return AmsErrorHelper::success();
    }
    bool toolhead_homed() const override {
        return homed;
    }

    bool homed = true;
    std::vector<std::string> captured;
};

TEST_CASE("an unhomed tool changer mount homes and sends the swap in the same call",
          "[ams][homing][toolchanger]") {
    LVGLTestFixture fixture;
    ToolChangerHomingProbeBackend backend;
    backend.homed = false;

    auto err = ToolChangerTestAccess::call_dispatch_operation(backend, "SELECT_TOOL T=1",
                                                              AmsAction::SELECTING);

    REQUIRE(err.success());
    REQUIRE(backend.captured.size() == 2);
    CHECK(backend.captured[0] == "G28");
    CHECK(backend.captured[1] == "SELECT_TOOL T=1");
    // The macro ack resolves the hold through the main-thread queue.
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    CHECK_FALSE(ToolChangerTestAccess::has_pending_dispatch(backend));
    CHECK(backend.get_system_info().action == AmsAction::IDLE);
}

// =====================================================================
// dispatch_payload's "custom" branch (integration)
// =====================================================================
// Unit-style tests above use HomingProbeBackend, whose api_ is null -- they
// never leave dispatch_payload()'s legacy branch (the hardcoded execute_gcode
// virtuals), so they cannot prove a caller's non-default on_error/timeout_ms/
// silent actually reach MoonrakerAPI::execute_gcode(). MoonrakerAPIMock does
// NOT override execute_gcode() -- it inherits the real implementation and
// round-trips through MoonrakerClientMock, exactly like
// "QIDI Box on_started dispatches printer.objects.query (integration)" in
// test_ams_backend_qidi.cpp. That is the only way to reach the custom branch
// from a test: a live api_/client_ so !api_ doesn't short-circuit it first.
TEST_CASE("ensure_homed_then custom timeout/silent bypass the hardcoded virtuals and reach "
          "MoonrakerAPI::execute_gcode (integration)",
          "[ams][homing][integration]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    helix::PrinterState state;
    state.init_subjects(false);
    MoonrakerAPIMock api(client, state);

    AmsBackendAfc backend(&api, &client);

    constexpr uint32_t CUSTOM_TIMEOUT_MS = 12345;
    REQUIRE(CUSTOM_TIMEOUT_MS != MoonrakerAPI::AMS_OPERATION_TIMEOUT_MS);
    // The mock handler for printer.gcode.script runs synchronously inside
    // send_jsonrpc(), so this fires before ensure_homed_then() even returns --
    // exercising the on_error leg of dispatch_payload()'s custom branch, not
    // just the dispatch itself.
    client.force_next_gcode_error(MoonrakerErrorType::JSON_RPC_ERROR, "boom", "BOX_LOAD");

    std::string seen;
    // skip_homing=true keeps this test on the payload leg, not G28 -- that leg
    // is already covered by the two tests above.
    auto err = backend.ensure_homed_then(
        "BOX_LOAD", nullptr, [&seen](const MoonrakerError& e) { seen = e.message; },
        CUSTOM_TIMEOUT_MS, /*skip_homing=*/true, /*silent=*/false);
    REQUIRE(err.success());

    // Dispatch went straight to MoonrakerAPI::execute_gcode() carrying OUR
    // timeout_ms/silent -- the hardcoded 1-arg/2-arg execute_gcode virtuals fix
    // AMS_OPERATION_TIMEOUT_MS/true and can never produce these values.
    CHECK(client.last_send_method() == "printer.gcode.script");
    CHECK(client.last_send_script() == "BOX_LOAD");
    CHECK(client.last_send_timeout_ms() == CUSTOM_TIMEOUT_MS);
    CHECK_FALSE(client.last_send_silent());

    // The error callback is marshalled through token.defer() (L081 Mechanism
    // C) rather than invoked inline -- drain the queue to run it.
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    CHECK(seen == "boom");
}

// =====================================================================
// AmsBackendCfs::dispatch_action_script (Task 5: collapse the CFS fork)
// =====================================================================
// dispatch_action_script used to hand-duplicate ensure_homed_then's
// query/parse/G28 sequence purely to get on_error plumbing the base lacked.
// It now collapses to a single ensure_homed_then() call with on_error set and
// silent=false -- which always lands on dispatch_payload()'s "custom" branch
// (see the integration test above), so these tests need a live api_/client_
// exactly like that one: a null-api_ probe would short-circuit dispatch_payload
// before it ever reaches MoonrakerAPI::execute_gcode(), and the payload gcode
// would never be recorded.
//
// dispatch_action_script stays private in AmsBackendCfs -- these tests reach
// the REAL production implementation through the ::CfsTestAccess friend shim
// (tests/test_helpers/cfs_test_access.h), not by subclassing to widen access.

TEST_CASE("CFS dispatch_action_script routes through ensure_homed_then and homes when unhomed",
          "[ams][homing][cfs]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    helix::PrinterState state;
    state.init_subjects(false);
    MoonrakerAPIMock api(client, state);

    // homed_axes defaults to "" (not homed) -- exercises the G28-then-payload leg.
    helix::printer::AmsBackendCfs backend(&api, nullptr);

    auto err = CfsTestAccess::call_dispatch_action_script(backend, "BOX_LOAD LANE=1");
    REQUIRE(err.success());

    // G28's success callback is marshalled through token.defer() (L081
    // Mechanism C) before the payload is dispatched -- drain to let it run.
    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());

    REQUIRE(client.gcode_script_history().size() == 2);
    CHECK(client.gcode_script_history()[0] == "G28");
    CHECK(client.gcode_script_history()[1] == "BOX_LOAD LANE=1");

    // The payload send is the last one recorded -- confirms silent=false
    // (CFS's own timeout-toast behaviour) survived the collapse into
    // ensure_homed_then()'s "custom" dispatch_payload() branch.
    CHECK(client.last_send_method() == "printer.gcode.script");
    CHECK(client.last_send_script() == "BOX_LOAD LANE=1");
    CHECK_FALSE(client.last_send_silent());
}

TEST_CASE("CFS Fork variant never homes via dispatch_action_script", "[ams][homing][cfs][fork]") {
    MoonrakerClientMock client(MoonrakerClientMock::PrinterType::VORON_24);
    helix::PrinterState state;
    state.init_subjects(false);
    MoonrakerAPIMock api(client, state);

    helix::printer::AmsBackendCfs backend(&api, nullptr);
    CfsTestAccess::set_macro_variant_fork(backend);

    // homed_axes is STILL "" (not homed) here -- proves the skip comes from
    // skip_homing=true (Fork maps to it), not from the toolhead happening to
    // already be homed.
    auto err = CfsTestAccess::call_dispatch_action_script(backend, "BOX_LOAD LANE=1");
    REQUIRE(err.success());

    helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());

    REQUIRE(client.gcode_script_history().size() == 1);
    CHECK(client.gcode_script_history()[0] == "BOX_LOAD LANE=1");
    CHECK_FALSE(client.last_send_silent());
}

// =====================================================================
// FilamentPanel: an unhomed cold load homes with no prompt
// =====================================================================
// The tier the load takes decides who homes: a backend homes right before its
// own dispatch, the macro tier homes right before the macro, and a raw
// extrude, which moves only E, never homes.

namespace {

using helix::ui::FilamentPanelTestAccess;

/// A load macro with no parameters, so it runs without a parameter dialog.
constexpr const char* PLAIN_LOAD_MACRO = "G1 E50 F300";

/// A lane backend that records the loads it is handed, and moves nothing. @p wants_slot decides
/// whether plan_load() gives it the load or hands the load to a configured macro.
class LoadRecordingLaneBackend : public helix::test::LaneMaterialBackend {
  public:
    explicit LoadRecordingLaneBackend(bool wants_slot)
        : LaneMaterialBackend(/*lane=*/1, /*nozzle_c=*/220), wants_slot_(wants_slot) {}

    AmsError load_filament(int) override {
        ++loads;
        return AmsErrorHelper::success();
    }
    AmsError change_tool(int) override {
        ++loads;
        return AmsErrorHelper::success();
    }
    [[nodiscard]] bool requires_slot_selection_for_load() const override {
        return wants_slot_;
    }

    int loads = 0;

  private:
    bool wants_slot_;
};

/// The process-wide toolhead reads unhomed, which is what ensure_homed_then() asks.
void unhome_process_toolhead() {
    helix::PrinterStateTestAccess::reset(get_printer_state());
    get_printer_state().init_subjects(false);
    get_printer_state().update_from_status({{"toolhead", {{"homed_axes", ""}}}});
}

/// No load macro anywhere, so a load with no backend falls through to raw gcode.
void clear_load_macros() {
    helix::PrinterDiscovery bare;
    bare.parse_objects(nlohmann::json::array({"extruder"}));
    StandardMacros::instance().reset();
    StandardMacros::instance().init(bare);
    REQUIRE(StandardMacros::instance().get(StandardMacroSlot::LoadFilament).is_empty());
}

/// Press Load on the harness's cold, unhomed printer and let the preheat finish.
void press_load_cold(helix::test::FilamentPanelHarness& h) {
    auto& queue = helix::ui::UpdateQueue::instance();
    h.state.update_from_status({{"extruder", {{"temperature", 25.0}}}});
    helix::ui::UpdateQueueTestAccess::drain_all(queue);
    FilamentPanelTestAccess::handle_load_button(*h.panel);

    h.state.update_from_status({{"extruder", {{"temperature", 250.0}}}});
    helix::ui::UpdateQueueTestAccess::drain_all(queue);
    FilamentPanelTestAccess::check_pending_preheat(*h.panel);
    helix::ui::UpdateQueueTestAccess::drain_all(queue);
}

/// Position of the first script sent whose first word is @p word, or -1.
long first_sent(const helix::test::FilamentPanelHarness& h, const std::string& word) {
    const auto& sent = h.client.gcode_script_history();
    for (size_t i = 0; i < sent.size(); ++i) {
        if (sent[i] == word || sent[i].rfind(word + " ", 0) == 0) {
            return static_cast<long>(i);
        }
    }
    return -1;
}

} // namespace

TEST_CASE_METHOD(LVGLUITestFixture,
                 "Filament panel: an unhomed macro load sends G28, then the macro, with no prompt",
                 "[ams][homing][filament]") {
    unhome_process_toolhead();
    helix::test::FilamentPanelHarness h;
    h.cache_macros({{"LOAD_FILAMENT", PLAIN_LOAD_MACRO}});

    press_load_cold(h);

    const long home = first_sent(h, "G28");
    const long macro = first_sent(h, "LOAD_FILAMENT");
    REQUIRE(home >= 0);
    REQUIRE(macro >= 0);
    CHECK(home < macro);
}

TEST_CASE_METHOD(LVGLUITestFixture,
                 "Filament panel: a failed home before a macro load sends no macro",
                 "[ams][homing][filament]") {
    unhome_process_toolhead();
    helix::test::FilamentPanelHarness h;
    h.cache_macros({{"LOAD_FILAMENT", PLAIN_LOAD_MACRO}});
    h.client.force_next_gcode_error(MoonrakerErrorType::JSON_RPC_ERROR, "Must home axis first",
                                    "G28");

    press_load_cold(h);

    REQUIRE(first_sent(h, "G28") >= 0);
    CHECK(first_sent(h, "LOAD_FILAMENT") < 0);
    CHECK_FALSE(FilamentPanelTestAccess::operation_active(*h.panel));
}

TEST_CASE_METHOD(LVGLUITestFixture, "Filament panel: a raw extrude load never homes",
                 "[ams][homing][filament]") {
    unhome_process_toolhead();
    helix::test::FilamentPanelHarness h;
    clear_load_macros();

    press_load_cold(h);

    CHECK(first_sent(h, "G28") < 0);
    const auto& sent = h.client.gcode_script_history();
    std::string history;
    for (const auto& script : sent) {
        history += "[" + script + "]";
    }
    INFO("sent: " << history);
    const std::string raw = helix::ui::filament_load_fallback_gcode();
    const std::string first_move =
        raw.substr(raw.find("G1 "), raw.find('\n', raw.find("G1 ")) - raw.find("G1 "));
    CHECK(std::any_of(sent.begin(), sent.end(), [&first_move](const std::string& s) {
        return s.find(first_move) != std::string::npos;
    }));
}

TEST_CASE_METHOD(LVGLUITestFixture, "Filament panel: only the macro tier is homed by the panel",
                 "[ams][homing][filament]") {
    unhome_process_toolhead();

    SECTION("a macro load homes before the macro") {
        auto owned = std::make_unique<LoadRecordingLaneBackend>(/*wants_slot=*/false);
        LoadRecordingLaneBackend* backend = owned.get();
        helix::test::FilamentPanelHarness h(std::move(owned));
        h.cache_macros({{"LOAD_FILAMENT", PLAIN_LOAD_MACRO}});

        press_load_cold(h);

        REQUIRE(first_sent(h, "LOAD_FILAMENT") >= 0);
        CHECK(first_sent(h, "G28") >= 0);
        CHECK(backend->loads == 0);
    }

    SECTION("a backend load is handed to the backend, which homes itself") {
        auto owned = std::make_unique<LoadRecordingLaneBackend>(/*wants_slot=*/true);
        LoadRecordingLaneBackend* backend = owned.get();
        helix::test::FilamentPanelHarness h(std::move(owned));
        clear_load_macros();

        press_load_cold(h);

        REQUIRE(backend->loads == 1);
        CHECK(first_sent(h, "G28") < 0);
    }
}
