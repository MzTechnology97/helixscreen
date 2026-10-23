// Copyright (C) 2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * host_runs_systemd() reads the /run/systemd/system marker under a root, the
 * way sd_booted() does. A systemctl binary proves nothing: chroots, containers
 * and SysV boxes ship one, and SystemPower must not trust it there.
 */

#include "system_power.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "../catch_amalgamated.hpp"

namespace fs = std::filesystem;

namespace {

class ScratchRoot {
  public:
    ScratchRoot() {
        const char* base = std::getenv("TMPDIR");
        std::string templ = std::string(base ? base : "/tmp") + "/host_runs_systemd_XXXXXX";
        std::vector<char> buf(templ.begin(), templ.end());
        buf.push_back('\0');
        if (::mkdtemp(buf.data()) == nullptr) {
            FAIL("mkdtemp failed");
        }
        path_ = buf.data();
    }
    ~ScratchRoot() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const std::string& path() const {
        return path_;
    }

  private:
    std::string path_;
};

} // namespace

TEST_CASE("host_runs_systemd reads the /run/systemd/system marker", "[power]") {
    ScratchRoot root;

    SECTION("no marker reads as not systemd") {
        CHECK_FALSE(helix::host_runs_systemd(root.path()));
    }

    SECTION("the marker directory reads as systemd") {
        fs::create_directories(root.path() + "/run/systemd/system");
        CHECK(helix::host_runs_systemd(root.path()));
    }

    SECTION("a systemctl binary alone is not systemd") {
        fs::create_directories(root.path() + "/bin");
        std::ofstream(root.path() + "/bin/systemctl") << "#!/bin/sh\nexit 0\n";
        fs::permissions(root.path() + "/bin/systemctl", fs::perms::owner_all);
        REQUIRE(fs::exists(root.path() + "/bin/systemctl"));
        CHECK_FALSE(helix::host_runs_systemd(root.path()));
    }

    SECTION("a plain file where the marker directory belongs is not systemd") {
        fs::create_directories(root.path() + "/run/systemd");
        std::ofstream(root.path() + "/run/systemd/system") << "";
        CHECK_FALSE(helix::host_runs_systemd(root.path()));
    }
}
