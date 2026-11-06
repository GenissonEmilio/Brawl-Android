// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <cstdint>

namespace BrawlAndroid {

struct TouchPadSample {
    uint16_t buttons = 0;
    std::array<int8_t, 4> axes{};
};

// Production UI-to-PAD state, independent of Android/JNI. The bridge serializes calls with
// its mutex. Each rising button edge survives an ordinary UP until exactly one PAD poll.
// Cancel is different from UP: no pending edge may survive a cancelled gesture/session.
class TouchPadState {
public:
    void Set(uint16_t buttons, std::array<int8_t, 4> axes, bool enabled) {
        enabled_ = enabled;
        if (!enabled) {
            Cancel();
            return;
        }
        pendingPress_ |= static_cast<uint16_t>(buttons & ~held_);
        // Startup is a separate consumer: PAD polls can run behind the title
        // cover and must not swallow a short A/START tap before it is ready.
        pendingStartupPress_ |= static_cast<uint16_t>(buttons & ~held_ & kStartupButtons);
        startupSuppressed_ &= buttons;
        held_ = buttons;
        axes_ = axes;
    }

    void Cancel() {
        held_ = 0;
        pendingPress_ = 0;
        pendingStartupPress_ = 0;
        startupSuppressed_ = 0;
        axes_ = {};
        // A cancelled gesture does not connect/disconnect a controller.
    }

    bool Sample(unsigned port, TouchPadSample& out) {
        if (port != 0 || !enabled_) return false;
        out.buttons = static_cast<uint16_t>((held_ | pendingPress_) & ~startupSuppressed_);
        out.axes = axes_;
        pendingPress_ = 0;
        return true;
    }

    bool ConsumeStartupPress(bool titleReady) {
        if (!titleReady || !enabled_ || pendingStartupPress_ == 0) return false;
        const uint16_t accepted = pendingStartupPress_;
        pendingStartupPress_ = 0;
        pendingPress_ &= static_cast<uint16_t>(~accepted);
        // The press dismisses the host cover. Holding it must not also advance
        // the game's first dialog; an ordinary release/new press restores PAD.
        startupSuppressed_ |= static_cast<uint16_t>(held_ & accepted);
        return true;
    }

private:
    static constexpr uint16_t kStartupButtons = 0x0100u | 0x1000u; // GameCube A / START
    bool enabled_ = false;
    uint16_t held_ = 0;
    uint16_t pendingPress_ = 0;
    uint16_t pendingStartupPress_ = 0;
    uint16_t startupSuppressed_ = 0;
    std::array<int8_t, 4> axes_{};
};

} // namespace BrawlAndroid
