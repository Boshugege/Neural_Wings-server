#pragma once
#include "Engine/Network/NetTypes.h"
#include "Engine/Network/Protocol/PacketSerializer.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <chrono>

/// Authoritative game server powered by nbnet.
///
/// Always registers UDP. If compiled with NW_ENABLE_WEBRTC_C, it also
/// registers native WebRTC (WebSocket signaling + data channel).
/// All clients appear as NBN_ConnectionHandle to game logic.
class GameServer
{
public:
    GameServer() = default;
    ~GameServer();

    /// Start listening on the given port.
    /// nbnet drivers determine which transports are accepted.
    bool Start(uint16_t port = DEFAULT_SERVER_PORT);

    /// Run one server tick: poll network + broadcast positions.
    void Tick();

    /// Shut down gracefully.
    void Stop();

    bool IsRunning() const { return m_running; }

private:
    // ── Internal helpers ───────────────────────────────────────────
    void HandleNewConnection();
    void HandleClientDisconnected();
    void HandleClientMessage();

    void DispatchPacket(ClientID clientID, const uint8_t *data, size_t len);
    void HandleClientHello(ClientID clientID, const uint8_t *data, size_t len);
    void HandlePositionUpdate(ClientID clientID, const uint8_t *data, size_t len);
    void HandleClientDisconnect(ClientID clientID);
    void HandleChatRequest(ClientID clientID, const uint8_t *data, size_t len);
    void HandleNicknameUpdateRequest(ClientID clientID, const uint8_t *data, size_t len);

    void SendWelcome(ClientID clientID);
    void SendObjectDespawn(ClientID toClientID, ClientID ownerClientID, NetObjectID objectID);
    void SendTo(ClientID clientID, const uint8_t *data, size_t len, uint8_t channel);
    void RemoveClient(ClientID clientID, const char *reason, bool closeTransport = false);
    void BroadcastPositions();
    void RemoveTimedOutClients();

    // ── Chat helpers ────────────────────────────────────────────
    void BroadcastChat(ChatMessageType chatType, ClientID senderID,
                       const std::string &senderName, const std::string &text);
    void SendChatTo(ClientID targetID, ChatMessageType chatType, ClientID senderID,
                    const std::string &senderName, const std::string &text);
    /// Send a system message to a specific client or all clients (targetID=0 for all).
    void SendSystemMessage(const std::string &text, ClientID targetID = INVALID_CLIENT_ID);
    void SendNicknameUpdateResult(ClientID clientID, NicknameUpdateStatus status,
                                  const std::string &nickname);
    std::string GetClientDisplayName(ClientID clientID) const;
    static std::string NormalizeNickname(const std::string &nickname);
    static bool IsValidNickname(const std::string &nickname);

    // ── Data ───────────────────────────────────────────────────────
    bool m_running = false;
    ClientID m_nextClientID = 1; // 0 is INVALID

    /// Per-client state stored on the server.
    struct ClientState
    {
        ClientID id = INVALID_CLIENT_ID;
        uint32_t connHandle = 0; // NBN_ConnectionHandle
        NetUUID uuid{};          // persistent client identity

        NetObjectID objectID = INVALID_NET_OBJECT_ID;
        NetTransformState lastTransform{};
        bool hasTransform = false;
        bool welcomed = false;
        std::string nickname;
        ClientID whisperTargetID = INVALID_CLIENT_ID;
        std::string whisperTargetNickname;
        std::chrono::steady_clock::time_point lastSeen = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point lastChatTime{}; // rate limit
    };

    /// ClientID → state
    std::unordered_map<ClientID, ClientState> m_clients;

    /// NBN_ConnectionHandle → ClientID   (reverse index for event dispatch)
    std::unordered_map<uint32_t, ClientID> m_connIndex;

    /// NetUUID → ClientID   (persistent identity mapping)
    std::unordered_map<NetUUID, ClientID, NetUUIDHash> m_uuidIndex;
    /// normalized nickname -> ClientID (online only)
    std::unordered_map<std::string, ClientID> m_nicknameIndex;

    // Disable application-level timeout by default. We rely on transport disconnect events.
    std::chrono::milliseconds m_clientTimeout{0};
    uint32_t m_serverTick = 0;
};
