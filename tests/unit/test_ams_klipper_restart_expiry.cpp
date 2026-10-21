// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

// A Klipper restart ends every firmware-side filament operation, so a backend
// still reporting one busy when Klipper comes back ready would refuse every
// tool change for nothing. AmsState ends it once, for every backend, on the
// klippy_state transition into READY.

#include "ui_update_queue.h"

#include "../lvgl_test_fixture.h"
#include "../test_helpers/ad5x_ifs_test_access.h"
#include "../test_helpers/afc_test_access.h"
#include "../test_helpers/cfs_test_access.h"
#include "../test_helpers/toolchanger_test_access.h"
#include "../test_helpers/update_queue_test_access.h"
#include "ams_backend_ad5x_ifs.h"
#include "ams_backend_afc.h"
#include "ams_backend_cfs.h"
#include "ams_backend_mock.h"
#include "ams_backend_toolchanger.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "printer_state.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

class RestartRecordingBackend : public AmsBackendMock {
  public:
    RestartRecordingBackend() : AmsBackendMock(4) {}

    int restarts = 0;
    void on_klipper_restarted() override {
        ++restarts;
    }
};

class KlipperRestartFixture : public LVGLTestFixture {
  public:
    KlipperRestartFixture() {
        get_printer_state().init_subjects(false);
        get_printer_state().set_klippy_state_sync(KlippyState::READY);
        AmsState::instance().init_subjects(true);
    }

    ~KlipperRestartFixture() override {
        AmsState::instance().set_backend(nullptr);
        get_printer_state().set_klippy_state_sync(KlippyState::READY);
        settle();
    }

    void settle() {
        helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    }

    void restart_klipper() {
        get_printer_state().set_klippy_state_sync(KlippyState::STARTUP);
        get_printer_state().set_klippy_state_sync(KlippyState::READY);
        settle();
    }
};

/// The app's own start: the klippy_state subject holds its SHUTDOWN default
/// until Moonraker reports, so the first READY is a connect.
class KlipperBootFixture : public LVGLTestFixture {
  public:
    KlipperBootFixture() {
        get_printer_state().init_subjects(false);
        get_printer_state().set_klippy_state_sync(KlippyState::SHUTDOWN);
        AmsState::instance().init_subjects(true);
        settle();
    }

    ~KlipperBootFixture() override {
        AmsState::instance().set_backend(nullptr);
        get_printer_state().set_klippy_state_sync(KlippyState::READY);
        settle();
    }

    void settle() {
        helix::ui::UpdateQueueTestAccess::drain_all(helix::ui::UpdateQueue::instance());
    }
};

/// A tool changer whose macro acks only when the test says so, so a dispatch
/// stays armed across a restart.
class HeldAckToolChanger : public AmsBackendToolChanger {
  public:
    HeldAckToolChanger() : AmsBackendToolChanger(nullptr, nullptr) {}

    AmsError execute_gcode(const std::string& /*gcode*/) override {
        return AmsErrorHelper::success();
    }
    AmsError execute_gcode(const std::string& /*gcode*/,
                           std::function<void()> on_complete) override {
        held_ack = std::move(on_complete);
        return AmsErrorHelper::success();
    }
    bool toolhead_homed() const override {
        return true;
    }

    std::function<void()> held_ack;
};

} // namespace

TEST_CASE_METHOD(KlipperRestartFixture, "Klipper coming back ready ends the backends' operations",
                 "[ams][klipper_restart_expiry]") {
    auto owned = std::make_unique<RestartRecordingBackend>();
    auto* backend = owned.get();
    AmsState::instance().set_backend(std::move(owned));

    SECTION("a restart: STARTUP, then READY") {
        restart_klipper();
        CHECK(backend->restarts == 1);
    }
    SECTION("a shutdown recovered to READY") {
        get_printer_state().set_klippy_state_sync(KlippyState::SHUTDOWN);
        get_printer_state().set_klippy_state_sync(KlippyState::READY);
        settle();
        CHECK(backend->restarts == 1);
    }
    SECTION("Klipper still starting ends nothing yet") {
        get_printer_state().set_klippy_state_sync(KlippyState::STARTUP);
        settle();
        CHECK(backend->restarts == 0);
    }
    SECTION("READY repeated without leaving it ends nothing") {
        get_printer_state().set_klippy_state_sync(KlippyState::READY);
        settle();
        CHECK(backend->restarts == 0);
    }
}

TEST_CASE_METHOD(KlipperRestartFixture, "A deinit and re-init leave exactly one restart observer",
                 "[ams][klipper_restart_expiry]") {
    // The printer-switch cycle. Klipper is READY and the observer has seen it.
    AmsState::instance().deinit_subjects();

    // Deinitialized: a restart edge reaches nothing.
    auto first = std::make_unique<RestartRecordingBackend>();
    auto* stale = first.get();
    AmsState::instance().set_backend(std::move(first));
    restart_klipper();
    CHECK(stale->restarts == 0);

    AmsState::instance().init_subjects(true);
    settle();
    auto second = std::make_unique<RestartRecordingBackend>();
    auto* backend = second.get();
    AmsState::instance().set_backend(std::move(second));

    restart_klipper();
    CHECK(backend->restarts == 1);
}

TEST_CASE_METHOD(KlipperRestartFixture, "AFC ends a busy operation on a Klipper restart",
                 "[ams][afc][klipper_restart_expiry]") {
    AmsBackendAfc backend(nullptr, nullptr);
    AfcTestAccess::system_info(backend).action = AmsAction::LOADING;

    backend.on_klipper_restarted();

    CHECK(backend.get_current_action() == AmsAction::IDLE);
}

TEST_CASE_METHOD(KlipperRestartFixture,
                 "AD5X IFS ends its operation and phase tracking on a restart",
                 "[ams][ad5x][klipper_restart_expiry]") {
    AmsBackendAd5xIfs backend(nullptr, nullptr);
    Ad5xIfsTestAccess::begin_phase(backend, /*is_unload=*/false);
    REQUIRE(Ad5xIfsTestAccess::phase_active(backend));

    backend.on_klipper_restarted();

    CHECK(Ad5xIfsTestAccess::action(backend) == AmsAction::IDLE);
    CHECK_FALSE(Ad5xIfsTestAccess::phase_active(backend));
}

TEST_CASE_METHOD(KlipperRestartFixture, "CFS ends its operation and phase tracking on a restart",
                 "[ams][cfs][klipper_restart_expiry]") {
    helix::printer::AmsBackendCfs backend(nullptr, nullptr);
    CfsTestAccess::force_phase_intent(backend, AmsAction::LOADING);
    REQUIRE(CfsTestAccess::phase_active(backend));

    backend.on_klipper_restarted();

    CHECK(backend.get_current_action() == AmsAction::IDLE);
    CHECK_FALSE(CfsTestAccess::phase_active(backend));
}

TEST_CASE_METHOD(KlipperBootFixture, "The first READY after the app starts is a connect",
                 "[ams][klipper_restart_expiry]") {
    auto owned = std::make_unique<RestartRecordingBackend>();
    auto* backend = owned.get();
    AmsState::instance().set_backend(std::move(owned));

    get_printer_state().set_klippy_state_sync(KlippyState::READY);
    settle();
    CHECK(backend->restarts == 0);

    get_printer_state().set_klippy_state_sync(KlippyState::STARTUP);
    get_printer_state().set_klippy_state_sync(KlippyState::READY);
    settle();
    CHECK(backend->restarts == 1);
}

TEST_CASE_METHOD(KlipperRestartFixture, "A new printer's first READY is a connect",
                 "[ams][klipper_restart_expiry]") {
    // The change-host path: the old printer's backends go, Klipper reads as
    // down until the new host reports, and its backends arrive with discovery.
    settle();
    AmsState::instance().clear_backends();
    get_printer_state().set_klippy_state_sync(KlippyState::SHUTDOWN);
    settle();
    auto owned = std::make_unique<RestartRecordingBackend>();
    auto* backend = owned.get();
    AmsState::instance().add_backend(std::move(owned));

    get_printer_state().set_klippy_state_sync(KlippyState::READY);
    settle();
    CHECK(backend->restarts == 0);

    restart_klipper();
    CHECK(backend->restarts == 1);
}

TEST_CASE_METHOD(KlipperRestartFixture,
                 "A tool changer forgets its dispatched operation on a Klipper restart",
                 "[ams][toolchanger][klipper_restart_expiry]") {
    HeldAckToolChanger backend;
    REQUIRE(ToolChangerTestAccess::call_dispatch_operation(backend, "SELECT_TOOL T=1",
                                                           AmsAction::SELECTING)
                .success());
    REQUIRE(ToolChangerTestAccess::has_pending_dispatch(backend));

    backend.on_klipper_restarted();

    CHECK(backend.get_current_action() == AmsAction::IDLE);
    CHECK_FALSE(ToolChangerTestAccess::has_pending_dispatch(backend));
}
