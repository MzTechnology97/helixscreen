// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Which shutdown dialog opens.
 *
 * A screen on the printer's own host gets one row of buttons that go through
 * Moonraker, or straight to local power when Moonraker is down or the host
 * does not run systemd (Moonraker's own power path needs it). A screen on a
 * different host gets the Both / Printer / Screen split buttons.
 */

#include "../../src/ui/panel_widgets/shutdown_widget.h"

#include "../catch_amalgamated.hpp"

using namespace helix;

TEST_CASE("Shutdown dialog plan: a remote printer splits whatever this host runs",
          "[shutdown][power]") {
    for (const bool connected : {true, false}) {
        for (const bool systemd : {true, false}) {
            CHECK(plan_shutdown_dialog(/*configured_host_same=*/false, connected, systemd) ==
                  ShutdownDialogMode::Dual);
        }
    }
}

TEST_CASE("Shutdown dialog plan: same host stays single-scope", "[shutdown][power]") {
    SECTION("connected on a systemd host goes through Moonraker") {
        CHECK(plan_shutdown_dialog(/*configured_host_same=*/true, /*api_connected=*/true,
                                   /*this_host_runs_systemd=*/true) ==
              ShutdownDialogMode::SingleViaMoonraker);
    }

    SECTION("connected on a non-systemd host goes straight to local power") {
        CHECK(plan_shutdown_dialog(/*configured_host_same=*/true, /*api_connected=*/true,
                                   /*this_host_runs_systemd=*/false) ==
              ShutdownDialogMode::SingleViaLocalPower);
    }

    SECTION("disconnected on a non-systemd host uses local power too") {
        CHECK(plan_shutdown_dialog(/*configured_host_same=*/true, /*api_connected=*/false,
                                   /*this_host_runs_systemd=*/false) ==
              ShutdownDialogMode::SingleViaLocalPower);
    }

    SECTION("disconnected falls back to local power") {
        CHECK(plan_shutdown_dialog(/*configured_host_same=*/true, /*api_connected=*/false,
                                   /*this_host_runs_systemd=*/true) ==
              ShutdownDialogMode::SingleViaLocalPower);
    }
}
