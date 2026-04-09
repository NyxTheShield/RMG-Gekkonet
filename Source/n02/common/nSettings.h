/*
 * n02 - Open Kaillera Client
 * Settings storage abstraction
 * Rewritten: stub implementation for Phase 1
 * Phase 3 will delegate to RMG-K CoreSettings
 */
#pragma once

class nSettings {
public:
    static void Initialize(char * submo = "p2p", bool global = false);
    static void Terminate();
    static int get_int(char * key, int def_ = -1);
    static char* get_str(char * key, char * buf, char * def_ = 0);
    static void set_int(char * key, int val);
    static void set_str(char * key, char * val);
};
