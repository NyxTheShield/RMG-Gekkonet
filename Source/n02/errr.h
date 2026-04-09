/*
 * n02 - Open Kaillera Client
 * Error handling and logging (platform-independent)
 */
#pragma once

void _n02_TRACE(char * file, int line);

#define n02_TRACE() _n02_TRACE(__FILE__, __LINE__)
