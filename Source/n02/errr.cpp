/*
 * n02 - Open Kaillera Client
 * Error handling and logging (platform-independent)
 * Rewritten: removed Win32 MessageBox/DialogBox, SEH exception dialog
 */
#include "errr.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <climits>

// Logging function - writes to file and stdout
void __cdecl kprintf(char * arg_0, ...) {
    char V88[2048];
    va_list args;
    va_start(args, arg_0);
    V88[0] = 0;
    if (arg_0 != NULL)
        vsnprintf(V88, sizeof(V88), arg_0, args);
    va_end(args);

    // Log to file
    static FILE* logFile = nullptr;
    if (logFile == nullptr) {
        logFile = fopen("keaa.txt", "w");
    }
    if (logFile != nullptr) {
        fputs(V88, logFile);
        fflush(logFile);
    }
}

// Trace stack for debugging
typedef struct {
    char * file;
    int line;
} n02_TRACE_st;

#define n02_TRACE_stack_len 16
#define n02_TRACE_stack_mask 15

n02_TRACE_st n02_TRACE_stack[n02_TRACE_stack_len];
unsigned int n02_TRACE_stack_pos = 0;

void _n02_TRACE(char * file, int line) {
    n02_TRACE_stack[n02_TRACE_stack_pos & n02_TRACE_stack_mask].file = file;
    n02_TRACE_stack[n02_TRACE_stack_pos & n02_TRACE_stack_mask].line = line;
    n02_TRACE_stack_pos++;
}
