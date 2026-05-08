/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef RMGK_GEKKO_HPP
#define RMGK_GEKKO_HPP

#include "Emulation.hpp"
#include "RollbackNetcode.hpp"

class rmgk_gekko
{
  public:
    static bool start_p2p_session(const char* gameName, int players, int inputSize,
        int localPlayer, unsigned short localPort, const char* remoteIp, unsigned short remotePort, int localDelay);
    static void close_session();
    static void request_stop();
    static bool execute();
    static bool set_deterministic(bool enabled);
    static bool install_core_input_callback();
    static void clear_core_input_callback();
    static bool synchronize_input(void* values, int size, int players);
};

#endif // RMGK_GEKKO_HPP
