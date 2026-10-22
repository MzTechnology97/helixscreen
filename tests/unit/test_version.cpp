// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helix_version.h"
#include "version.h"

#include "../catch_amalgamated.hpp"

using namespace helix::version;

// ============================================================================
// parse_version() tests
// ============================================================================

TEST_CASE("parse_version() handles valid version strings", "[version][parse]") {
    SECTION("full semver") {
        auto v = parse_version("1.2.3");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 1);
        REQUIRE(v->minor == 2);
        REQUIRE(v->patch == 3);
    }

    SECTION("major only") {
        auto v = parse_version("2");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 2);
        REQUIRE(v->minor == 0);
        REQUIRE(v->patch == 0);
    }

    SECTION("major.minor only") {
        auto v = parse_version("2.5");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 2);
        REQUIRE(v->minor == 5);
        REQUIRE(v->patch == 0);
    }

    SECTION("with v prefix") {
        auto v = parse_version("v1.2.3");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 1);
        REQUIRE(v->minor == 2);
        REQUIRE(v->patch == 3);
    }

    SECTION("with V prefix") {
        auto v = parse_version("V2.0.0");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 2);
        REQUIRE(v->minor == 0);
        REQUIRE(v->patch == 0);
    }

    SECTION("with pre-release suffix") {
        auto v = parse_version("1.0.0-beta");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 1);
        REQUIRE(v->minor == 0);
        REQUIRE(v->patch == 0);
    }

    SECTION("with build metadata") {
        auto v = parse_version("1.0.0+build123");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 1);
        REQUIRE(v->minor == 0);
        REQUIRE(v->patch == 0);
    }

    SECTION("with both pre-release and build") {
        auto v = parse_version("2.1.0-rc1+sha.abc1234");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 2);
        REQUIRE(v->minor == 1);
        REQUIRE(v->patch == 0);
    }

    SECTION("zeros are valid") {
        auto v = parse_version("0.0.0");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 0);
        REQUIRE(v->minor == 0);
        REQUIRE(v->patch == 0);
    }

    SECTION("large version numbers") {
        auto v = parse_version("100.200.300");
        REQUIRE(v.has_value());
        REQUIRE(v->major == 100);
        REQUIRE(v->minor == 200);
        REQUIRE(v->patch == 300);
    }
}

TEST_CASE("parse_version() handles invalid version strings", "[version][parse]") {
    SECTION("empty string") {
        auto v = parse_version("");
        REQUIRE_FALSE(v.has_value());
    }

    SECTION("just letters") {
        auto v = parse_version("abc");
        REQUIRE_FALSE(v.has_value());
    }

    SECTION("just v") {
        auto v = parse_version("v");
        REQUIRE_FALSE(v.has_value());
    }
}

// ============================================================================
// Version comparison tests
// ============================================================================

TEST_CASE("Version comparison operators", "[version][comparison]") {
    SECTION("equality") {
        Version a{1, 2, 3};
        Version b{1, 2, 3};
        REQUIRE(a == b);
        REQUIRE_FALSE(a != b);
    }

    SECTION("inequality - different major") {
        Version a{1, 0, 0};
        Version b{2, 0, 0};
        REQUIRE(a != b);
        REQUIRE_FALSE(a == b);
    }

    SECTION("less than - major") {
        Version a{1, 0, 0};
        Version b{2, 0, 0};
        REQUIRE(a < b);
        REQUIRE_FALSE(b < a);
    }

    SECTION("less than - minor") {
        Version a{1, 1, 0};
        Version b{1, 2, 0};
        REQUIRE(a < b);
        REQUIRE_FALSE(b < a);
    }

    SECTION("less than - patch") {
        Version a{1, 2, 1};
        Version b{1, 2, 2};
        REQUIRE(a < b);
        REQUIRE_FALSE(b < a);
    }

    SECTION("greater than") {
        Version a{2, 0, 0};
        Version b{1, 9, 9};
        REQUIRE(a > b);
        REQUIRE_FALSE(b > a);
    }

    SECTION("less than or equal") {
        Version a{1, 2, 3};
        Version b{1, 2, 3};
        Version c{1, 2, 4};
        REQUIRE(a <= b);
        REQUIRE(a <= c);
        REQUIRE_FALSE(c <= a);
    }

    SECTION("greater than or equal") {
        Version a{1, 2, 3};
        Version b{1, 2, 3};
        Version c{1, 2, 2};
        REQUIRE(a >= b);
        REQUIRE(a >= c);
        REQUIRE_FALSE(c >= a);
    }
}

// ============================================================================
// check_version_constraint() tests
// ============================================================================

TEST_CASE("check_version_constraint() with >= operator", "[version][constraint]") {
    SECTION("exact match") {
        REQUIRE(check_version_constraint(">=2.0.0", "2.0.0"));
    }

    SECTION("higher major version") {
        REQUIRE(check_version_constraint(">=2.0.0", "3.0.0"));
    }

    SECTION("higher minor version") {
        REQUIRE(check_version_constraint(">=2.0.0", "2.1.0"));
    }

    SECTION("higher patch version") {
        REQUIRE(check_version_constraint(">=2.0.0", "2.0.1"));
    }

    SECTION("lower major version fails") {
        REQUIRE_FALSE(check_version_constraint(">=2.0.0", "1.9.9"));
    }

    SECTION("lower minor version fails") {
        REQUIRE_FALSE(check_version_constraint(">=2.1.0", "2.0.9"));
    }

    SECTION("lower patch version fails") {
        REQUIRE_FALSE(check_version_constraint(">=2.0.1", "2.0.0"));
    }
}

TEST_CASE("check_version_constraint() with > operator", "[version][constraint]") {
    SECTION("exact match fails") {
        REQUIRE_FALSE(check_version_constraint(">2.0.0", "2.0.0"));
    }

    SECTION("higher version passes") {
        REQUIRE(check_version_constraint(">1.0.0", "1.0.1"));
        REQUIRE(check_version_constraint(">1.0.0", "1.1.0"));
        REQUIRE(check_version_constraint(">1.0.0", "2.0.0"));
    }

    SECTION("lower version fails") {
        REQUIRE_FALSE(check_version_constraint(">2.0.0", "1.9.9"));
    }
}

TEST_CASE("check_version_constraint() with = operator", "[version][constraint]") {
    SECTION("exact match passes") {
        REQUIRE(check_version_constraint("=2.0.0", "2.0.0"));
    }

    SECTION("different version fails") {
        REQUIRE_FALSE(check_version_constraint("=2.0.0", "2.0.1"));
        REQUIRE_FALSE(check_version_constraint("=2.0.0", "1.9.9"));
    }
}

TEST_CASE("check_version_constraint() with no operator (implicit =)", "[version][constraint]") {
    SECTION("exact match passes") {
        REQUIRE(check_version_constraint("2.0.0", "2.0.0"));
    }

    SECTION("different version fails") {
        REQUIRE_FALSE(check_version_constraint("2.0.0", "2.0.1"));
    }
}

TEST_CASE("check_version_constraint() with < operator", "[version][constraint]") {
    SECTION("lower version passes") {
        REQUIRE(check_version_constraint("<3.0.0", "2.9.9"));
        REQUIRE(check_version_constraint("<2.1.0", "2.0.9"));
    }

    SECTION("exact match fails") {
        REQUIRE_FALSE(check_version_constraint("<2.0.0", "2.0.0"));
    }

    SECTION("higher version fails") {
        REQUIRE_FALSE(check_version_constraint("<2.0.0", "2.0.1"));
    }
}

TEST_CASE("check_version_constraint() with <= operator", "[version][constraint]") {
    SECTION("lower version passes") {
        REQUIRE(check_version_constraint("<=2.5.0", "2.4.9"));
    }

    SECTION("exact match passes") {
        REQUIRE(check_version_constraint("<=2.5.0", "2.5.0"));
    }

    SECTION("higher version fails") {
        REQUIRE_FALSE(check_version_constraint("<=2.5.0", "2.5.1"));
    }
}

TEST_CASE("check_version_constraint() edge cases", "[version][constraint][edge]") {
    SECTION("empty constraint matches anything") {
        REQUIRE(check_version_constraint("", "1.0.0"));
        REQUIRE(check_version_constraint("", "999.0.0"));
    }

    SECTION("constraint with spaces") {
        REQUIRE(check_version_constraint(">= 2.0.0", "2.0.0"));
        REQUIRE(check_version_constraint("  >=2.0.0", "2.1.0"));
    }

    SECTION("version with v prefix") {
        REQUIRE(check_version_constraint(">=2.0.0", "v2.0.0"));
    }

    SECTION("constraint with v prefix") {
        REQUIRE(check_version_constraint(">=v2.0.0", "2.0.0"));
    }

    SECTION("invalid constraint returns false") {
        REQUIRE_FALSE(check_version_constraint(">=", "2.0.0"));
        REQUIRE_FALSE(check_version_constraint(">=abc", "2.0.0"));
    }

    SECTION("invalid version returns false") {
        REQUIRE_FALSE(check_version_constraint(">=2.0.0", ""));
        REQUIRE_FALSE(check_version_constraint(">=2.0.0", "invalid"));
    }
}

// ============================================================================
// to_string() tests
// ============================================================================

TEST_CASE("to_string() formats versions correctly", "[version][to_string]") {
    SECTION("regular version") {
        Version v{1, 2, 3};
        REQUIRE(to_string(v) == "1.2.3");
    }

    SECTION("zeros") {
        Version v{0, 0, 0};
        REQUIRE(to_string(v) == "0.0.0");
    }

    SECTION("large numbers") {
        Version v{10, 20, 30};
        REQUIRE(to_string(v) == "10.20.30");
    }
}

// ============================================================================
// Real-world constraint examples from task spec
// ============================================================================

TEST_CASE("Version constraint examples from spec", "[version][constraint][spec]") {
    // Examples from the task specification table
    SECTION(">=2.0.0 with 2.0.0 -> match") {
        REQUIRE(check_version_constraint(">=2.0.0", "2.0.0"));
    }

    SECTION(">=2.0.0 with 2.1.0 -> match") {
        REQUIRE(check_version_constraint(">=2.0.0", "2.1.0"));
    }

    SECTION(">=2.0.0 with 1.9.0 -> no match") {
        REQUIRE_FALSE(check_version_constraint(">=2.0.0", "1.9.0"));
    }

    SECTION(">1.0.0 with 1.0.1 -> match") {
        REQUIRE(check_version_constraint(">1.0.0", "1.0.1"));
    }

    SECTION("=2.0.0 with 2.0.0 -> match") {
        REQUIRE(check_version_constraint("=2.0.0", "2.0.0"));
    }
}

// ============================================================================
// helix::docs_url() tests
// ============================================================================

TEST_CASE("helix::docs_url() opens the running release line's docs", "[version][docs_url]") {
    REQUIRE(helix::docs_url(1, 1, "guide/") == "https://helixscreen.org/1.1/guide/");
    REQUIRE(helix::docs_url(1, 0, "guide/getting-started/") ==
            "https://helixscreen.org/1.0/guide/getting-started/");
    REQUIRE(helix::docs_url(2, 10, "upgrading/") == "https://helixscreen.org/2.10/upgrading/");

    SECTION("the one-argument form uses this build's major.minor") {
        REQUIRE(helix::docs_url("guide/") ==
                helix::docs_url(HELIX_VERSION_MAJOR, HELIX_VERSION_MINOR, "guide/"));
    }
}

TEST_CASE("helix::docs_url_text() drops the scheme and trailing slash", "[version][docs_url]") {
    REQUIRE(helix::docs_url_text("https://helixscreen.org/1.1/guide/") ==
            "helixscreen.org/1.1/guide");
    REQUIRE(helix::docs_url_text("https://helixscreen.org/1.1/guide") ==
            "helixscreen.org/1.1/guide");
    REQUIRE(helix::docs_url_text("helixscreen.org/x/") == "helixscreen.org/x");
    REQUIRE(helix::docs_url_text("") == "");
}
