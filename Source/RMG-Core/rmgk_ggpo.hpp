/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef RMGK_GGPO_HPP
#define RMGK_GGPO_HPP

#include "Emulation.hpp"
#include "RollbackNetcode.hpp"

class rmgk_ggpo
{
  public:
    using SynchronizeInputCallback = bool (*)(void* values, int size, int players, void* userData);

    static bool save_game_state(CoreRollbackState& state, int frame);
    static bool load_game_state(const CoreRollbackState& state);
    static void free_buffer(CoreRollbackState& state);
    static bool advance_frame(int frameOutputFlags = CoreFrameOutput_None);
    static bool advance_frames(int frames, int frameOutputFlags = CoreFrameOutput_None);
    static bool set_deterministic(bool enabled);

    static void set_synchronize_input_callback(SynchronizeInputCallback callback, void* userData);
    static bool install_core_input_callback();
    static void clear_core_input_callback();
    static bool synchronize_input(void* values, int size, int players);
};

#endif // RMGK_GGPO_HPP
