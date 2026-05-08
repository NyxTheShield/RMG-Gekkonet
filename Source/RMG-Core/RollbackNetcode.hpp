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

#include <cstdint>

struct CoreRollbackState
{
    unsigned char* buffer = nullptr;
    int len = 0;
    int checksum = 0;
    int frame = 0;
};

struct CoreRollbackRunFrameStats
{
    uint64_t totalUs = 0;
    uint64_t r4300Us = 0;
    uint64_t viUs = 0;
    uint64_t newFrameUs = 0;
    uint64_t cheatsUs = 0;
    uint64_t pacingUs = 0;
    uint64_t inputUs = 0;
    uint64_t pauseUs = 0;
    uint64_t netplayUs = 0;
    uint64_t dynarecRecompileCount = 0;
    uint64_t dynarecRecompileUs = 0;
    uint64_t dynarecInvalidateUs = 0;
    uint64_t dynarecFullInvalidateCount = 0;
    uint64_t dynarecRangeInvalidateCount = 0;
    uint64_t dynarecBlockInvalidateCount = 0;
    uint64_t cachedCodeFullInvalidateCount = 0;
    uint64_t cachedCodeRangeInvalidateCount = 0;
    uint32_t emumode = 0;
    uint32_t cp0CountBefore = 0;
    uint32_t cp0CountAfter = 0;
    uint32_t nextInterruptBefore = 0;
    uint32_t nextInterruptAfter = 0;
    uint32_t pcBefore = 0;
    uint32_t pcAfter = 0;
    uint32_t currentFrameBefore = 0;
    uint32_t currentFrameAfter = 0;
    uint32_t dynarecPcaddrBefore = 0;
    uint32_t dynarecPcaddrAfter = 0;
    uint32_t cp0LastAddrBefore = 0;
    uint32_t cp0LastAddrAfter = 0;
    int32_t dynarecCycleCountBefore = 0;
    int32_t dynarecCycleCountAfter = 0;
    int32_t dynarecPendingExceptionBefore = 0;
    int32_t dynarecPendingExceptionAfter = 0;
    int32_t dynarecStopBefore = 0;
    int32_t dynarecStopAfter = 0;
    int32_t delaySlotBefore = 0;
    int32_t delaySlotAfter = 0;
    int outputFlags = 0;
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
bool CoreRollbackSetVerboseStats(bool enabled);
bool CoreRollbackSetTimesyncScale(double scale);
bool CoreRollbackExecute(m64p_rollback_execute_callbacks& callbacks);
bool CoreRollbackRunFrame(int flags);
bool CoreRollbackGetRunFrameStats(CoreRollbackRunFrameStats& stats);

#endif // CORE_ROLLBACK_NETCODE_HPP
