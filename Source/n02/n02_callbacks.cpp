/*
 * n02 - Open Kaillera Client
 * Callback stub implementations
 *
 * These functions are called by p2p_core.cpp and kaillera_core.cpp.
 * They were originally implemented in p2p_ui.cpp and kaillera_ui.cpp (Win32 dialogs).
 * Now they route through the n02::UICallbacks interface to the Qt UI layer.
 */

#include "n02_client.h"
#include "kailleraclient.h"
#include "kcore/kaillera_core.h"
#include "core/p2p_core.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>

// Defined in n02_client.cpp
extern kailleraInfos infos_copy;

///////////////////////////////////////////////////////////////////////////////
// P2P Core Callbacks
///////////////////////////////////////////////////////////////////////////////

void p2p_chat_callback(char * nick, char * msg) {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pChatCallback)
        cb.p2pChatCallback(nick, msg);
}

void p2p_game_callback(char * game, int player, int maxplayers) {
    // Set global state for the KSSDFA state machine
    strncpy(GAME, (game != NULL) ? game : "", sizeof(GAME) - 1);
    GAME[sizeof(GAME) - 1] = 0;
    playerno = player;
    numplayers = maxplayers;

    // Signal KSSDFA to transition to game state
    KSSDFA.input = KSSDFA_START_GAME;

    auto& cb = n02::getUICallbacks();
    if (cb.p2pGameCallback)
        cb.p2pGameCallback(game, player, maxplayers);
}

void p2p_end_game_callback() {
    // Signal KSSDFA to transition out of game state
    KSSDFA.input = KSSDFA_END_GAME;
    KSSDFA.state = 0;

    auto& cb = n02::getUICallbacks();
    if (cb.p2pEndGameCallback)
        cb.p2pEndGameCallback();
}

void p2p_client_dropped_callback(char * nick, int player) {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pClientDroppedCallback)
        cb.p2pClientDroppedCallback(nick, player);
}

void __cdecl p2p_core_debug(char * arg_0, ...) {
    char V88[2048];
    va_list args;
    va_start(args, arg_0);
    V88[0] = 0;
    if (arg_0 != NULL)
        vsnprintf(V88, sizeof(V88), arg_0, args);
    va_end(args);

    auto& cb = n02::getUICallbacks();
    if (cb.p2pDebugCallback)
        cb.p2pDebugCallback(V88);
}

void p2p_hosted_game_callback(char * game) {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pHostedGameCallback)
        cb.p2pHostedGameCallback(game);
}

void p2p_ping_callback(int PING) {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pPingCallback)
        cb.p2pPingCallback(PING);
}

void p2p_peer_left_callback() {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pPeerLeftCallback)
        cb.p2pPeerLeftCallback();
}

void p2p_peer_joined_callback() {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pPeerJoinedCallback)
        cb.p2pPeerJoinedCallback();
}

void p2p_peer_info_callback(char* p33rname, char* app) {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pPeerInfoCallback)
        cb.p2pPeerInfoCallback(p33rname, app);
}

int p2p_getSelectedDelay() {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pGetSelectedDelayCallback)
        return cb.p2pGetSelectedDelayCallback();
    return 0; // Auto delay
}

void p2p_ssrv_packet_recv_callback(char *cmd, int len, void*sadr) {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pSsrvPacketRecvCallback)
        cb.p2pSsrvPacketRecvCallback(cmd, len, sadr);
}

void p2p_fodipp_callback(char * host) {
    auto& cb = n02::getUICallbacks();
    if (cb.p2pFodippCallback)
        cb.p2pFodippCallback(host);
}

// p2p_EndGame is called as a module function - delegate to p2p_drop_game
void p2p_EndGame() {
    extern void p2p_drop_game();
    p2p_drop_game();
}

///////////////////////////////////////////////////////////////////////////////
// Kaillera Core Callbacks
///////////////////////////////////////////////////////////////////////////////

void kaillera_user_add_callback(char*name, int ping, int status, unsigned short id, char conn) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraUserAddCallback)
        cb.kailleraUserAddCallback(name, ping, status, id, conn);
}

void kaillera_game_add_callback(char*gname, unsigned int id, char*emulator, char*owner, char*users, char status) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraGameAddCallback)
        cb.kailleraGameAddCallback(gname, id, emulator, owner, users, status);
}

void kaillera_chat_callback(char*name, char * msg) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraChatCallback)
        cb.kailleraChatCallback(name, msg);
}

void kaillera_game_chat_callback(char*name, char * msg) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraGameChatCallback)
        cb.kailleraGameChatCallback(name, msg);
}

void kaillera_motd_callback(char*name, char * msg) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraMotdCallback)
        cb.kailleraMotdCallback(name, msg);
}

void kaillera_user_join_callback(char*name, int ping, unsigned short id, char conn) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraUserJoinCallback)
        cb.kailleraUserJoinCallback(name, ping, id, conn);
}

void kaillera_user_leave_callback(char*name, char*quitmsg, unsigned short id) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraUserLeaveCallback)
        cb.kailleraUserLeaveCallback(name, quitmsg, id);
}

void kaillera_game_create_callback(char*gname, unsigned int id, char*emulator, char*owner) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraGameCreateCallback)
        cb.kailleraGameCreateCallback(gname, id, emulator, owner);
}

void kaillera_game_close_callback(unsigned int id) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraGameCloseCallback)
        cb.kailleraGameCloseCallback(id);
}

void kaillera_game_status_change_callback(unsigned int id, char status, int players, int maxplayers) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraGameStatusChangeCallback)
        cb.kailleraGameStatusChangeCallback(id, status, players, maxplayers);
}

void kaillera_user_game_create_callback() {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraUserGameCreateCallback)
        cb.kailleraUserGameCreateCallback();
}

void kaillera_user_game_joined_callback() {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraUserGameJoinedCallback)
        cb.kailleraUserGameJoinedCallback();
}

void kaillera_user_game_closed_callback() {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraUserGameClosedCallback)
        cb.kailleraUserGameClosedCallback();
}

void kaillera_user_game_close_callback() {
    // Same as closed callback in the original
    kaillera_user_game_closed_callback();
}

void kaillera_player_add_callback(char *name, int ping, unsigned short id, char conn) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraPlayerAddCallback)
        cb.kailleraPlayerAddCallback(name, ping, id, conn);
}

void kaillera_player_joined_callback(char * username, int ping, unsigned short uid, char connset) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraPlayerJoinedCallback)
        cb.kailleraPlayerJoinedCallback(username, ping, uid, connset);
}

void kaillera_player_left_callback(char * user, unsigned short id) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraPlayerLeftCallback)
        cb.kailleraPlayerLeftCallback(user, id);
}

void kaillera_user_kicked_callback() {
    KSSDFA.input = KSSDFA_END_GAME;
    KSSDFA.state = 0;

    auto& cb = n02::getUICallbacks();
    if (cb.kailleraUserKickedCallback)
        cb.kailleraUserKickedCallback();
}

void kaillera_login_stat_callback(char*lsmsg) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraLoginStatCallback)
        cb.kailleraLoginStatCallback(lsmsg);
}

void kaillera_player_dropped_callback(char * user, int gdpl) {
    // Notify the emulator that a player dropped
    if (infos_copy.clientDroppedCallback)
        infos_copy.clientDroppedCallback(user, gdpl);

    // If we were the one dropped, end the game
    if (gdpl == playerno) {
        KSSDFA.input = KSSDFA_END_GAME;
        KSSDFA.state = 0;
    }

    auto& cb = n02::getUICallbacks();
    if (cb.kailleraPlayerDroppedCallback)
        cb.kailleraPlayerDroppedCallback(user, gdpl);
}

void kaillera_game_callback(char * game, char player, char players) {
    // Set global state for the KSSDFA state machine
    if (game) {
        strncpy(GAME, game, sizeof(GAME) - 1);
        GAME[sizeof(GAME) - 1] = 0;
    }
    playerno = player;
    numplayers = players;

    // Signal KSSDFA to transition to game state
    KSSDFA.input = KSSDFA_START_GAME;

    auto& cb = n02::getUICallbacks();
    if (cb.kailleraGameStartCallback)
        cb.kailleraGameStartCallback(game, player, players);
}

void kaillera_game_netsync_wait_callback(int tx) {
    auto& cb = n02::getUICallbacks();
    if (cb.kailleraGameNetsyncWaitCallback)
        cb.kailleraGameNetsyncWaitCallback(tx);
}

void kaillera_end_game_callback() {
    // Signal KSSDFA to transition out of game state
    KSSDFA.input = KSSDFA_END_GAME;
    KSSDFA.state = 0;

    auto& cb = n02::getUICallbacks();
    if (cb.kailleraEndGameCallback)
        cb.kailleraEndGameCallback();
}

void __cdecl kaillera_core_debug(char * arg_0, ...) {
    char V88[2048];
    va_list args;
    va_start(args, arg_0);
    V88[0] = 0;
    if (arg_0 != NULL)
        vsnprintf(V88, sizeof(V88), arg_0, args);
    va_end(args);

    auto& cb = n02::getUICallbacks();
    if (cb.kailleraDebugCallback)
        cb.kailleraDebugCallback(V88);
}

void __cdecl kaillera_error_callback(char * arg_0, ...) {
    char V88[2048];
    va_list args;
    va_start(args, arg_0);
    V88[0] = 0;
    if (arg_0 != NULL)
        vsnprintf(V88, sizeof(V88), arg_0, args);
    va_end(args);

    auto& cb = n02::getUICallbacks();
    if (cb.kailleraErrorCallback)
        cb.kailleraErrorCallback(V88);
}

// Kaillera step function wrapper used by n02_client.cpp
bool kaillera_SelectServerDlgStep() {
    extern void kaillera_step();
    extern bool kaillera_core_initialized;
    if (kaillera_core_initialized)
        kaillera_step();
    return true;
}

// P2P step function wrapper
bool p2p_SelectServerDlgStep() {
    extern void p2p_step();
    extern bool p2p_core_initialized;
    if (p2p_core_initialized)
        p2p_step();
    return true;
}

// FODIPP (Find Out Destination IP P2P) - stub
// Was originally in p2p_ui.cpp; called from UI to discover external IP.
// Will be reimplemented when Qt UI is added.
void p2p_fodipp() {
    // No-op stub - Qt UI will implement external IP discovery
}
