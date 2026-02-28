/*
 * n02 - Open Kaillera Client
 * Network statistics tracking
 * Rewritten: removed Win32 dialog, kept stat counters and thread-safe buffer
 */
#include "stats.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <mutex>

int PACKETLOSSCOUNT;
int PACKETMISOTDERCOUNT;
int PACKETINCDSCCOUNT;
int PACKETIADSCCOUNT;

int SOCK_RECV_PACKETS;
int SOCK_RECV_BYTES;
int SOCK_RECV_RETR;
int SOCK_SEND_PACKETS;
int SOCK_SEND_BYTES;
int SOCK_SEND_RETR;

int SOCK_SEND_PPS;
int GAME_FPS;

static std::mutex g_stats_mutex;
static char g_stats_extra[4096];
static size_t g_stats_extra_len = 0;

void StatsAppendLine(const char* fmt, ...) {
    if (fmt == nullptr || *fmt == 0)
        return;

    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    const size_t line_len = strlen(line);
    if (line_len == 0)
        return;

    std::lock_guard<std::mutex> lock(g_stats_mutex);

    const size_t needed = line_len + 2; // + "\r\n"
    if (needed >= sizeof(g_stats_extra)) {
        size_t copy_len = sizeof(g_stats_extra) - 3;
        memcpy(g_stats_extra, line + (line_len - copy_len), copy_len);
        g_stats_extra[copy_len] = '\r';
        g_stats_extra[copy_len + 1] = '\n';
        g_stats_extra[copy_len + 2] = 0;
        g_stats_extra_len = copy_len + 2;
    } else {
        if (g_stats_extra_len + needed >= sizeof(g_stats_extra)) {
            size_t overflow = (g_stats_extra_len + needed) - (sizeof(g_stats_extra) - 1);
            if (overflow >= g_stats_extra_len) {
                g_stats_extra_len = 0;
            } else {
                memmove(g_stats_extra, g_stats_extra + overflow, g_stats_extra_len - overflow);
                g_stats_extra_len -= overflow;
            }
        }
        memcpy(g_stats_extra + g_stats_extra_len, line, line_len);
        g_stats_extra_len += line_len;
        g_stats_extra[g_stats_extra_len++] = '\r';
        g_stats_extra[g_stats_extra_len++] = '\n';
        g_stats_extra[g_stats_extra_len] = 0;
    }
}

void StatsClearExtra() {
    std::lock_guard<std::mutex> lock(g_stats_mutex);
    g_stats_extra_len = 0;
    g_stats_extra[0] = 0;
}

// Stats display - stub (Qt UI will handle display in Phase 5)
void StatsDisplayThreadBegin() {
    // No-op - Qt UI will display stats
}

void StatsDisplayThreadEnd() {
    // No-op
}
