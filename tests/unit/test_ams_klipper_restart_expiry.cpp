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
#include "../test_helpers/update_queue_test_access.h"
#include "ams_backend_ad5x_ifs.h"
#include "ams_backend_afc.h"
#include "ams_backend_cfs.h"
#include "ams_backend_mock.h"
#include "ams_state.h"
#include "ams_types.h"
#include "app_globals.h"
#include "printer_state.h"

#include <memory>
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
