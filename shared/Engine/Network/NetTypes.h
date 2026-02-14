#pragma once
#include <cstdint>
#include <cstring>

/// Unique identifier assigned by the server to each connected client.
using ClientID = uint32_t;

/// Unique identifier for a networked game object.
using NetObjectID = uint32_t;

/// Reserved value: "not assigned yet".
constexpr ClientID INVALID_CLIENT_ID = 0;
constexpr NetObjectID INVALID_NET_OBJECT_ID = 0;

/// Default network settings.
constexpr uint16_t DEFAULT_SERVER_PORT = 7777;
constexpr const char *DEFAULT_SERVER_HOST = "127.0.0.1";

/// 128-bit UUID for persistent client identity.
struct NetUUID
{
    uint8_t bytes[16] = {};

    bool operator==(const NetUUID &o) const { return std::memcmp(bytes, o.bytes, 16) == 0; }
    bool operator!=(const NetUUID &o) const { return !(*this == o); }

    bool IsNull() const
    {
        for (int i = 0; i < 16; ++i)
            if (bytes[i] != 0)
                return false;
        return true;
    }
};

/// Hash support for NetUUID (for use in std::unordered_map etc.)
struct NetUUIDHash
{
    std::size_t operator()(const NetUUID &u) const noexcept
    {
        // FNV-1a on the 16 bytes
        std::size_t h = 14695981039346656037ULL;
        for (int i = 0; i < 16; ++i)
        {
            h ^= u.bytes[i];
            h *= 1099511628211ULL;
        }
        return h;
    }
};

enum class ConnectionState : uint8_t
{
    Disconnected,
    Connecting,
    Connected,
    Disconnecting
};
