#pragma once
#include "Engine/Network/NetTypes.h"
#include "Engine/Network/Protocol/PacketSerializer.h"

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <iostream>

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

    void SendWelcome(ClientID clientID);
    void SendTo(ClientID clientID, const uint8_t *data, size_t len, uint8_t channel);
    void RemoveClient(ClientID clientID, const char *reason);
    void BroadcastPositions();

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
    };

    /// ClientID → state
    std::unordered_map<ClientID, ClientState> m_clients;

    /// NBN_ConnectionHandle → ClientID   (reverse index for event dispatch)
    std::unordered_map<uint32_t, ClientID> m_connIndex;

    /// NetUUID → ClientID   (persistent identity mapping)
    std::unordered_map<NetUUID, ClientID, NetUUIDHash> m_uuidIndex;
};
