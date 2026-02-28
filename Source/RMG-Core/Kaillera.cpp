/*
 * Rosalie's Mupen GUI - https://github.com/Rosalie241/RMG
 * Copyright (C) 2020 Rosalie Wanders <rosalie@mailbox.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#define CORE_INTERNAL
#include "Kaillera.hpp"
#include "Settings.hpp"
#include "Error.hpp"

#ifdef _WIN32

#include "n02_client.h"
#include "kailleraclient.h"

#include <cstring>

//
// Static Variables
//

static bool s_Initialized = false;
static bool s_GameActive = false;
static int s_PlayerNumber = 0; // 0 = not in netplay, 1-4 = player number
static int s_NumPlayers = 0;   // Total number of players in the game
static std::string s_AppName;
static std::string s_GameList;

// Callback storage
static CoreKaillera::GameStartCallback s_GameStartCallback;
static CoreKaillera::ChatReceivedCallback s_ChatReceivedCallback;
static CoreKaillera::ClientDroppedCallback s_ClientDroppedCallback;
static CoreKaillera::MoreInfosCallback s_MoreInfosCallback;

//
// C Callback Bridges (called by n02 from its internal thread)
//

static int GameCallbackBridge(char *game, int player, int numplayers)
{
    // Store player number and total player count
    s_PlayerNumber = player;
    s_NumPlayers = numplayers;

    // Set game active BEFORE callback so emulation thread sees it immediately
    s_GameActive = true;

    if (s_GameStartCallback)
    {
        try
        {
            s_GameStartCallback(std::string(game ? game : ""), player, numplayers);
            return 0; // Success
        }
        catch (...)
        {
            s_GameActive = false;
            return -1; // Error
        }
    }
    return 0;
}

static void ChatReceivedCallbackBridge(char *nick, char *text)
{
    if (s_ChatReceivedCallback)
    {
        try
        {
            s_ChatReceivedCallback(std::string(nick ? nick : ""), std::string(text ? text : ""));
        }
        catch (...)
        {
            // Ignore errors in callback
        }
    }
}

static void ClientDroppedCallbackBridge(char *nick, int playernb)
{
    if (s_ClientDroppedCallback)
    {
        try
        {
            s_ClientDroppedCallback(std::string(nick ? nick : ""), playernb);
        }
        catch (...)
        {
            // Ignore errors in callback
        }
    }
}

static void MoreInfosCallbackBridge(char *gamename)
{
    if (s_MoreInfosCallback)
    {
        try
        {
            s_MoreInfosCallback(std::string(gamename ? gamename : ""));
        }
        catch (...)
        {
            // Ignore errors in callback
        }
    }
}

//
// Exported Functions
//

CORE_EXPORT bool CoreInitKaillera(void)
{
    if (s_Initialized)
    {
        return true; // Already initialized
    }

    // Initialize n02 subsystem (sockets, settings, modules)
    int ret = n02::init();
    if (ret != 0)
    {
        CoreSetError("Kaillera initialization failed with error code: " + std::to_string(ret));
        return false;
    }

    // Verify version
    char version[16];
    n02::getVersion(version);

    // Load settings from RMG-K config into n02 globals
    int mode = CoreSettingsGetIntValue(SettingsID::Kaillera_ActiveMode);
    n02::activateMode(mode);

    kaillera_spoof_ping = CoreSettingsGetIntValue(SettingsID::Kaillera_SpoofPing);
    kaillera_30fps_mode = CoreSettingsGetBoolValue(SettingsID::Kaillera_30fpsMode) ? 1 : 0;
    p2p_frame_delay_override = CoreSettingsGetIntValue(SettingsID::Kaillera_FrameDelay);
    p2p_30fps_mode = CoreSettingsGetBoolValue(SettingsID::Kaillera_30fpsMode) ? 1 : 0;

    s_Initialized = true;
    s_GameActive = false;

    return true;
}

CORE_EXPORT bool CoreShutdownKaillera(void)
{
    if (!s_Initialized)
    {
        return true; // Not initialized, nothing to do
    }

    // End game if active
    if (s_GameActive)
    {
        CoreEndKailleraGame();
    }

    // Save settings back to RMG-K config
    CoreSettingsSetValue(SettingsID::Kaillera_ActiveMode, n02::getActiveMode());

    // Shutdown n02 subsystem
    n02::shutdown();

    s_Initialized = false;
    s_GameActive = false;

    return true;
}

CORE_EXPORT bool CoreHasInitKaillera(void)
{
    return s_Initialized && s_GameActive;
}

CORE_EXPORT bool CoreShowKailleraServerDialog(void* parentHwnd)
{
    if (!s_Initialized)
    {
        CoreSetError("Kaillera not initialized. Call CoreInitKaillera() first");
        return false;
    }

    // Show n02 server dialog (blocking in Phase 1, Qt-driven in Phase 6)
    n02::selectServerDialog(parentHwnd);

    return true;
}

CORE_EXPORT int CoreModifyKailleraPlayValues(void* values, int size)
{
    if (!s_Initialized || !s_GameActive)
    {
        return -1; // Not in netplay mode
    }

    // Call n02 to synchronize input
    return n02::modifyPlayValues(values, size);
}

CORE_EXPORT bool CoreKailleraSendChat(std::string text)
{
    if (!s_Initialized || !s_GameActive)
    {
        return false;
    }

    // n02 expects non-const char*
    char* textBuf = new char[text.length() + 1];
    strcpy(textBuf, text.c_str());

    n02::chatSend(textBuf);

    delete[] textBuf;
    return true;
}

CORE_EXPORT bool CoreEndKailleraGame(void)
{
    if (!s_Initialized)
    {
        return false;
    }

    n02::endGame();

    s_GameActive = false;
    return true;
}

CORE_EXPORT void CoreMarkKailleraGameInactive(void)
{
    // Mark game as inactive without calling n02::endGame()
    // Used when the game ends due to network issues or another player dropping
    s_GameActive = false;
}

CORE_EXPORT void CoreSetKailleraCallbacks(
    CoreKaillera::GameStartCallback gameStartCallback,
    CoreKaillera::ChatReceivedCallback chatReceivedCallback,
    CoreKaillera::ClientDroppedCallback clientDroppedCallback,
    CoreKaillera::MoreInfosCallback moreInfosCallback)
{
    s_GameStartCallback = gameStartCallback;
    s_ChatReceivedCallback = chatReceivedCallback;
    s_ClientDroppedCallback = clientDroppedCallback;
    s_MoreInfosCallback = moreInfosCallback;

    // Set up kailleraInfos structure
    if (s_Initialized)
    {
        static kailleraInfos infos;

        // Allocate app name (must persist)
        static char appNameBuf[128];
        strncpy(appNameBuf, s_AppName.c_str(), sizeof(appNameBuf) - 1);
        appNameBuf[sizeof(appNameBuf) - 1] = '\0';
        infos.appName = appNameBuf;

        // Allocate game list (must persist)
        static char* gameListBuf = nullptr;
        if (gameListBuf)
        {
            delete[] gameListBuf;
        }
        gameListBuf = new char[s_GameList.length() + 1];
        memcpy(gameListBuf, s_GameList.c_str(), s_GameList.length() + 1);
        infos.gameList = gameListBuf;

        // Set callbacks
        infos.gameCallback = GameCallbackBridge;
        infos.chatReceivedCallback = s_ChatReceivedCallback ? ChatReceivedCallbackBridge : nullptr;
        infos.clientDroppedCallback = s_ClientDroppedCallback ? ClientDroppedCallbackBridge : nullptr;
        infos.moreInfosCallback = s_MoreInfosCallback ? MoreInfosCallbackBridge : nullptr;

        n02::setInfos(&infos);
    }
}

CORE_EXPORT bool CoreSetKailleraAppInfo(std::string appName, std::string gameList)
{
    s_AppName = appName;
    s_GameList = gameList;

    // Update kailleraInfos if already initialized
    if (s_Initialized)
    {
        // Trigger callback update which will also update app info
        CoreSetKailleraCallbacks(
            s_GameStartCallback,
            s_ChatReceivedCallback,
            s_ClientDroppedCallback,
            s_MoreInfosCallback
        );
    }

    return true;
}

CORE_EXPORT void CoreSetKailleraPlayerNumber(int playerNumber)
{
    s_PlayerNumber = playerNumber;
}

CORE_EXPORT int CoreGetKailleraPlayerNumber(void)
{
    return s_PlayerNumber;
}

CORE_EXPORT int CoreGetKailleraNumPlayers(void)
{
    return s_NumPlayers;
}

CORE_EXPORT int CoreGetKailleraFrameDelay(void)
{
    if (!s_Initialized || !s_GameActive)
    {
        return 0;
    }

    return n02::getFrameDelay();
}

#else // !_WIN32

// Stub implementations for non-Windows platforms
// Kaillera is Windows-only

CORE_EXPORT bool CoreInitKaillera(void)
{
    CoreSetError("Kaillera is only supported on Windows");
    return false;
}

CORE_EXPORT bool CoreShutdownKaillera(void)
{
    return true;
}

CORE_EXPORT bool CoreHasInitKaillera(void)
{
    return false;
}

CORE_EXPORT bool CoreShowKailleraServerDialog(void* parentHwnd)
{
    (void)parentHwnd;
    CoreSetError("Kaillera is only supported on Windows");
    return false;
}

CORE_EXPORT int CoreModifyKailleraPlayValues(void* values, int size)
{
    (void)values;
    (void)size;
    return -1;
}

CORE_EXPORT bool CoreKailleraSendChat(std::string text)
{
    (void)text;
    return false;
}

CORE_EXPORT bool CoreEndKailleraGame(void)
{
    return true;
}

CORE_EXPORT void CoreMarkKailleraGameInactive(void)
{
    // No-op on non-Windows
}

CORE_EXPORT void CoreSetKailleraCallbacks(
    CoreKaillera::GameStartCallback gameStartCallback,
    CoreKaillera::ChatReceivedCallback chatReceivedCallback,
    CoreKaillera::ClientDroppedCallback clientDroppedCallback,
    CoreKaillera::MoreInfosCallback moreInfosCallback)
{
    (void)gameStartCallback;
    (void)chatReceivedCallback;
    (void)clientDroppedCallback;
    (void)moreInfosCallback;
}

CORE_EXPORT bool CoreSetKailleraAppInfo(std::string appName, std::string gameList)
{
    (void)appName;
    (void)gameList;
    return false;
}

CORE_EXPORT void CoreSetKailleraPlayerNumber(int playerNumber)
{
    (void)playerNumber;
}

CORE_EXPORT int CoreGetKailleraPlayerNumber(void)
{
    return 0;
}

CORE_EXPORT int CoreGetKailleraNumPlayers(void)
{
    return 0;
}

CORE_EXPORT int CoreGetKailleraFrameDelay(void)
{
    return 0;
}

#endif // _WIN32
