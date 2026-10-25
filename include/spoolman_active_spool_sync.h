// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "i_moonraker_sub_apis.h"
#include "spoolman_types.h"

#include <functional>
#include <string>

#include "hv/json.hpp"

/// Startup sync of the external spool slot from Moonraker's active Spoolman spool, so
/// the filament panel shows the right spool even when it was changed from Spoolman's
/// web UI or another client.
namespace helix::spoolman_sync {

/// Applies a fetched spool to the external slot. Called from the fetch reply.
using ApplySpoolFn = std::function<void(const SpoolInfo& spool, std::string log_context)>;

/// Asks Moonraker for its Spoolman status and hands the reply to sync_from_status().
void request_startup_sync(ISpoolmanAPI& spoolman, ApplySpoolFn apply);

/// The startup sync, given Moonraker's Spoolman status. No-op when Spoolman is down,
/// no spool is active, or the external slot already holds that spool; otherwise
/// fetches the spool and applies it. Main thread only: it reads AmsState.
void sync_from_status(ISpoolmanAPI& spoolman, bool connected, int active_spool_id,
                      const ApplySpoolFn& apply);

/// The notify_spoolman_status_changed handler: sets Spoolman availability, and on a
/// connect reruns the startup sync. @p msg is the full JSON-RPC message; one without
/// a boolean `spoolman_connected` is ignored. Callable from any thread.
void on_status_notification(ISpoolmanAPI& spoolman, const nlohmann::json& msg, ApplySpoolFn apply);

} // namespace helix::spoolman_sync
