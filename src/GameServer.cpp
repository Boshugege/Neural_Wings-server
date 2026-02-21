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
#include <cctype>

// ── Constants ──────────────────────────────────────────────────────
static constexpr const char *NW_PROTOCOL_NAME = "neural_wings";

static uint8_t MapChannel(uint8_t ourChannel)
{
    // our convention: 0 = reliable, 1 = unreliable
    return (ourChannel == 0) ? NBN_CHANNEL_RESERVED_RELIABLE : NBN_CHANNEL_RESERVED_UNRELIABLE;
}

static std::string TrimSpaces(const std::string &text)
{
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }

    if (begin >= text.size())
        return "";

    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }

    return text.substr(begin, end - begin);
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
    m_serverTick = 0;
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
    m_nicknameIndex.clear();
    m_clients.clear();
    std::cout << "[GameServer] Stopped\n";
}

// ── Tick ───────────────────────────────────────────────────────────

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

void GameServer::HandleClientDisconnect(ClientID clientID)
{
    RemoveClient(clientID, "requested disconnect", true);
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

void GameServer::RemoveClient(ClientID clientID, const char *reason, bool closeTransport)
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
    if (!it->second.nickname.empty())
        m_nicknameIndex.erase(NormalizeNickname(it->second.nickname));

    if (closeTransport)
    {
        if (NBN_GameServer_CloseClient(connHandle) < 0)
        {
            std::cerr << "[GameServer] Failed to close transport for client "
                      << clientID << "\n";
        }
    }
    // Keep UUID mapping alive so returning players are recognised.
    // Only remove connection/state tracking.
    m_clients.erase(it);
    m_connIndex.erase(connHandle);

    std::cout << "[GameServer] Client " << clientID << " " << reason << "\n";
}

void GameServer::RemoveTimedOutClients()
{
    if (m_clientTimeout.count() <= 0)
        return;

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
        RemoveClient(id, "timed out", true);
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

    auto pkt = PacketSerializer::WritePositionBroadcast(entries, m_serverTick);

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

// ── Chat ───────────────────────────────────────────────────────────

static constexpr size_t MAX_CHAT_TEXT_LEN = 256;
static constexpr size_t MAX_NICKNAME_LEN = 16;
static constexpr size_t MIN_NICKNAME_LEN = 3;
static constexpr std::chrono::milliseconds CHAT_RATE_LIMIT{300}; // 0.3 s

std::string GameServer::GetClientDisplayName(ClientID clientID) const
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end() || it->second.nickname.empty())
        return "Player " + std::to_string(clientID);
    return it->second.nickname;
}

std::string GameServer::NormalizeNickname(const std::string &nickname)
{
    std::string out;
    out.reserve(nickname.size());
    for (char ch : nickname)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool GameServer::IsValidNickname(const std::string &nickname)
{
    if (nickname.size() < MIN_NICKNAME_LEN || nickname.size() > MAX_NICKNAME_LEN)
        return false;
    for (char ch : nickname)
    {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c == '_')
            continue;
        return false;
    }
    return true;
}

void GameServer::SendNicknameUpdateResult(ClientID clientID,
                                          NicknameUpdateStatus status,
                                          const std::string &nickname)
{
    auto pkt = PacketSerializer::WriteNicknameUpdateResult(status, nickname);
    SendTo(clientID, pkt.data(), pkt.size(), 0);
}

void GameServer::HandleNicknameUpdateRequest(ClientID clientID,
                                             const uint8_t *data, size_t len)
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end() || !it->second.welcomed)
        return;

    auto req = PacketSerializer::ReadNicknameUpdateRequest(data, len);
    const std::string requested = req.nickname;
    const std::string currentNorm = NormalizeNickname(GetClientDisplayName(clientID));
    const std::string requestedNorm = NormalizeNickname(requested);

    if (requestedNorm == currentNorm)
    {
        // Idempotent update: keep quiet, only acknowledge.
        SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Accepted,
                                 GetClientDisplayName(clientID));
        return;
    }

    if (!IsValidNickname(requested))
    {
        SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Invalid,
                                 GetClientDisplayName(clientID));
        return;
    }

    auto existing = m_nicknameIndex.find(requestedNorm);
    if (existing != m_nicknameIndex.end() && existing->second != clientID)
    {
        SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Conflict,
                                 GetClientDisplayName(clientID));
        return;
    }

    if (!it->second.nickname.empty())
    {
        m_nicknameIndex.erase(NormalizeNickname(it->second.nickname));
    }
    it->second.nickname = requested;
    m_nicknameIndex[requestedNorm] = clientID;

    SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Accepted, requested);
    SendSystemMessage("Your nickname is now '" + requested + "'.", clientID);
}

void GameServer::HandleChatRequest(ClientID clientID,
                                   const uint8_t *data, size_t len)
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end() || !it->second.welcomed)
        return;

    auto req = PacketSerializer::ReadChatRequest(data, len);

    // ── Validation ─────────────────────────────────────────────────
    // 1. Text length check
    if (req.text.empty() || req.text.size() > MAX_CHAT_TEXT_LEN)
    {
        std::cerr << "[GameServer] Chat rejected from " << clientID
                  << ": invalid text length (" << req.text.size() << ")\n";
        return;
    }

    // 2. Rate limit
    auto now = std::chrono::steady_clock::now();
    if ((now - it->second.lastChatTime) < CHAT_RATE_LIMIT)
    {
        std::cerr << "[GameServer] Chat rate-limited for " << clientID << "\n";
        SendSystemMessage("Message rate-limited. Please slow down.", clientID);
        return;
    }
    it->second.lastChatTime = now;

    const std::string senderName = GetClientDisplayName(clientID);

    auto setPublicMode = [&]()
    {
        it->second.whisperTargetID = INVALID_CLIENT_ID;
        it->second.whisperTargetNickname.clear();
    };

    auto sendHelp = [this, clientID]()
    {
        SendSystemMessage(
            "Available chat commands:\n"
            "/w <nickname> - enter whisper mode (supports spaces in nickname).\n"
            "/a - return to public chat.\n"
            "/help - show this help message.",
            clientID);
    };

    if (!req.text.empty() && req.text[0] == '/')
    {
        if (req.text == "/help")
        {
            sendHelp();
            return;
        }

        if (req.text.rfind("/a", 0) == 0 &&
            TrimSpaces(req.text.substr(2)).empty())
        {
            setPublicMode();
            SendSystemMessage(
                "[CHAT_MODE:PUBLIC] Switched to public chat.",
                clientID);
            return;
        }

        if (req.text == "/w" || req.text.rfind("/w ", 0) == 0)
        {
            const std::string targetNickname = TrimSpaces(req.text.substr(2));

            if (targetNickname.empty())
            {
                SendSystemMessage("Usage: /w <nickname>", clientID);
                return;
            }

            const std::string targetNorm = NormalizeNickname(targetNickname);
            auto targetIdIt = m_nicknameIndex.find(targetNorm);
            if (targetIdIt == m_nicknameIndex.end())
            {
                setPublicMode();
                SendSystemMessage(
                    "[CHAT_MODE:PUBLIC] Player '" + targetNickname +
                        "' is not online. Switched to public chat.",
                    clientID);
                return;
            }

            const ClientID targetID = targetIdIt->second;
            auto targetStateIt = m_clients.find(targetID);
            if (targetStateIt == m_clients.end() || !targetStateIt->second.welcomed)
            {
                setPublicMode();
                SendSystemMessage(
                    "[CHAT_MODE:PUBLIC] Player '" + targetNickname +
                        "' is not online. Switched to public chat.",
                    clientID);
                return;
            }

            const std::string targetDisplayName = GetClientDisplayName(targetID);
            it->second.whisperTargetID = targetID;
            it->second.whisperTargetNickname = targetDisplayName;
            SendSystemMessage(
                "[CHAT_MODE:WHISPER:" + targetDisplayName +
                    "] Whisper mode on for '" + targetDisplayName +
                    "'. Use /a to return to public chat.",
                clientID);
            return;
        }

        SendSystemMessage("Unknown command. Type /help for commands.", clientID);
        return;
    }

    if (it->second.whisperTargetID != INVALID_CLIENT_ID)
    {
        const ClientID targetID = it->second.whisperTargetID;
        auto targetStateIt = m_clients.find(targetID);
        if (targetStateIt == m_clients.end() || !targetStateIt->second.welcomed)
        {
            const std::string offlineName =
                it->second.whisperTargetNickname.empty()
                    ? "selected player"
                    : ("'" + it->second.whisperTargetNickname + "'");
            setPublicMode();
            SendSystemMessage(
                "[CHAT_MODE:PUBLIC] Whisper target " + offlineName +
                    " is offline. Switched to public chat.",
                clientID);
            return;
        }

        const std::string targetDisplayName = GetClientDisplayName(targetID);
        it->second.whisperTargetNickname = targetDisplayName;

        std::cout << "[Chat] [Whisper] " << senderName << " -> "
                  << targetDisplayName << ": " << req.text << "\n";

        SendChatTo(targetID, ChatMessageType::Whisper,
                   clientID, senderName, req.text);

        if (targetID != clientID)
        {
            SendChatTo(clientID, ChatMessageType::Whisper,
                       clientID, senderName, req.text);
        }
        return;
    }

    switch (req.chatType)
    {
    case ChatMessageType::Public:
    {
        std::cout << "[Chat] [Public] " << senderName << ": " << req.text << "\n";
        BroadcastChat(ChatMessageType::Public, clientID, senderName, req.text);
        break;
    }
    case ChatMessageType::Whisper:
    {
        SendSystemMessage("Use /w <nickname> to enter whisper mode.", clientID);
        break;
    }
    case ChatMessageType::System:
        // Clients are not allowed to send system messages
        std::cerr << "[GameServer] Client " << clientID
                  << " tried to send a system message\n";
        break;
    }
}

void GameServer::BroadcastChat(ChatMessageType chatType, ClientID senderID,
                               const std::string &senderName, const std::string &text)
{
    auto pkt = PacketSerializer::WriteChatBroadcast(chatType, senderID, senderName, text);
    for (auto &[id, cs] : m_clients)
    {
        (void)id;
        if (!cs.welcomed)
            continue;
        SendTo(cs.id, pkt.data(), pkt.size(), 0); // reliable
    }
}

void GameServer::SendChatTo(ClientID targetID, ChatMessageType chatType,
                            ClientID senderID, const std::string &senderName,
                            const std::string &text)
{
    auto pkt = PacketSerializer::WriteChatBroadcast(chatType, senderID, senderName, text);
    SendTo(targetID, pkt.data(), pkt.size(), 0); // reliable
}

void GameServer::SendSystemMessage(const std::string &text, ClientID targetID)
{
    if (targetID != INVALID_CLIENT_ID)
    {
        // Send to specific client
        auto pkt = PacketSerializer::WriteChatBroadcast(
            ChatMessageType::System, INVALID_CLIENT_ID, "System", text);
        SendTo(targetID, pkt.data(), pkt.size(), 0);
    }
    else
    {
        // Broadcast to all
        BroadcastChat(ChatMessageType::System, INVALID_CLIENT_ID, "System", text);
    }
}
