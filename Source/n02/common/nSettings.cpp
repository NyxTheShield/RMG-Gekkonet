/*
 * n02 - Open Kaillera Client
 * Settings storage - stub implementation
 * Phase 3 will replace with RMG-K CoreSettings delegation
 */
#include "nSettings.h"

#include <cstring>
#include <cstdio>
#include <map>
#include <string>

// Simple in-memory storage as a stub
// Phase 3 will replace with CoreSettingsSetValue/CoreSettingsGetIntValue
static std::map<std::string, std::string> s_settings;
static std::string s_currentSection;

void nSettings::Initialize(char * submo, bool global) {
    s_currentSection = submo ? submo : "p2p";
    (void)global;
}

void nSettings::Terminate() {
    // No-op
}

int nSettings::get_int(char * key, int def_) {
    std::string fullKey = s_currentSection + "/" + (key ? key : "");
    auto it = s_settings.find(fullKey);
    if (it != s_settings.end()) {
        return atoi(it->second.c_str());
    }
    return def_;
}

char* nSettings::get_str(char * key, char * buf, char * def_) {
    std::string fullKey = s_currentSection + "/" + (key ? key : "");
    auto it = s_settings.find(fullKey);
    if (it != s_settings.end()) {
        strncpy(buf, it->second.c_str(), 127);
        buf[127] = 0;
        return buf;
    }
    if (def_) {
        strncpy(buf, def_, 127);
        buf[127] = 0;
    } else {
        buf[0] = 0;
    }
    return buf;
}

void nSettings::set_int(char * key, int val) {
    std::string fullKey = s_currentSection + "/" + (key ? key : "");
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    s_settings[fullKey] = buf;
}

void nSettings::set_str(char * key, char * val) {
    std::string fullKey = s_currentSection + "/" + (key ? key : "");
    s_settings[fullKey] = val ? val : "";
}
