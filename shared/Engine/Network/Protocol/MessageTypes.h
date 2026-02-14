#pragma once
#include <cstdint>

/// Every network packet starts with a 1-byte message type.
enum class NetMessageType : uint8_t
{
    // ── Connection ──────────────────────────
    ClientHello = 0x01,      // C→S  client requests to join
    ServerWelcome = 0x02,    // S→C  server assigns ClientID
    ClientDisconnect = 0x03, // C→S  graceful disconnect

    // ── Position sync ───────────────────────
    PositionUpdate = 0x10,    // C→S  client sends own position
    PositionBroadcast = 0x11, // S→C  server broadcasts all positions

    // ── Future (reserved) ───────────────────
    // RoomJoin      = 0x20,
    // RoomLeave     = 0x21,
    // FireBullet    = 0x30,
    // HitConfirm    = 0x31,
};
