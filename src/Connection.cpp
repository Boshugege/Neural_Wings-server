// ────────────────────────────────────────────────────────────────────
// GameServer connection and packet dispatch
// ────────────────────────────────────────────────────────────────────

extern "C"
{
#include <nbnet.h>
}

#include "GameServer.h"

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

void GameServer::DispatchPacket(ClientID clientID,
                                const uint8_t *data, size_t len)
{
    if (len < sizeof(NetPacketHeader))
        return;

    // Treat any valid packet from a known client as keep-alive.
    auto itClient = m_clients.find(clientID);
    if (itClient != m_clients.end())
        itClient->second.lastSeen = std::chrono::steady_clock::now();

    NetMessageType type = PacketSerializer::PeekType(data, len);
    switch (type)
    {
    case NetMessageType::ClientHello:
        HandleClientHello(clientID, data, len);
        break;
    case NetMessageType::PositionUpdate:
        HandlePositionUpdate(clientID, data, len);
        break;
    case NetMessageType::ObjectRelease:
        HandleObjectRelease(clientID, data, len);
        break;
    case NetMessageType::Heartbeat:
        HandleHeartbeat(clientID, data, len);
        break;
    case NetMessageType::ClientDisconnect:
        HandleClientDisconnect(clientID);
        break;
    case NetMessageType::ChatRequest:
        HandleChatRequest(clientID, data, len);
        break;
    case NetMessageType::NicknameUpdateRequest:
        HandleNicknameUpdateRequest(clientID, data, len);
        break;
    default:
        std::cerr << "[GameServer] Unknown message type "
                  << static_cast<int>(type) << "\n";
        break;
    }
}

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

            // Security policy: if the same UUID is already online,
            // reject the later login instead of replacing the active session.
            if (oldID != clientID)
            {
                auto existingIt = m_clients.find(oldID);
                if (existingIt != m_clients.end() &&
                    existingIt->second.connHandle != it->second.connHandle)
                {
                    std::cout << "[GameServer] Duplicate UUID blocked, keep online ClientID "
                              << oldID << "\n";
                    RemoveClient(clientID, "duplicate UUID", true);
                    return;
                }
            }
            std::cout << "[GameServer] Returning player UUID recognised, "
                      << "reusing ClientID " << oldID << "\n";

            // Update the state to use the old ClientID
            ClientState &cs = it->second;
            cs.id = oldID;
            cs.uuid = uuid;
            cs.welcomed = true;
            if (cs.nickname.empty())
                cs.nickname = "Player " + std::to_string(oldID);
            cs.lastSeen = std::chrono::steady_clock::now();

            // Re-index: move state from temp clientID to old clientID
            ClientState movedState = cs;
            m_clients.erase(it);
            m_clients[oldID] = movedState;
            m_connIndex[movedState.connHandle] = oldID;
            m_nicknameIndex[NormalizeNickname(movedState.nickname)] = oldID;

            SendWelcome(oldID);
            SendNicknameUpdateResult(oldID, NicknameUpdateStatus::Accepted, movedState.nickname);
            return;
        }

        // New player — register UUID
        it->second.uuid = uuid;
        m_uuidIndex[uuid] = clientID;
        std::cout << "[GameServer] New player UUID registered, ClientID "
                  << clientID << "\n";
    }

    it->second.welcomed = true;
    if (it->second.nickname.empty())
        it->second.nickname = "Player " + std::to_string(clientID);
    m_nicknameIndex[NormalizeNickname(it->second.nickname)] = clientID;
    it->second.lastSeen = std::chrono::steady_clock::now();
    SendWelcome(clientID);
    SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Accepted, it->second.nickname);
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

void GameServer::HandleObjectRelease(ClientID clientID,
                                     const uint8_t *data, size_t len)
{
    auto msg = PacketSerializer::Read<MsgObjectRelease>(data, len);

    auto it = m_clients.find(clientID);
    if (it == m_clients.end())
        return;

    const NetObjectID releasedObjectID = msg.objectID;

    // Only act if this client actually owns this object
    if (it->second.objectID != releasedObjectID)
        return;

    // Broadcast ObjectDespawn to all other welcomed clients
    for (const auto &[id, cs] : m_clients)
    {
        (void)id;
        if (!cs.welcomed || cs.id == clientID)
            continue;
        SendObjectDespawn(cs.id, clientID, releasedObjectID);
    }

    // Clear the object state but keep the client connected
    it->second.objectID = INVALID_NET_OBJECT_ID;
    it->second.hasTransform = false;
    it->second.lastTransform = {};
    it->second.lastSeen = std::chrono::steady_clock::now();

    std::cout << "[GameServer] Client " << clientID
              << " released object " << releasedObjectID << "\n";
}

void GameServer::HandleHeartbeat(ClientID clientID,
                                 const uint8_t *data, size_t len)
{
    auto msg = PacketSerializer::Read<MsgHeartbeat>(data, len);
    if (msg.clientID != INVALID_CLIENT_ID && msg.clientID != clientID)
    {
        std::cerr << "[GameServer] Heartbeat client id mismatch, conn="
                  << clientID << " payload=" << msg.clientID << "\n";
        return;
    }

    auto it = m_clients.find(clientID);
    if (it != m_clients.end())
        it->second.lastSeen = std::chrono::steady_clock::now();
}

void GameServer::HandleClientDisconnect(ClientID clientID)
{
    RemoveClient(clientID, "requested disconnect", true);
}
