/*
 * Copyright (C) 2024 LibreMobileOS Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraProviderExtension.h"

#include <algorithm> // for std::clamp
#include <atomic>
#include <fstream>
#include <string>
#include <vector>

// === CONFIGURATION ===
namespace {
    constexpr char kSwitchPath[] = "/sys/class/leds/led:switch_0/brightness";
    const std::vector<std::string> kTorchLedPaths = {
        "/sys/class/leds/led:torch_0/brightness",
        "/sys/class/leds/led:torch_1/brightness"
    };

    // Constants for brightness logic
    constexpr int32_t kMaxTotalStrength = 1000;
    constexpr int32_t kDefaultStrength = 300;
    constexpr size_t kLedCount = 2; // torch_0 and torch_1

    // State Tracking
    std::atomic<bool> gTorchEnabled{false};
    std::atomic<int32_t> gLastStrength{kDefaultStrength};
} // namespace

// === I/O Helpers ===
template <typename T>
static bool writeSysfs(const std::string& path, const T& value) {
    std::ofstream file(path);
    if (file.is_open()) {
        file << value;
        return true;
    }
    return false;
}

template <typename T>
static T readSysfs(const std::string& path, const T& defaultValue) {
    std::ifstream file(path);
    T result;
    if (file >> result) {
        return result;
    }
    return defaultValue;
}

// === HAL Interface ===

bool supportsTorchStrengthControlExt() {
    return true;
}

bool supportsSetTorchModeExt() {
    return true;
}

int32_t getTorchDefaultStrengthLevelExt() {
    return kDefaultStrength;
}

int32_t getTorchMaxStrengthLevelExt() {
    return kMaxTotalStrength;
}

int32_t getTorchStrengthLevelExt() {
    // Read from the first LED. 
    // LOGIC FIX: Since 'set' divides the total by 2, we must multiply by 2 
    // here to return the correct total strength context.
    int32_t singleLedBrightness = readSysfs(kTorchLedPaths[0], 0);
    return singleLedBrightness * kLedCount;
}

void setTorchStrengthLevelExt(int32_t torchStrength, bool enabled) {
    if (enabled) {
        // 1. Store the requested total strength for later toggling
        gLastStrength.store(torchStrength);

        // 2. Calculate per-LED strength
        int32_t clampedStrength = std::clamp(torchStrength, 0, kMaxTotalStrength);
        int32_t perLed = clampedStrength / kLedCount;

        // 3. Apply to all LEDs
        for (const auto& path : kTorchLedPaths) {
            writeSysfs(path, perLed);
        }

        // 4. Activate the main switch if currently off
        bool wasEnabled = gTorchEnabled.exchange(true);
        if (!wasEnabled) {
            writeSysfs(kSwitchPath, 1);
        }
    } else {
        // 1. Disable the main switch
        writeSysfs(kSwitchPath, 0);
        gTorchEnabled.store(false);

        // 2. Reset LEDs to 0 (Good practice for safety)
        for (const auto& path : kTorchLedPaths) {
            writeSysfs(path, 0);
        }
    }
}

void setTorchModeExt(bool enabled) {
    // Restore the last known strength if enabling, otherwise pass 0
    int32_t targetStrength = enabled ? gLastStrength.load() : 0;
    setTorchStrengthLevelExt(targetStrength, enabled);
}
