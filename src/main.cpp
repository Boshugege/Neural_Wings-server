#include "GameServer.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstdlib>

// ── Graceful shutdown ──────────────────────────────────────────────
static GameServer *g_server = nullptr;

#ifdef _WIN32
#include <windows.h>
static BOOL WINAPI ConsoleHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        std::cout << "\n[Server] Shutting down...\n";
        if (g_server)
            g_server->Stop();
    }
    return TRUE;
}
#else
static void SignalHandler(int /*sig*/)
{
    std::cout << "\n[Server] Shutting down...\n";
    if (g_server)
        g_server->Stop();
}
#endif

// ── Entry point ────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    uint16_t port = DEFAULT_SERVER_PORT;

    // Allow overriding port via command-line: server.exe <port>
    if (argc > 1)
        port = static_cast<uint16_t>(std::atoi(argv[1]));

    GameServer server;
    g_server = &server;

    // Register Ctrl-C handler.
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#else
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
#endif

    if (!server.Start(port))
    {
        std::cerr << "[Server] Failed to start on port " << port << "\n";
        return 1;
    }

    std::cout << "[Server] Running on port " << port
              << ". Press Ctrl+C to stop.\n";

    // ── Main tick loop (60 Hz) ─────────────────────────────────────
    using clock = std::chrono::steady_clock;
    constexpr auto TICK_INTERVAL =
        std::chrono::duration_cast<clock::duration>(
            std::chrono::milliseconds(16)); // ~60 ticks/s

    while (server.IsRunning())
    {
        auto tickStart = clock::now();

        server.Tick();

        auto elapsed = clock::now() - tickStart;
        if (elapsed < TICK_INTERVAL)
            std::this_thread::sleep_for(TICK_INTERVAL - elapsed);
    }

    std::cout << "[Server] Exited cleanly.\n";
    return 0;
}
