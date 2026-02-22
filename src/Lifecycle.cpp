// ────────────────────────────────────────────────────────────────────
// GameServer lifecycle
// ────────────────────────────────────────────────────────────────────

extern "C"
{
#include <nbnet.h>
#include <net_drivers/udp.h>
#if defined(NW_ENABLE_WEBRTC_C)
#include <net_drivers/webrtc_c.h>
#endif
}

#include "GameServer.h"

static constexpr const char *NW_PROTOCOL_NAME = "neural_wings";

GameServer::~GameServer()
{
    Stop();
}

bool GameServer::Start(uint16_t port)
{
    // Register drivers BEFORE starting.
    // NBN_Driver_Register asserts the driver isn't already registered,
    // and NBN_GameServer_Stop does NOT unregister drivers, so we must
    // only register once per process lifetime.
    static bool s_driverRegistered = false;
    if (!s_driverRegistered)
    {
        NBN_UDP_Register();
#if defined(NW_ENABLE_WEBRTC_C)
        NBN_WebRTC_C_Config wrtcCfg{};
        wrtcCfg.enable_tls = false;
        wrtcCfg.cert_path = nullptr;
        wrtcCfg.key_path = nullptr;
        wrtcCfg.passphrase = nullptr;
        wrtcCfg.ice_servers = nullptr;
        wrtcCfg.ice_servers_count = 0;
        wrtcCfg.log_level = RTC_LOG_WARNING;
        NBN_WebRTC_C_Register(wrtcCfg);
#endif
        s_driverRegistered = true;
    }

    if (NBN_GameServer_StartEx(NW_PROTOCOL_NAME, port, false) < 0)
    {
        std::cerr << "[GameServer] NBN_GameServer_StartEx failed on port "
                  << port << "\n";
        return false;
    }

    m_running = true;
    m_serverTick = 0;
    std::cout << "[GameServer] Started on port " << port
              << " (client timeout " << m_clientTimeout.count() << " ms)\n";
    return true;
}

void GameServer::Stop()
{
    if (!m_running)
        return;

    m_running = false;

    NBN_GameServer_Stop();

    m_connIndex.clear();
    m_nicknameIndex.clear();
    m_clients.clear();
    std::cout << "[GameServer] Stopped\n";
}

void GameServer::Tick()
{
    if (!m_running)
        return;
    ++m_serverTick;

    // 1. Poll all network events
    int ev;
    while ((ev = NBN_GameServer_Poll()) != NBN_NO_EVENT)
    {
        if (ev < 0)
        {
            std::cerr << "[GameServer] Poll error\n";
            break;
        }

        switch (ev)
        {
        case NBN_NEW_CONNECTION:
            HandleNewConnection();
            break;
        case NBN_CLIENT_DISCONNECTED:
            HandleClientDisconnected();
            break;
        case NBN_CLIENT_MESSAGE_RECEIVED:
            HandleClientMessage();
            break;
        }
    }

    RemoveTimedOutClients();

    // 2. Broadcast game state
    BroadcastPositions();

    // 3. Flush outgoing packets to all clients
    if (NBN_GameServer_SendPackets() < 0)
    {
        std::cerr << "[GameServer] SendPackets failed\n";
    }
}
