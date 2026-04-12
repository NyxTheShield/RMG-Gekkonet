#pragma once

// Preprocessor stringification macros
#define N02_STRINGIFY(x) #x
#define N02_TOSTRING(x) N02_STRINGIFY(x)

// Use GIT_REVISION from CI build if available, otherwise use default
#ifdef GIT_REVISION
#define N02_VER N02_TOSTRING(GIT_REVISION)
#else
#define N02_VER "dev"
#endif

#define N02_COMP_VER "kaillera 0.9"
#define KAILLERA_VERSION N02_VER " (" N02_COMP_VER " compatible)"
#define N02_WINDOW_TITLE "N02 " N02_VER

int kaillera_ping_server(char * host, int port, int limit = 1000);
void kaillera_step();


void __cdecl kaillera_core_debug(char * arg_0, ...);
void __cdecl kaillera_error_callback(char * arg_0, ...);


int kaillera_get_frames_count();
int kaillera_get_delay();
unsigned short kaillera_get_user_id();

bool kaillera_is_connected();
bool kaillera_core_initialize(int port, char * appname, char * username, char connection_setting);
void kaillera_set_spoof_ping(int spoof_ping_ms);  // Call before connect: 0=auto, >0=spoof ping in ms
bool kaillera_core_connect(char * ip, int port = 27888);
bool kaillera_core_finish_login(int timeout_ms);
const char* kaillera_core_get_last_error();
bool kaillera_disconnect(char * quitmsg);
bool kaillera_core_cleanup();
int kaillera_core_get_port();
void kaillera_chat_send(char * text);
void kaillera_game_chat_send(char * text);
void kaillera_kick_user (unsigned short id);
void kaillera_join_game(unsigned int id, const char* gameName = nullptr);
void kaillera_create_game(char * name);
void kaillera_leave_game ();
void kaillera_start_game();
void kaillera_game_drop();
void kaillera_end_game();
int kaillera_modify_play_values (void * values, int size);
void kaillera_print_core_status();
bool kaillera_is_game_running();

void kaillera_user_add_callback(char*name, int ping, int status, unsigned short id, char conn);
void kaillera_game_add_callback(char*gname, unsigned int id, char*emulator, char*owner, char*users, char status);
void kaillera_chat_callback(char*name, char * msg);
void kaillera_game_chat_callback(char*name, char * msg);
void kaillera_motd_callback(char*name, char * msg);
void kaillera_user_join_callback(char*name, int ping, unsigned short id, char conn);
void kaillera_user_leave_callback(char*name, char*quitmsg, unsigned short id);
void kaillera_game_create_callback(char*gname, unsigned int id, char*emulator, char*owner);
void kaillera_user_game_close_callback();
void kaillera_game_close_callback(unsigned int id);
void kaillera_user_game_create_callback();
void kaillera_game_status_change_callback(unsigned int id, char status, int players, int maxplayers);
void kaillera_user_game_closed_callback();
void kaillera_user_game_close_callback();
void kaillera_user_game_closed_callback();
void kaillera_user_game_closed_callback();
void kaillera_user_game_joined_callback();
void kaillera_player_add_callback(char *name, int ping, unsigned short id, char conn);
void kaillera_player_joined_callback(char * username, int ping, unsigned short uid, char connset);
void kaillera_player_left_callback(char * user, unsigned short id);
void kaillera_user_kicked_callback();
void kaillera_login_stat_callback(char*lsmsg);
void kaillera_player_dropped_callback(char * user, int gdpl);
void kaillera_game_callback(char * game, char player, char players);
void kaillera_game_netsync_wait_callback(int tx);
void kaillera_end_game_callback();
