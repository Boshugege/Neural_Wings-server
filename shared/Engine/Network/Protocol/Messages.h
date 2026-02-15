#pragma once
#include "Engine/Network/NetTypes.h"
#include "Engine/Network/Protocol/MessageTypes.h"
#include <cstdint>
#include <cstring>

// ─── POD message structs ───────────────────────────────────────────
// All structs are tightly packed so they can be memcpy'd directly.
// For cross-platform use we keep them simple flat data; no pointers.

#pragma pack(push, 1)

/// Header prepended to every packet.
struct NetPacketHeader
{
    NetMessageType type;
};

// ── Connection ──────────────────────────────────────────────────────

/// C→S : "I want to join"
struct MsgClientHello
{
    NetPacketHeader header{NetMessageType::ClientHello};
    NetUUID uuid{}; // persistent client identity
};

/// S→C : "Welcome, here is your ID"
struct MsgServerWelcome
{
    NetPacketHeader header{NetMessageType::ServerWelcome};
    ClientID assignedClientID = INVALID_CLIENT_ID;
};

/// C→S : graceful goodbye
struct MsgClientDisconnect
{
    NetPacketHeader header{NetMessageType::ClientDisconnect};
    ClientID clientID = INVALID_CLIENT_ID;
};

// ── Position sync ───────────────────────────────────────────────────

/// Compact transform state for one object.
struct NetTransformState
{
    float posX, posY, posZ;
    float rotW, rotX, rotY, rotZ; // quaternion
};

/// C→S : client reports its own position.
struct MsgPositionUpdate
{
    NetPacketHeader header{NetMessageType::PositionUpdate};
    ClientID clientID = INVALID_CLIENT_ID;
    NetObjectID objectID = INVALID_NET_OBJECT_ID;
    NetTransformState transform{};
};

/// One entry inside the broadcast.
struct NetBroadcastEntry
{
    ClientID clientID = INVALID_CLIENT_ID;
    NetObjectID objectID = INVALID_NET_OBJECT_ID;
    NetTransformState transform{};
};

/// S→C : server broadcasts positions of ALL players.
/// Variable-length: header + count + count*Entry.
struct MsgPositionBroadcast
{
    NetPacketHeader header{NetMessageType::PositionBroadcast};
    uint16_t entryCount = 0;
    // Followed by `entryCount` NetBroadcastEntry structs in the buffer.
};

/// S→C : server notifies that a network object should be removed.
struct MsgObjectDespawn
{
    NetPacketHeader header{NetMessageType::ObjectDespawn};
    ClientID ownerClientID = INVALID_CLIENT_ID;
    NetObjectID objectID = INVALID_NET_OBJECT_ID;
};

#pragma pack(pop)
