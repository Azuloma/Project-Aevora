// SPDX-FileCopyrightText: Copyright 2025 suyu Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "device_power_state.h"

#if defined(_WIN32)
#include <windows.h>

#elif defined(__linux__)
#include <fstream>
#include <string>
#include <dirent.h>
#endif

namespace Common {

    PowerStatus GetPowerStatus()
    {
        PowerStatus info;

#if defined(_WIN32)
        SYSTEM_POWER_STATUS status;
        if (GetSystemPowerStatus(&status)) {
            if (status.BatteryFlag == 128) {
                info.has_battery = false;
            } else {
                info.percentage = status.BatteryLifePercent;
                info.charging = (status.BatteryFlag & 8) != 0;
            }
        } else {
            info.has_battery = false;
        }

#elif defined(__linux__)
        const char* battery_path = "/sys/class/power_supply/BAT0/";

        std::ifstream capFile(std::string(battery_path) + "capacity");
        if (capFile) {
            capFile >> info.percentage;
        }
        else {
            info.has_battery = false;
        }

        std::ifstream statFile(std::string(battery_path) + "status");
        if (statFile) {
            std::string status;
            std::getline(statFile, status);
            info.charging = (status == "Charging");
        }
#else
        info.has_battery = false;
#endif

        return info;
    }
}
