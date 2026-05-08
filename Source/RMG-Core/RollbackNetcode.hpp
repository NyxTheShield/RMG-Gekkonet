/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef CORE_ROLLBACK_NETCODE_HPP
#define CORE_ROLLBACK_NETCODE_HPP

#include "m64p/api/m64p_types.h"

struct CoreRollbackState
{
    unsigned char* buffer = nullptr;
    int len = 0;
    int checksum = 0;
    int frame = 0;
};

using CoreRollbackInputCallback = int (*)(void* values, int size, int players);

bool CoreRollbackSaveGameState(CoreRollbackState& state, int frame);
bool CoreRollbackSaveGameStateInto(CoreRollbackState& state, unsigned char* buffer, int capacity, int frame);
bool CoreRollbackLoadGameState(const CoreRollbackState& state);
void CoreRollbackFreeGameState(CoreRollbackState& state);
bool CoreRollbackAdvanceFrame(void);
bool CoreRollbackSampleInput(void* values, int size, int players);
bool CoreRollbackSetInputCallback(CoreRollbackInputCallback callback);
bool CoreRollbackSetDeterministic(bool enabled);
bool CoreRollbackExecute(m64p_rollback_execute_callbacks& callbacks);
bool CoreRollbackRunFrame(int flags);

#endif // CORE_ROLLBACK_NETCODE_HPP
