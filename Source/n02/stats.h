/*
 * n02 - Open Kaillera Client
 * Network statistics tracking
 */
#pragma once

extern int PACKETLOSSCOUNT;
extern int PACKETMISOTDERCOUNT;
extern int PACKETINCDSCCOUNT;
extern int PACKETIADSCCOUNT;
extern int SOCK_RECV_PACKETS;
extern int SOCK_RECV_BYTES;
extern int SOCK_RECV_RETR;
extern int SOCK_SEND_PACKETS;
extern int SOCK_SEND_BYTES;
extern int SOCK_SEND_RETR;

void StatsDisplayThreadBegin();
void StatsDisplayThreadEnd();

void StatsAppendLine(const char* fmt, ...);
void StatsClearExtra();
