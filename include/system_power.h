// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace helix {

/// True when systemd is the running init under @p root ("" = this host),
/// read from the /run/systemd/system marker the way sd_booted() reads it. A
/// systemctl binary proves nothing: chroots, containers and SysV boxes ship one.
bool host_runs_systemd(const std::string& root = {});

/// Initiates shutdown/reboot of the machine running helixscreen. Tries logind
/// via sd-bus first; falls back to systemctl if sd-bus is unavailable or the
/// call fails. Returns true if one of the mechanisms dispatched successfully.
class SystemPower {
  public:
    static bool reboot_local();
    static bool shutdown_local();
};

} // namespace helix
