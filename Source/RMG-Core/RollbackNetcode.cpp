/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 *  Copyright (C) 2020-2025 Rosalie Wanders <rosalie@mailbox.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3.
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#define CORE_INTERNAL
#include "RollbackNetcode.hpp"
#include "Emulation.hpp"
#include "Error.hpp"
#include "Library.hpp"

#include "m64p/Api.hpp"

CORE_EXPORT bool CoreRollbackSaveGameState(CoreRollbackState& state, int frame)
{
    return CoreRollbackSaveGameStateInto(state, nullptr, 0, frame);
}

CORE_EXPORT bool CoreRollbackSaveGameStateInto(CoreRollbackState& state, unsigned char* buffer, int capacity, int frame)
{
    std::string error;
    m64p_error ret;
    m64p_rollback_state coreState = {};

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    coreState.frame = frame;
    coreState.buffer = buffer;
    coreState.len = capacity;
    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_SAVE_STATE, 0, &coreState);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackSaveGameState DoCommand(M64CMD_ROLLBACK_SAVE_STATE) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    state.buffer = coreState.buffer;
    state.len = coreState.len;
    state.checksum = coreState.checksum;
    state.frame = frame;
    return true;
}

CORE_EXPORT bool CoreRollbackLoadGameState(const CoreRollbackState& state)
{
    std::string error;
    m64p_error ret;
    m64p_rollback_state coreState = {};

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    coreState.buffer = state.buffer;
    coreState.len = state.len;
    coreState.checksum = state.checksum;
    coreState.frame = state.frame;
    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_LOAD_STATE, 0, &coreState);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackLoadGameState DoCommand(M64CMD_ROLLBACK_LOAD_STATE) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    return true;
}

CORE_EXPORT void CoreRollbackFreeGameState(CoreRollbackState& state)
{
    if (state.buffer != nullptr && m64p::Core.IsHooked())
    {
        m64p::Core.DoCommand(M64CMD_ROLLBACK_FREE_STATE, 0, state.buffer);
    }

    state = {};
}

CORE_EXPORT bool CoreRollbackAdvanceFrame(void)
{
    return CoreRunFrames(1, CoreFrameOutput_None);
}

CORE_EXPORT bool CoreRollbackSampleInput(void* values, int size, int players)
{
    std::string error;
    m64p_error ret;
    m64p_rollback_input_sample sample = {};

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    sample.values = values;
    sample.size = size;
    sample.players = players;
    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_SAMPLE_INPUT, 0, &sample);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackSampleInput DoCommand(M64CMD_ROLLBACK_SAMPLE_INPUT) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
    }

    return ret == M64ERR_SUCCESS;
}

CORE_EXPORT bool CoreRollbackSetInputCallback(CoreRollbackInputCallback callback)
{
    std::string error;
    m64p_error ret;

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_SET_INPUT_CALLBACK, 0, reinterpret_cast<void*>(callback));
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackSetInputCallback DoCommand(M64CMD_ROLLBACK_SET_INPUT_CALLBACK) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    return true;
}

CORE_EXPORT bool CoreRollbackSetDeterministic(bool enabled)
{
    std::string error;
    m64p_error ret;

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_SET_DETERMINISTIC, enabled ? 1 : 0, nullptr);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackSetDeterministic DoCommand(M64CMD_ROLLBACK_SET_DETERMINISTIC) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    return true;
}

CORE_EXPORT bool CoreRollbackSetVerboseStats(bool enabled)
{
    std::string error;
    m64p_error ret;

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_SET_VERBOSE_STATS, enabled ? 1 : 0, nullptr);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackSetVerboseStats DoCommand(M64CMD_ROLLBACK_SET_VERBOSE_STATS) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    return true;
}

CORE_EXPORT bool CoreRollbackSetTimesyncScale(double scale)
{
    std::string error;
    m64p_error ret;

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_SET_TIMESYNC_SCALE, 0, &scale);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackSetTimesyncScale DoCommand(M64CMD_ROLLBACK_SET_TIMESYNC_SCALE) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    return true;
}

CORE_EXPORT bool CoreRollbackExecute(m64p_rollback_execute_callbacks& callbacks)
{
    std::string error;
    m64p_error ret;

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_EXECUTE, 0, &callbacks);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackExecute DoCommand(M64CMD_ROLLBACK_EXECUTE) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    return true;
}

CORE_EXPORT bool CoreRollbackRunFrame(int flags)
{
    std::string error;
    m64p_error ret;

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_RUN_FRAME, flags, nullptr);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackRunFrame DoCommand(M64CMD_ROLLBACK_RUN_FRAME) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    return true;
}

CORE_EXPORT bool CoreRollbackGetRunFrameStats(CoreRollbackRunFrameStats& stats)
{
    std::string error;
    m64p_error ret;
    m64p_rollback_run_frame_stats coreStats = {};

    if (!m64p::Core.IsHooked())
    {
        return false;
    }

    ret = m64p::Core.DoCommand(M64CMD_ROLLBACK_GET_RUN_FRAME_STATS, 0, &coreStats);
    if (ret != M64ERR_SUCCESS)
    {
        error = "CoreRollbackGetRunFrameStats DoCommand(M64CMD_ROLLBACK_GET_RUN_FRAME_STATS) Failed: ";
        error += m64p::Core.ErrorMessage(ret);
        CoreSetError(error);
        return false;
    }

    stats.totalUs = coreStats.total_us;
    stats.r4300Us = coreStats.r4300_us;
    stats.viUs = coreStats.vi_us;
    stats.newFrameUs = coreStats.new_frame_us;
    stats.cheatsUs = coreStats.cheats_us;
    stats.pacingUs = coreStats.pacing_us;
    stats.inputUs = coreStats.input_us;
    stats.pauseUs = coreStats.pause_us;
    stats.netplayUs = coreStats.netplay_us;
    stats.dynarecRecompileCount = coreStats.dynarec_recompile_count;
    stats.dynarecRecompileUs = coreStats.dynarec_recompile_us;
    stats.dynarecInvalidateUs = coreStats.dynarec_invalidate_us;
    stats.dynarecFullInvalidateCount = coreStats.dynarec_full_invalidate_count;
    stats.dynarecRangeInvalidateCount = coreStats.dynarec_range_invalidate_count;
    stats.dynarecBlockInvalidateCount = coreStats.dynarec_block_invalidate_count;
    stats.cachedCodeFullInvalidateCount = coreStats.cached_code_full_invalidate_count;
    stats.cachedCodeRangeInvalidateCount = coreStats.cached_code_range_invalidate_count;
    stats.emumode = coreStats.emumode;
    stats.cp0CountBefore = coreStats.cp0_count_before;
    stats.cp0CountAfter = coreStats.cp0_count_after;
    stats.nextInterruptBefore = coreStats.next_interrupt_before;
    stats.nextInterruptAfter = coreStats.next_interrupt_after;
    stats.pcBefore = coreStats.pc_before;
    stats.pcAfter = coreStats.pc_after;
    stats.currentFrameBefore = coreStats.current_frame_before;
    stats.currentFrameAfter = coreStats.current_frame_after;
    stats.dynarecPcaddrBefore = coreStats.dynarec_pcaddr_before;
    stats.dynarecPcaddrAfter = coreStats.dynarec_pcaddr_after;
    stats.cp0LastAddrBefore = coreStats.cp0_last_addr_before;
    stats.cp0LastAddrAfter = coreStats.cp0_last_addr_after;
    stats.dynarecCycleCountBefore = coreStats.dynarec_cycle_count_before;
    stats.dynarecCycleCountAfter = coreStats.dynarec_cycle_count_after;
    stats.dynarecPendingExceptionBefore = coreStats.dynarec_pending_exception_before;
    stats.dynarecPendingExceptionAfter = coreStats.dynarec_pending_exception_after;
    stats.dynarecStopBefore = coreStats.dynarec_stop_before;
    stats.dynarecStopAfter = coreStats.dynarec_stop_after;
    stats.delaySlotBefore = coreStats.delay_slot_before;
    stats.delaySlotAfter = coreStats.delay_slot_after;
    stats.outputFlags = coreStats.output_flags;
    return true;
}
