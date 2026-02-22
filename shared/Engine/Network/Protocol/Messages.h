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

/// C→S : keep the connection alive while idle in non-gameplay screens.
struct MsgHeartbeat
{
    NetPacketHeader header{NetMessageType::Heartbeat};
    ClientID clientID = INVALID_CLIENT_ID;
};

// ── Flight state sync ────────────────────────────────────────────────

/// Compact flight state for one object (transform + velocity).
struct NetTransformState
{
    float posX, posY, posZ;
    float rotW, rotX, rotY, rotZ;    // quaternion
    float linVelX, linVelY, linVelZ; // linear velocity
    float angVelX, angVelY, angVelZ; // angular velocity
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
    uint32_t serverTick = 0;
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

/// C→S : client releases an object but stays connected.
/// Server will broadcast ObjectDespawn to other clients and clear the object state.
struct MsgObjectRelease
{
    NetPacketHeader header{NetMessageType::ObjectRelease};
    ClientID clientID = INVALID_CLIENT_ID;
    NetObjectID objectID = INVALID_NET_OBJECT_ID;
};

// ── Chat ────────────────────────────────────────────────────────────

/// Chat channel / message type.
enum class ChatMessageType : uint8_t
{
    System = 0,  // server-originated system message
    Public = 1,  // public (global) chat
    Whisper = 2, // private message between two players
};

enum class NicknameUpdateStatus : uint8_t
{
    Accepted = 0,
    Conflict = 1,
    Invalid = 2,
};

/// C→S : client requests to send a chat message.
/// Variable-length: header + fields + textLength + text bytes.
struct MsgChatRequest
{
    NetPacketHeader header{NetMessageType::ChatRequest};
    ChatMessageType chatType = ChatMessageType::Public;
    ClientID targetClientID = INVALID_CLIENT_ID; // only meaningful for Whisper
    uint16_t textLength = 0;
    // Followed by `textLength` bytes of UTF-8 text in the buffer.
};

/// S→C : server delivers a chat message to client(s).
/// Variable-length: header + fields + senderNameLength + senderName + textLength + text.
struct MsgChatBroadcast
{
    NetPacketHeader header{NetMessageType::ChatBroadcast};
    ChatMessageType chatType = ChatMessageType::Public;
    ClientID senderClientID = INVALID_CLIENT_ID; // 0 for system messages
    uint8_t senderNameLength = 0;
    // Followed by `senderNameLength` bytes of sender display name (UTF-8),
    // then uint16_t textLength, then `textLength` bytes of UTF-8 text.
};

/// C→S : client requests nickname update.
/// Variable-length: header + nicknameLength + nickname bytes.
struct MsgNicknameUpdateRequest
{
    NetPacketHeader header{NetMessageType::NicknameUpdateRequest};
    uint8_t nicknameLength = 0;
    // Followed by `nicknameLength` bytes of UTF-8 nickname.
};

/// S→C : server returns nickname validation result and authoritative nickname.
/// Variable-length: header + status + nicknameLength + nickname bytes.
struct MsgNicknameUpdateResult
{
    NetPacketHeader header{NetMessageType::NicknameUpdateResult};
    NicknameUpdateStatus status = NicknameUpdateStatus::Accepted;
    uint8_t nicknameLength = 0;
    // Followed by `nicknameLength` bytes of UTF-8 nickname.
};

#pragma pack(pop)
