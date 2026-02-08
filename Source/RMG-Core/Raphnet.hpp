/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef CORE_RAPHNET_HPP
#define CORE_RAPHNET_HPP

#include <string>
#include <algorithm>
#include <cctype>

// Returns true if the device name suggests a Raphnet adapter with firmware 3.0+.
// Pre-3.0 adapters include the version in the USB product string
// (e.g., "raphnet.net GC/N64 to USB, v2.9").
// 3.0+ adapters use a generic name without a version
// (e.g., "Raphnet GC and N64 Adapter").
inline bool isRaphnet3Plus(const std::string& deviceName)
{
    // Case-insensitive check for "raphnet"
    std::string nameLower = deviceName;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
        [](unsigned char c) { return std::tolower(c); });

    if (nameLower.find("raphnet") == std::string::npos)
    {
        return false;
    }

    // Look for a version pattern like "v<major>.<minor>"
    // e.g., "v2.9", "v3.0", "v3.6.1"
    size_t pos = 0;
    while (pos < nameLower.size())
    {
        pos = nameLower.find('v', pos);
        if (pos == std::string::npos)
        {
            break;
        }

        size_t numStart = pos + 1;
        if (numStart < nameLower.size() && std::isdigit(nameLower[numStart]))
        {
            // Parse the major version number
            int major = 0;
            size_t i = numStart;
            while (i < nameLower.size() && std::isdigit(nameLower[i]))
            {
                major = major * 10 + (nameLower[i] - '0');
                i++;
            }

            // Check for '.' after major version
            if (i < nameLower.size() && nameLower[i] == '.')
            {
                // Found a version pattern like "v<major>.<...>"
                return major >= 3;
            }
        }

        pos++;
    }

    // No version found in name — likely 3.0+ firmware
    return true;
}

#endif // CORE_RAPHNET_HPP
