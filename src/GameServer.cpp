// ────────────────────────────────────────────────────────────────────
// GameServer  –  nbnet server C++ wrapper
//
// The actual nbnet implementation (NBNET_IMPL) is compiled as C in
// nbnet_server_impl.c.  This file only uses declaration-level access.
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
#include <algorithm>

// ── Constants ──────────────────────────────────────────────────────
static constexpr const char *NW_PROTOCOL_NAME = "neural_wings";

static uint8_t MapChannel(uint8_t ourChannel)
{
    // our convention: 0 = reliable, 1 = unreliable
    return (ourChannel == 0) ? NBN_CHANNEL_RESERVED_RELIABLE : NBN_CHANNEL_RESERVED_UNRELIABLE;
}

// ── Lifecycle ──────────────────────────────────────────────────────

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
    std::cout << "[GameServer] Started on port " << port << "\n";
    return true;
}

void GameServer::Stop()
{
    if (!m_running)
        return;

    m_running = false;

    NBN_GameServer_Stop();

    m_connIndex.clear();
    m_clients.clear();
    std::cout << "[GameServer] Stopped\n";
}

// ── Tick ───────────────────────────────────────────────────────────

void GameServer::Tick()
{
    if (!m_running)
        return;

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

// ── Connection events ──────────────────────────────────────────────

void GameServer::HandleNewConnection()
{
    NBN_ConnectionHandle conn = NBN_GameServer_GetIncomingConnection();

    // Always accept (authentication can be added later)
    NBN_GameServer_AcceptIncomingConnection();

    const ClientID newID = m_nextClientID++;

    ClientState state;
    state.id = newID;
    state.connHandle = conn;
    state.lastSeen = std::chrono::steady_clock::now();

    m_clients[newID] = state;
    m_connIndex[conn] = newID;

    std::cout << "[GameServer] Peer connected (awaiting Hello), assigned temp ClientID "
              << newID << "\n";
}

void GameServer::HandleClientDisconnected()
{
    NBN_ConnectionHandle conn = NBN_GameServer_GetDisconnectedClient();

    auto it = m_connIndex.find(conn);
    if (it == m_connIndex.end())
        return;

    RemoveClient(it->second, "disconnected");
}

void GameServer::HandleClientMessage()
{
    NBN_MessageInfo info = NBN_GameServer_GetMessageInfo();

    if (info.type != NBN_BYTE_ARRAY_MESSAGE_TYPE || !info.data)
        return;

    NBN_ByteArrayMessage *msg =
        static_cast<NBN_ByteArrayMessage *>(info.data);

    // Look up who sent it (info.sender is NBN_ConnectionHandle)
    auto it = m_connIndex.find(info.sender);
    if (it == m_connIndex.end())
        return;

    DispatchPacket(it->second, msg->bytes, msg->length);
}

// ── Packet dispatch ────────────────────────────────────────────────

void GameServer::DispatchPacket(ClientID clientID,
                                const uint8_t *data, size_t len)
{
    if (len < sizeof(NetPacketHeader))
        return;

    NetMessageType type = PacketSerializer::PeekType(data, len);
    switch (type)
    {
    case NetMessageType::ClientHello:
        HandleClientHello(clientID, data, len);
        break;
    case NetMessageType::PositionUpdate:
        HandlePositionUpdate(clientID, data, len);
        break;
    case NetMessageType::ClientDisconnect:
        HandleClientDisconnect(clientID);
        break;
    default:
        std::cerr << "[GameServer] Unknown message type "
                  << static_cast<int>(type) << "\n";
        break;
    }
}

// ── Handlers ───────────────────────────────────────────────────────

void GameServer::HandleClientHello(ClientID clientID,
                                   const uint8_t *data, size_t len)
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end() || it->second.welcomed)
        return;

    // Read UUID from the Hello packet
    auto hello = PacketSerializer::Read<MsgClientHello>(data, len);
    const NetUUID &uuid = hello.uuid;

    if (!uuid.IsNull())
    {
        // Check if this UUID was seen before (returning player)
        auto uuidIt = m_uuidIndex.find(uuid);
        if (uuidIt != m_uuidIndex.end())
        {
            // Returning player — reuse the old ClientID
            ClientID oldID = uuidIt->second;
            std::cout << "[GameServer] Returning player UUID recognised, "
                      << "reusing ClientID " << oldID << "\n";

            // Update the state to use the old ClientID
            ClientState &cs = it->second;
            cs.id = oldID;
            cs.uuid = uuid;
            cs.welcomed = true;
            cs.lastSeen = std::chrono::steady_clock::now();

            // Re-index: move state from temp clientID to old clientID
            ClientState movedState = cs;
            m_clients.erase(it);
            m_clients[oldID] = movedState;
            m_connIndex[movedState.connHandle] = oldID;

            SendWelcome(oldID);
            return;
        }

        // New player — register UUID
        it->second.uuid = uuid;
        m_uuidIndex[uuid] = clientID;
        std::cout << "[GameServer] New player UUID registered, ClientID "
                  << clientID << "\n";
    }

    it->second.welcomed = true;
    it->second.lastSeen = std::chrono::steady_clock::now();
    SendWelcome(clientID);
    std::cout << "[GameServer] Assigned ClientID " << clientID << "\n";
}

void GameServer::HandlePositionUpdate(ClientID clientID,
                                      const uint8_t *data, size_t len)
{
    auto msg = PacketSerializer::Read<MsgPositionUpdate>(data, len);

    auto it = m_clients.find(clientID);
    if (it == m_clients.end())
        return;

    it->second.objectID = msg.objectID;
    it->second.lastTransform = msg.transform;
    it->second.hasTransform = true;
    it->second.lastSeen = std::chrono::steady_clock::now();
}

void GameServer::HandleClientDisconnect(ClientID clientID)
{
    RemoveClient(clientID, "requested disconnect");
}

// ── Sending helpers ────────────────────────────────────────────────

void GameServer::SendWelcome(ClientID clientID)
{
    auto pkt = PacketSerializer::WriteServerWelcome(clientID);
    SendTo(clientID, pkt.data(), pkt.size(), 0); // reliable
}

void GameServer::SendObjectDespawn(ClientID toClientID, ClientID ownerClientID, NetObjectID objectID)
{
    if (objectID == INVALID_NET_OBJECT_ID)
        return;
    auto pkt = PacketSerializer::WriteObjectDespawn(ownerClientID, objectID);
    SendTo(toClientID, pkt.data(), pkt.size(), 0); // reliable
}

void GameServer::SendTo(ClientID clientID,
                        const uint8_t *data, size_t len, uint8_t channel)
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end())
        return;

    NBN_GameServer_SendByteArrayTo(
        it->second.connHandle,
        const_cast<uint8_t *>(data),
        static_cast<unsigned int>(len),
        MapChannel(channel));
}

void GameServer::RemoveClient(ClientID clientID, const char *reason)
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end())
        return;

    const ClientID removedOwnerID = it->second.id;
    const NetObjectID removedObjectID = it->second.objectID;
    const bool shouldNotify = it->second.welcomed && removedObjectID != INVALID_NET_OBJECT_ID;

    if (shouldNotify)
    {
        std::vector<ClientID> notifyTargets;
        notifyTargets.reserve(m_clients.size());
        for (const auto &[id, cs] : m_clients)
        {
            (void)id;
            if (!cs.welcomed || cs.id == removedOwnerID)
                continue;
            notifyTargets.push_back(cs.id);
        }
        for (ClientID targetID : notifyTargets)
            SendObjectDespawn(targetID, removedOwnerID, removedObjectID);
    }

    uint32_t connHandle = it->second.connHandle;
    // Keep UUID mapping alive so returning players are recognised.
    // Only remove connection/state tracking.
    m_clients.erase(it);
    m_connIndex.erase(connHandle);

    std::cout << "[GameServer] Client " << clientID << " " << reason << "\n";
}

void GameServer::RemoveTimedOutClients()
{
    const auto now = std::chrono::steady_clock::now();
    std::vector<ClientID> timedOutIDs;
    timedOutIDs.reserve(m_clients.size());

    for (const auto &[id, cs] : m_clients)
    {
        (void)id;
        if (!cs.welcomed)
            continue;
        if ((now - cs.lastSeen) > m_clientTimeout)
            timedOutIDs.push_back(cs.id);
    }

    for (ClientID id : timedOutIDs)
        RemoveClient(id, "timed out");
}

// ── Broadcast ──────────────────────────────────────────────────────

void GameServer::BroadcastPositions()
{
    // Collect entries from all welcomed clients that have reported.
    std::vector<NetBroadcastEntry> entries;
    entries.reserve(m_clients.size());

    for (const auto &[id, cs] : m_clients)
    {
        (void)id;
        if (!cs.welcomed || !cs.hasTransform)
            continue;

        NetBroadcastEntry e;
        e.clientID = cs.id;
        e.objectID = cs.objectID;
        e.transform = cs.lastTransform;
        entries.push_back(e);
    }

    if (entries.empty())
        return;

    auto pkt = PacketSerializer::WritePositionBroadcast(entries);

    for (auto &[id, cs] : m_clients)
    {
        (void)id;
        if (!cs.welcomed)
            continue;

        NBN_GameServer_SendByteArrayTo(
            cs.connHandle,
            pkt.data(),
            static_cast<unsigned int>(pkt.size()),
            NBN_CHANNEL_RESERVED_UNRELIABLE); // unreliable for position broadcast
    }
}
