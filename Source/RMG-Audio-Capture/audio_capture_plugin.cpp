/*
 * Minimal mupen64plus audio plugin for Kaillera replay export.
 * Captures raw PCM to a temporary file without speaker output.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#define CALL __cdecl
#else
#define EXPORT __attribute__((visibility("default")))
#define CALL
#endif

typedef void* m64p_dynlib_handle;

typedef enum
{
    M64PLUGIN_AUDIO = 3
} m64p_plugin_type;

typedef enum
{
    M64ERR_SUCCESS = 0,
    M64ERR_NOT_INIT,
    M64ERR_ALREADY_INIT,
    M64ERR_INCOMPATIBLE,
    M64ERR_INPUT_ASSERT,
    M64ERR_INPUT_INVALID,
    M64ERR_INPUT_NOT_FOUND,
    M64ERR_NO_MEMORY,
    M64ERR_FILES,
    M64ERR_INTERNAL,
    M64ERR_INVALID_STATE,
    M64ERR_PLUGIN_FAIL,
    M64ERR_SYSTEM_FAIL,
    M64ERR_UNSUPPORTED,
    M64ERR_WRONG_TYPE
} m64p_error;

typedef enum
{
    SYSTEM_NTSC = 0,
    SYSTEM_PAL,
    SYSTEM_MPAL
} m64p_system_type;

typedef struct
{
    unsigned char* RDRAM;
    unsigned char* DMEM;
    unsigned char* IMEM;
    unsigned int* MI_INTR_REG;
    unsigned int* AI_DRAM_ADDR_REG;
    unsigned int* AI_LEN_REG;
    unsigned int* AI_CONTROL_REG;
    unsigned int* AI_STATUS_REG;
    unsigned int* AI_DACRATE_REG;
    unsigned int* AI_BITRATE_REG;
    void (*CheckInterrupts)(void);
} AUDIO_INFO;

static bool s_Initialized = false;
static AUDIO_INFO s_AudioInfo = {};
static FILE* s_OutputFile = nullptr;
static char s_OutputPath[1024] = {};
static unsigned int s_Frequency = 33600;
static unsigned long long s_BytesWritten = 0;

extern "C"
{

EXPORT void CALL audio_capture_set_output(const char* path)
{
    if (path == nullptr)
    {
        s_OutputPath[0] = '\0';
        return;
    }

    strncpy(s_OutputPath, path, sizeof(s_OutputPath) - 1);
    s_OutputPath[sizeof(s_OutputPath) - 1] = '\0';
}

EXPORT unsigned int CALL audio_capture_get_frequency(void)
{
    return s_Frequency;
}

EXPORT unsigned long long CALL audio_capture_get_bytes_written(void)
{
    return s_BytesWritten;
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle, void*,
                                     void (*)(void*, int, const char*))
{
    if (s_Initialized)
    {
        return M64ERR_ALREADY_INIT;
    }

    s_Initialized = true;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!s_Initialized)
    {
        return M64ERR_NOT_INIT;
    }

    s_Initialized = false;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type* pluginType,
                                        int* pluginVersion,
                                        int* apiVersion,
                                        const char** pluginNamePtr,
                                        int* capabilities)
{
    if (pluginType != nullptr)
    {
        *pluginType = M64PLUGIN_AUDIO;
    }
    if (pluginVersion != nullptr)
    {
        *pluginVersion = 0x010000;
    }
    if (apiVersion != nullptr)
    {
        *apiVersion = 0x020000;
    }
    if (pluginNamePtr != nullptr)
    {
        *pluginNamePtr = "RMG Audio Capture";
    }
    if (capabilities != nullptr)
    {
        *capabilities = 0;
    }

    return M64ERR_SUCCESS;
}

EXPORT int CALL InitiateAudio(AUDIO_INFO audioInfo)
{
    s_AudioInfo = audioInfo;
    return 1;
}

EXPORT int CALL RomOpen(void)
{
    s_BytesWritten = 0;

    if (s_OutputPath[0] != '\0')
    {
        s_OutputFile = fopen(s_OutputPath, "wb");
        if (s_OutputFile == nullptr)
        {
            return 0;
        }
    }

    return 1;
}

EXPORT void CALL RomClosed(void)
{
    if (s_OutputFile != nullptr)
    {
        fclose(s_OutputFile);
        s_OutputFile = nullptr;
    }
}

EXPORT void CALL AiDacrateChanged(int systemType)
{
    unsigned int viClock;
    switch (systemType)
    {
    default:
    case SYSTEM_NTSC:
        viClock = 48681812;
        break;
    case SYSTEM_PAL:
        viClock = 49656530;
        break;
    case SYSTEM_MPAL:
        viClock = 48628316;
        break;
    }

    const unsigned int dacRate = *s_AudioInfo.AI_DACRATE_REG;
    s_Frequency = viClock / (dacRate + 1);
}

EXPORT void CALL AiLenChanged(void)
{
    if (s_OutputFile == nullptr || s_AudioInfo.RDRAM == nullptr)
    {
        return;
    }

    const unsigned int address = *s_AudioInfo.AI_DRAM_ADDR_REG & 0xFFFFFF;
    const unsigned int length = *s_AudioInfo.AI_LEN_REG;
    if (length == 0)
    {
        return;
    }

    const uint8_t* source = s_AudioInfo.RDRAM + address;
    const unsigned int sampleCount = length / 4;

    for (unsigned int i = 0; i < sampleCount; ++i)
    {
        uint8_t output[4];
        output[0] = source[i * 4 + 2];
        output[1] = source[i * 4 + 3];
        output[2] = source[i * 4 + 0];
        output[3] = source[i * 4 + 1];
        fwrite(output, 1, sizeof(output), s_OutputFile);
    }

    s_BytesWritten += static_cast<unsigned long long>(sampleCount) * 4ULL;
}

EXPORT void CALL ProcessAList(void) {}
EXPORT void CALL SetSpeedFactor(int) {}
EXPORT void CALL VolumeUp(void) {}
EXPORT void CALL VolumeDown(void) {}
EXPORT int CALL VolumeGetLevel(void) { return 100; }
EXPORT void CALL VolumeSetLevel(int) {}
EXPORT void CALL VolumeMute(void) {}
EXPORT const char* CALL VolumeGetString(void) { return "100%"; }

} // extern "C"
