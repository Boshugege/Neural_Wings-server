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
    ObjectDespawn = 0x12,     // S→C  server tells clients to remove an object

    // ── Chat ─────────────────────────────────
    ChatRequest = 0x40,   // C→S  client sends a chat message
    ChatBroadcast = 0x41, // S→C  server delivers a chat message
    NicknameUpdateRequest = 0x42, // C→S client requests nickname change
    NicknameUpdateResult = 0x43,  // S→C server returns nickname check result

    // ── Future (reserved) ───────────────────
    // RoomJoin      = 0x20,
    // RoomLeave     = 0x21,
    // FireBullet    = 0x30,
    // HitConfirm    = 0x31,
};
