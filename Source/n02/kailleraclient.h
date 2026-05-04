/*
 * n02 - Open Kaillera Client
 * Internal header (shared between n02 modules)
 */
#pragma once

// __cdecl is a Windows calling convention, define it as empty on other platforms
#ifndef _WIN32
#ifndef __cdecl
#define __cdecl
#endif
#endif

#include "common/k_framecache.h"

extern int PACKETLOSSCOUNT;

typedef struct {
    unsigned state:2;
    unsigned input:2;
} KSSDFA_;

extern KSSDFA_ KSSDFA;

#define KSSDFA_START_GAME 2
#define KSSDFA_END_GAME 1

/* KSSDFAST:
 + | 00 | 01 | 11 | 10 |
---+----+----+----+----|
00 | 00 | 00 | 01 | 01 |
01 | 01 | 00 | 00 | 01 |
11 | 11 | 11 | 11 | 11 |
10 | 10 | 00 | 00 | 10 |
*/

extern int KSSDFAST[16];

void __cdecl kprintf(char * arg_0, ...);
extern char * gamelist;

// kailleraInfos struct - callbacks use plain C calling convention (no __stdcall)
struct kailleraInfos {
    char *appName;
    char *gameList;
    int (*gameCallback)(char *game, int player, int numplayers);
    void (*chatReceivedCallback)(char *nick, char *text);
    void (*clientDroppedCallback)(char *nick, int playernb);
    void (*moreInfosCallback)(char *gamename);
};

extern kailleraInfos infos;

extern char GAME[128];
extern char APP[128];
extern int playerno;
extern int numplayers;

bool activate_mode(int mode);
int get_active_mode_index();

extern char recording_player_names[4][32];

// Spoof ping and 30fps mode settings (set by UI, used by cores)
extern int kaillera_spoof_ping;
extern int kaillera_30fps_mode;

extern int p2p_30fps_mode;
