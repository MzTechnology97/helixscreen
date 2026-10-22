// tests/test_helpers/registered_backend.h
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An AmsBackend owned by AmsState, for tests that drive code reaching the
// backend through AmsState::instance().
#pragma once

#include "ui_update_queue.h"

#include "ams_backend.h"
#include "ams_state.h"

#include <memory>
#include <utility>

namespace helix::test {

/// Owns a backend through AmsState, then hands it back for driving.
/// set_backend() clears first, so a harness always starts as backend 0.
///
/// Construct it with the backend's own constructor arguments:
///
///     RegisteredBackend<AmsBackendAfcTestHelper> harness;
///     harness->feed_afc_stepper("lane1", {{"map", "T0"}});
template <typename BackendT> class RegisteredBackend {
  public:
    template <typename... Args> explicit RegisteredBackend(Args&&... args) {
        auto owned = std::make_unique<BackendT>(std::forward<Args>(args)...);
        backend_ = owned.get();
        ::AmsState::instance().set_backend(std::move(owned));
    }

    ~RegisteredBackend() {
        // Drain ahead of clear_backends(): callbacks queued by driving the
        // backend resolve it through get_backend(0), which answers nullptr once
        // the backends are gone.
        helix::ui::UpdateQueue::instance().drain();
        ::AmsState::instance().clear_backends();
    }

    RegisteredBackend(const RegisteredBackend&) = delete;
    RegisteredBackend& operator=(const RegisteredBackend&) = delete;

    BackendT& operator*() const {
        return *backend_;
    }

    BackendT* operator->() const {
        return backend_;
    }

  private:
    BackendT* backend_;
};

} // namespace helix::test
