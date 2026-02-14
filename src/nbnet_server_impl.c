// ────────────────────────────────────────────────────────────────────
// nbnet server implementation – compiled as C
//
// This .c file compiles the nbnet implementation (NBNET_IMPL) and the
// UDP driver.  It MUST be compiled as C (not C++) because nbnet uses
// C99 designated initializers that MSVC rejects in C++17.
//
// The corresponding C++ code (GameServer.cpp) includes nbnet.h inside
// extern "C" { } for declaration-only access.
// ────────────────────────────────────────────────────────────────────

#include <stdio.h>

// nbnet logging
#define NBN_LogInfo(...)    do { printf("[nbnet INFO] ");    printf(__VA_ARGS__); printf("\n"); } while(0)
#define NBN_LogError(...)   do { fprintf(stderr, "[nbnet ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define NBN_LogWarning(...) do { printf("[nbnet WARN] ");    printf(__VA_ARGS__); printf("\n"); } while(0)
#define NBN_LogDebug(...)   (void)0
#define NBN_LogTrace(...)   (void)0

#define NBNET_IMPL

#include <nbnet.h>
#include <net_drivers/udp.h>
#if defined(NW_ENABLE_WEBRTC_C)
#include <net_drivers/webrtc_c.h>
#endif
