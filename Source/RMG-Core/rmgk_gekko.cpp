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
#include "rmgk_gekko.hpp"

#include "Error.hpp"
#include "Library.hpp"

#ifdef RMGK_HAVE_GEKKONET
#include <gekkonet.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr unsigned int kGekkoStateCapacity = 24u * 1024u * 1024u;
constexpr int kGekkoMaxLoggedFrames = 600;
constexpr float kGekkoPacingFramesAheadThreshold = 0.5f;
constexpr int kGekkoPacingExtraUs = 267;
constexpr int kGekkoWaitSleepUs = 100;

#ifdef RMGK_HAVE_GEKKONET
struct PendingGekkoSave
{
    int frame = 0;
    unsigned int* checksum = nullptr;
    unsigned int* stateLen = nullptr;
    unsigned char* state = nullptr;
};

GekkoSession* g_GekkoSession = nullptr;
int g_GekkoPlayers = 0;
int g_GekkoInputSize = 0;
int g_GekkoLocalPlayer = 0;
int g_GekkoLocalHandle = -1;
int g_GekkoRemoteHandle = -1;
std::vector<unsigned char> g_GekkoLatchedInput;
bool g_GekkoHasLatchedInput = false;
std::vector<PendingGekkoSave> g_GekkoPendingSaves;
std::mutex g_GekkoLogMutex;
int g_GekkoLogFrames = 0;
uint32_t g_GekkoLastSubmittedInput = 0xffffffffu;
std::vector<unsigned char> g_GekkoLastLatchedInput;
int g_GekkoWaitingLoops = 0;
int g_GekkoLocalInputLogRepeats = 0;
int g_GekkoPacingLogFrames = 0;
bool g_GekkoLogEnabled = false;
#endif

int rmgk_gekko_core_input_callback(void* values, int size, int players)
{
    return rmgk_gekko::synchronize_input(values, size, players) ? 1 : 0;
}

#ifdef RMGK_HAVE_GEKKONET
std::string hex_input(uint32_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return stream.str();
}

void reset_gekko_log()
{
    const char* logEnv = std::getenv("RMGK_GEKKO_LOG");
    g_GekkoLogEnabled = logEnv != nullptr && std::strcmp(logEnv, "0") != 0;
    if (!g_GekkoLogEnabled)
    {
        g_GekkoLogFrames = 0;
        g_GekkoLastSubmittedInput = 0xffffffffu;
        g_GekkoLastLatchedInput.clear();
        g_GekkoWaitingLoops = 0;
        g_GekkoLocalInputLogRepeats = 0;
        g_GekkoPacingLogFrames = 0;
        return;
    }

    std::lock_guard<std::mutex> lock(g_GekkoLogMutex);
    const char* path = g_GekkoLocalPlayer == 2 ? "rollback_gekko_client.log" : "rollback_gekko_host.log";
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    file << "RMG-K GekkoNet input log\n";
    g_GekkoLogFrames = 0;
    g_GekkoLastSubmittedInput = 0xffffffffu;
    g_GekkoLastLatchedInput.clear();
    g_GekkoWaitingLoops = 0;
    g_GekkoLocalInputLogRepeats = 0;
    g_GekkoPacingLogFrames = 0;
}

void write_gekko_log(const std::string& message)
{
    if (!g_GekkoLogEnabled)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_GekkoLogMutex);
    const char* path = g_GekkoLocalPlayer == 2 ? "rollback_gekko_client.log" : "rollback_gekko_host.log";
    std::ofstream file(path, std::ios::out | std::ios::app);
    file << "core_frame=" << CoreGetCurrentFrameCount() << " " << message << "\n";
}

const char* gekko_session_event_name(GekkoSessionEventType type)
{
    switch (type)
    {
    case GekkoPlayerSyncing: return "player_syncing";
    case GekkoPlayerConnected: return "player_connected";
    case GekkoPlayerDisconnected: return "player_disconnected";
    case GekkoSessionStarted: return "session_started";
    case GekkoSpectatorPaused: return "spectator_paused";
    case GekkoSpectatorUnpaused: return "spectator_unpaused";
    case GekkoDesyncDetected: return "desync_detected";
    default: return "unknown";
    }
}

const char* gekko_game_event_name(GekkoGameEventType type)
{
    switch (type)
    {
    case GekkoAdvanceEvent: return "advance";
    case GekkoSaveEvent: return "save";
    case GekkoLoadEvent: return "load";
    default: return "unknown";
    }
}

void log_session_events()
{
    if (!g_GekkoLogEnabled)
    {
        return;
    }

    int count = 0;
    GekkoSessionEvent** events = gekko_session_events(g_GekkoSession, &count);
    for (int i = 0; i < count; i++)
    {
        GekkoSessionEvent* event = events[i];
        if (event == nullptr)
        {
            continue;
        }

        std::ostringstream stream;
        stream << "event name=" << gekko_session_event_name(event->type);
        switch (event->type)
        {
        case GekkoPlayerSyncing:
            stream << " handle=" << event->data.syncing.handle
                   << " count=" << static_cast<int>(event->data.syncing.current)
                   << " total=" << static_cast<int>(event->data.syncing.max);
            break;
        case GekkoPlayerConnected:
            stream << " handle=" << event->data.connected.handle;
            break;
        case GekkoPlayerDisconnected:
            stream << " handle=" << event->data.disconnected.handle;
            break;
        case GekkoDesyncDetected:
            stream << " frame=" << event->data.desynced.frame
                   << " remote_handle=" << event->data.desynced.remote_handle
                   << " local_checksum=" << event->data.desynced.local_checksum
                   << " remote_checksum=" << event->data.desynced.remote_checksum;
            break;
        default:
            break;
        }
        write_gekko_log(stream.str());
    }
}

bool save_gekko_state(const PendingGekkoSave& save)
{
    CoreRollbackState state;
    const int coreFrame = std::max(0, save.frame);
    if (g_GekkoLogEnabled)
    {
        std::ostringstream stream;
        stream << "save_state begin frame=" << save.frame
               << " core_frame=" << coreFrame
               << " state_ptr=" << static_cast<void*>(save.state)
               << " state_len_ptr=" << static_cast<void*>(save.stateLen)
               << " checksum_ptr=" << static_cast<void*>(save.checksum);
        write_gekko_log(stream.str());
    }

    if (save.state == nullptr || save.stateLen == nullptr)
    {
        write_gekko_log("save_state result=fail reason=null_event_buffer");
        CoreSetError("GekkoNet save event did not provide a state buffer");
        return false;
    }

    if (save.frame < 0)
    {
        *save.stateLen = 0;
        if (save.checksum != nullptr)
        {
            *save.checksum = 0;
        }
        write_gekko_log("save_state result=skipped reason=pre_frame_baseline");
        return true;
    }

    if (!CoreRollbackSaveGameStateInto(state, save.state, static_cast<int>(kGekkoStateCapacity), coreFrame))
    {
        write_gekko_log("save_state result=fail");
        return false;
    }

    if (state.len < 1 || static_cast<unsigned int>(state.len) > kGekkoStateCapacity)
    {
        std::ostringstream stream;
        stream << "save_state result=fail reason=state_too_large len=" << state.len
               << " capacity=" << kGekkoStateCapacity;
        write_gekko_log(stream.str());
        CoreSetError("GekkoNet rollback state exceeded configured state buffer");
        return false;
    }

    if (state.buffer != save.state)
    {
        write_gekko_log("save_state result=fail reason=state_not_written_in_place");
        CoreSetError("GekkoNet rollback state was not saved into the provided state buffer");
        return false;
    }

    if (save.stateLen != nullptr)
    {
        *save.stateLen = static_cast<unsigned int>(state.len);
    }
    if (save.checksum != nullptr)
    {
        *save.checksum = static_cast<unsigned int>(state.checksum);
    }

    if (g_GekkoLogEnabled && g_GekkoLogFrames < kGekkoMaxLoggedFrames)
    {
        std::ostringstream stream;
        stream << "save_state result=ok frame=" << save.frame
               << " len=" << state.len
               << " checksum=" << static_cast<unsigned int>(state.checksum);
        write_gekko_log(stream.str());
    }

    return true;
}

bool load_gekko_state(const GekkoGameEvent* event)
{
    CoreRollbackState state;
    state.buffer = event->data.load.state;
    state.len = static_cast<int>(event->data.load.state_len);
    state.frame = event->data.load.frame;
    if (!CoreRollbackLoadGameState(state))
    {
        write_gekko_log("load_state result=fail");
        return false;
    }

    if (g_GekkoLogEnabled && g_GekkoLogFrames < kGekkoMaxLoggedFrames)
    {
        std::ostringstream stream;
        stream << "load_state result=ok frame=" << event->data.load.frame
               << " len=" << event->data.load.state_len;
        write_gekko_log(stream.str());
    }
    return true;
}

bool submit_local_input()
{
    uint32_t input = 0;
    if (!CoreRollbackSampleInput(&input, g_GekkoInputSize, 1))
    {
        write_gekko_log("add_local_input sample=fail");
        return false;
    }

    gekko_add_local_input(g_GekkoSession, g_GekkoLocalHandle, &input);

    const bool changed = input != g_GekkoLastSubmittedInput;
    if (changed)
    {
        g_GekkoLocalInputLogRepeats = 0;
    }
    else
    {
        g_GekkoLocalInputLogRepeats++;
    }

    if (g_GekkoLogEnabled &&
        (changed || g_GekkoLocalInputLogRepeats <= 20 || (g_GekkoLocalInputLogRepeats % 60) == 0))
    {
        std::ostringstream stream;
        stream << "add_local_input local_player=" << g_GekkoLocalPlayer
               << " handle=" << g_GekkoLocalHandle
               << " physical_p1=" << hex_input(input)
               << " repeat=" << g_GekkoLocalInputLogRepeats;
        write_gekko_log(stream.str());
    }
    g_GekkoLastSubmittedInput = input;
    return true;
}

bool latch_gekko_input(const GekkoGameEvent* event)
{
    const int expectedBytes = g_GekkoPlayers * g_GekkoInputSize;
    if (event->data.adv.inputs == nullptr || static_cast<int>(event->data.adv.input_len) < expectedBytes)
    {
        write_gekko_log("sync_input result=fail reason=shape");
        return false;
    }

    if (static_cast<int>(g_GekkoLatchedInput.size()) != expectedBytes)
    {
        g_GekkoLatchedInput.resize(static_cast<size_t>(expectedBytes));
    }
    std::memcpy(g_GekkoLatchedInput.data(), event->data.adv.inputs, static_cast<size_t>(expectedBytes));
    g_GekkoHasLatchedInput = true;

    if (g_GekkoLogEnabled &&
        (g_GekkoLogFrames < kGekkoMaxLoggedFrames || g_GekkoLatchedInput != g_GekkoLastLatchedInput))
    {
        std::ostringstream stream;
        stream << "sync_input result=ok frame=" << event->data.adv.frame
               << " rolling_back=" << (event->data.adv.rolling_back ? "true" : "false")
               << " running_ahead=" << (event->data.adv.running_ahead ? "true" : "false");
        for (int player = 0; player < g_GekkoPlayers; player++)
        {
            uint32_t input = 0;
            std::memcpy(&input, g_GekkoLatchedInput.data() + (player * g_GekkoInputSize),
                std::min(g_GekkoInputSize, static_cast<int>(sizeof(input))));
            stream << " p" << (player + 1) << "=" << hex_input(input);
        }
        write_gekko_log(stream.str());
        g_GekkoLastLatchedInput = g_GekkoLatchedInput;
        g_GekkoLogFrames++;
    }
    return true;
}

bool process_pending_saves()
{
    for (const auto& save : g_GekkoPendingSaves)
    {
        if (!save_gekko_state(save))
        {
            g_GekkoPendingSaves.clear();
            return false;
        }
    }
    g_GekkoPendingSaves.clear();
    return true;
}

void apply_gekko_frame_pacing()
{
    const float framesAhead = gekko_frames_ahead(g_GekkoSession);
    int sleepUs = 0;

    if (framesAhead > kGekkoPacingFramesAheadThreshold)
    {
        sleepUs = static_cast<int>(std::lround(kGekkoPacingExtraUs * std::max(1.0f, framesAhead)));
        sleepUs = std::clamp(sleepUs, kGekkoPacingExtraUs, 2000);
        std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
    }

    g_GekkoPacingLogFrames++;
    if (g_GekkoLogEnabled &&
        (sleepUs > 0 || g_GekkoPacingLogFrames <= 10 || (g_GekkoPacingLogFrames % 60) == 0))
    {
        std::ostringstream stream;
        stream << "pacing frames_ahead=" << std::fixed << std::setprecision(2) << framesAhead
               << " sleep_us=" << sleepUs;

        if (g_GekkoRemoteHandle >= 0)
        {
            GekkoNetworkStats stats = {};
            gekko_network_stats(g_GekkoSession, g_GekkoRemoteHandle, &stats);
            stream << " remote_handle=" << g_GekkoRemoteHandle
                   << " ping_ms=" << stats.last_ping
                   << " avg_ping_ms=" << std::setprecision(1) << stats.avg_ping
                   << " jitter_ms=" << stats.jitter
                   << " kb_sent=" << stats.kb_sent
                   << " kb_recv=" << stats.kb_received;
        }

        write_gekko_log(stream.str());
    }
}

int rollback_execute_begin_frame(void* userData)
{
    (void)userData;
    if (g_GekkoSession == nullptr)
    {
        return 0;
    }

    g_GekkoHasLatchedInput = false;
    g_GekkoPendingSaves.clear();

    for (;;)
    {
        if (!submit_local_input())
        {
            return 0;
        }

        int count = 0;
        GekkoGameEvent** events = gekko_update_session(g_GekkoSession, &count);
        log_session_events();

        if (count == 0)
        {
            g_GekkoWaitingLoops++;
            if (g_GekkoLogEnabled &&
                (g_GekkoWaitingLoops <= 20 || (g_GekkoWaitingLoops % 60) == 0))
            {
                std::ostringstream stream;
                stream << "update_session result=waiting loop=" << g_GekkoWaitingLoops
                       << " events=0";
                write_gekko_log(stream.str());
            }
        }
        else if (g_GekkoLogEnabled)
        {
            std::ostringstream stream;
            stream << "update_session result=events count=" << count;
            for (int i = 0; i < count; i++)
            {
                if (events[i] != nullptr)
                {
                    stream << " event" << i << "=" << gekko_game_event_name(events[i]->type);
                    if (events[i]->type == GekkoAdvanceEvent)
                    {
                        stream << "(frame=" << events[i]->data.adv.frame
                               << ",rollback=" << (events[i]->data.adv.rolling_back ? "true" : "false")
                               << ",runahead=" << (events[i]->data.adv.running_ahead ? "true" : "false")
                               << ")";
                    }
                    else if (events[i]->type == GekkoSaveEvent)
                    {
                        stream << "(frame=" << events[i]->data.save.frame << ")";
                    }
                    else if (events[i]->type == GekkoLoadEvent)
                    {
                        stream << "(frame=" << events[i]->data.load.frame
                               << ",len=" << events[i]->data.load.state_len << ")";
                    }
                }
            }
            write_gekko_log(stream.str());
            g_GekkoWaitingLoops = 0;
        }
        else
        {
            g_GekkoWaitingLoops = 0;
        }

        bool deferSavesUntilFrameEnd = false;
        bool hasRealAdvance = false;
        for (int i = 0; i < count; i++)
        {
            GekkoGameEvent* event = events[i];
            if (event == nullptr)
            {
                continue;
            }

            switch (event->type)
            {
            case GekkoSaveEvent:
            {
                PendingGekkoSave save;
                save.frame = event->data.save.frame;
                save.checksum = event->data.save.checksum;
                save.stateLen = event->data.save.state_len;
                save.state = event->data.save.state;
                if (deferSavesUntilFrameEnd)
                {
                    write_gekko_log("save_state result=deferred");
                    g_GekkoPendingSaves.push_back(save);
                }
                else if (!save_gekko_state(save))
                {
                    return 0;
                }
                break;
            }
            case GekkoLoadEvent:
                write_gekko_log("load_state begin");
                if (!load_gekko_state(event))
                {
                    return 0;
                }
                break;
            case GekkoAdvanceEvent:
                write_gekko_log("advance_frame begin");
                if (!latch_gekko_input(event))
                {
                    return 0;
                }

                if (event->data.adv.rolling_back || event->data.adv.running_ahead)
                {
                    if (!CoreRollbackRunFrame(CoreFrameOutput_None))
                    {
                        write_gekko_log("advance_frame result=fail reason=run_frame");
                        return 0;
                    }
                    write_gekko_log("advance_frame result=rollback_frame_ok");
                    g_GekkoHasLatchedInput = false;
                }
                else
                {
                    write_gekko_log("advance_frame result=real_frame_ready");
                    hasRealAdvance = true;
                    deferSavesUntilFrameEnd = true;
                }
                break;
            default:
                break;
            }
        }

        if (hasRealAdvance)
        {
            write_gekko_log("begin_frame result=real_frame");
            return 1;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(kGekkoWaitSleepUs));
    }
}

int rollback_execute_end_frame(void* userData)
{
    (void)userData;
    write_gekko_log("end_frame begin");
    if (!process_pending_saves())
    {
        write_gekko_log("end_frame result=fail reason=save");
        return 0;
    }
    apply_gekko_frame_pacing();
    g_GekkoHasLatchedInput = false;
    write_gekko_log("end_frame result=ok");
    return 1;
}
#endif
} // namespace

CORE_EXPORT bool rmgk_gekko::start_p2p_session(const char* gameName, int players, int inputSize,
    int localPlayer, unsigned short localPort, const char* remoteIp, unsigned short remotePort, int localDelay)
{
#ifndef RMGK_HAVE_GEKKONET
    (void)gameName;
    (void)players;
    (void)inputSize;
    (void)localPlayer;
    (void)localPort;
    (void)remoteIp;
    (void)remotePort;
    (void)localDelay;
    return false;
#else
    close_session();

    g_GekkoLocalPlayer = localPlayer;
    reset_gekko_log();

    if (gameName == nullptr || players < 2 || inputSize != static_cast<int>(sizeof(uint32_t)) ||
        localPlayer < 1 || localPlayer > players || remoteIp == nullptr || remoteIp[0] == '\0' || remotePort == 0)
    {
        write_gekko_log("start_p2p_session result=fail reason=invalid_params");
        return false;
    }

    if (!gekko_create(&g_GekkoSession, GekkoGameSession))
    {
        write_gekko_log("gekko_create result=fail");
        return false;
    }

    GekkoConfig config = {};
    config.num_players = static_cast<unsigned char>(players);
    config.max_spectators = 0;
    config.input_prediction_window = 10;
    config.input_size = static_cast<unsigned int>(inputSize);
    config.state_size = kGekkoStateCapacity;
    config.limited_saving = false;
    config.desync_detection = true;
    config.check_distance = 10;
    gekko_start(g_GekkoSession, &config);
    gekko_net_adapter_set(g_GekkoSession, gekko_default_adapter(localPort));
    gekko_set_runahead(g_GekkoSession, 0);

    g_GekkoPlayers = players;
    g_GekkoInputSize = inputSize;
    g_GekkoLocalHandle = -1;
    g_GekkoRemoteHandle = -1;
    g_GekkoLatchedInput.assign(static_cast<size_t>(players * inputSize), 0);
    g_GekkoHasLatchedInput = false;
    g_GekkoPendingSaves.clear();

    if (g_GekkoLogEnabled)
    {
        std::ostringstream stream;
        stream << "start_p2p_session game=" << gameName
               << " players=" << players
               << " input_size=" << inputSize
               << " local_player=" << localPlayer
               << " local_port=" << localPort
               << " remote_ip=" << remoteIp
               << " remote_port=" << remotePort
               << " local_delay=" << localDelay
               << " state_capacity=" << kGekkoStateCapacity;
        write_gekko_log(stream.str());
    }

    std::string remoteAddress = std::string(remoteIp) + ":" + std::to_string(remotePort);
    for (int player = 1; player <= players; player++)
    {
        if (player == localPlayer)
        {
            const int handle = gekko_add_actor(g_GekkoSession, GekkoLocalPlayer, nullptr);
            if (handle < 0)
            {
                write_gekko_log("gekko_add_actor result=fail type=local");
                close_session();
                return false;
            }
            g_GekkoLocalHandle = handle;
            gekko_set_local_delay(g_GekkoSession, handle, static_cast<unsigned char>(std::clamp(localDelay, 0, 10)));
            if (g_GekkoLogEnabled)
            {
                std::ostringstream stream;
                stream << "gekko_add_actor result=ok player=" << player << " type=local handle=" << handle;
                write_gekko_log(stream.str());
            }
        }
        else
        {
            GekkoNetAddress address = {};
            address.data = remoteAddress.data();
            address.size = static_cast<unsigned int>(remoteAddress.size());
            const int handle = gekko_add_actor(g_GekkoSession, GekkoRemotePlayer, &address);
            if (handle < 0)
            {
                write_gekko_log("gekko_add_actor result=fail type=remote");
                close_session();
                return false;
            }
            if (g_GekkoRemoteHandle < 0)
            {
                g_GekkoRemoteHandle = handle;
            }
            if (g_GekkoLogEnabled)
            {
                std::ostringstream stream;
                stream << "gekko_add_actor result=ok player=" << player
                       << " type=remote handle=" << handle
                       << " remote=" << remoteAddress;
                write_gekko_log(stream.str());
            }
        }
    }

    if (g_GekkoLocalHandle < 0)
    {
        write_gekko_log("start_p2p_session result=fail reason=no_local_handle");
        close_session();
        return false;
    }

    if (!install_core_input_callback())
    {
        write_gekko_log("install_core_input_callback result=fail");
        close_session();
        return false;
    }
    write_gekko_log("install_core_input_callback result=ok");
    return true;
#endif
}

CORE_EXPORT void rmgk_gekko::close_session()
{
#ifdef RMGK_HAVE_GEKKONET
    clear_core_input_callback();
    if (g_GekkoSession != nullptr)
    {
        gekko_destroy(&g_GekkoSession);
        gekko_default_adapter_destroy();
    }
    g_GekkoSession = nullptr;
    g_GekkoPlayers = 0;
    g_GekkoInputSize = 0;
    g_GekkoLocalPlayer = 0;
    g_GekkoLocalHandle = -1;
    g_GekkoRemoteHandle = -1;
    g_GekkoLatchedInput.clear();
    g_GekkoHasLatchedInput = false;
    g_GekkoPendingSaves.clear();
#endif
}

CORE_EXPORT bool rmgk_gekko::execute()
{
#ifndef RMGK_HAVE_GEKKONET
    return false;
#else
    if (g_GekkoSession == nullptr)
    {
        return false;
    }

    m64p_rollback_execute_callbacks callbacks = {};
    callbacks.begin_frame = rollback_execute_begin_frame;
    callbacks.end_frame = rollback_execute_end_frame;
    return CoreRollbackExecute(callbacks);
#endif
}

CORE_EXPORT bool rmgk_gekko::set_deterministic(bool enabled)
{
    return CoreRollbackSetDeterministic(enabled);
}

CORE_EXPORT bool rmgk_gekko::install_core_input_callback()
{
    return CoreRollbackSetInputCallback(rmgk_gekko_core_input_callback);
}

CORE_EXPORT void rmgk_gekko::clear_core_input_callback()
{
    CoreRollbackSetInputCallback(nullptr);
}

CORE_EXPORT bool rmgk_gekko::synchronize_input(void* values, int size, int players)
{
#ifdef RMGK_HAVE_GEKKONET
    if (g_GekkoSession != nullptr)
    {
        const int expectedBytes = g_GekkoPlayers * g_GekkoInputSize;
        if (values == nullptr || size != g_GekkoInputSize || players < g_GekkoPlayers ||
            static_cast<int>(g_GekkoLatchedInput.size()) < expectedBytes)
        {
            write_gekko_log("pif_sync result=fail reason=shape");
            return false;
        }

        if (!g_GekkoHasLatchedInput)
        {
            write_gekko_log("pif_sync result=fail reason=no_latched_input");
            return false;
        }

        std::memset(values, 0, static_cast<size_t>(size) * static_cast<size_t>(players));
        std::memcpy(values, g_GekkoLatchedInput.data(), static_cast<size_t>(expectedBytes));
        return true;
    }
#endif
    (void)values;
    (void)size;
    (void)players;
    return true;
}
