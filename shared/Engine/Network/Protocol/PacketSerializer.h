#pragma once
#include "Engine/Network/Protocol/Messages.h"
#include <vector>
#include <cstring>
#include <cassert>

/// Header-only helpers for packing / unpacking network messages.
/// Keeps serialization in one place so client & server stay in sync.
namespace PacketSerializer
{

    // ────────────────────── Writers ──────────────────────

    inline std::vector<uint8_t> WriteClientHello(const NetUUID &uuid)
    {
        MsgClientHello msg;
        msg.uuid = uuid;
        std::vector<uint8_t> buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        return buf;
    }

    inline std::vector<uint8_t> WriteServerWelcome(ClientID id)
    {
        MsgServerWelcome msg;
        msg.assignedClientID = id;
        std::vector<uint8_t> buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        return buf;
    }

    inline std::vector<uint8_t> WritePositionUpdate(ClientID cid, NetObjectID oid,
                                                    const NetTransformState &ts)
    {
        MsgPositionUpdate msg;
        msg.clientID = cid;
        msg.objectID = oid;
        msg.transform = ts;
        std::vector<uint8_t> buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        return buf;
    }

    inline std::vector<uint8_t> WritePositionBroadcast(
        const std::vector<NetBroadcastEntry> &entries)
    {
        MsgPositionBroadcast hdr;
        hdr.entryCount = static_cast<uint16_t>(entries.size());
        size_t totalSize = sizeof(hdr) + entries.size() * sizeof(NetBroadcastEntry);
        std::vector<uint8_t> buf(totalSize);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        if (!entries.empty())
        {
            std::memcpy(buf.data() + sizeof(hdr),
                        entries.data(),
                        entries.size() * sizeof(NetBroadcastEntry));
        }
        return buf;
    }

    inline std::vector<uint8_t> WriteClientDisconnect(ClientID cid)
    {
        MsgClientDisconnect msg;
        msg.clientID = cid;
        std::vector<uint8_t> buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        return buf;
    }

    // ────────────────────── Readers ──────────────────────

    /// Peek at the message type (first byte).
    inline NetMessageType PeekType(const uint8_t *data, size_t len)
    {
        assert(len >= sizeof(NetPacketHeader));
        NetPacketHeader hdr;
        std::memcpy(&hdr, data, sizeof(hdr));
        return hdr.type;
    }

    template <typename T>
    inline T Read(const uint8_t *data, size_t len)
    {
        assert(len >= sizeof(T));
        T msg;
        std::memcpy(&msg, data, sizeof(T));
        return msg;
    }

    /// Read the variable-length broadcast entries that follow MsgPositionBroadcast.
    inline std::vector<NetBroadcastEntry> ReadBroadcastEntries(
        const uint8_t *data, size_t len)
    {
        auto hdr = Read<MsgPositionBroadcast>(data, len);
        std::vector<NetBroadcastEntry> entries(hdr.entryCount);
        if (hdr.entryCount > 0)
        {
            size_t offset = sizeof(MsgPositionBroadcast);
            std::memcpy(entries.data(), data + offset,
                        hdr.entryCount * sizeof(NetBroadcastEntry));
        }
        return entries;
    }

} // namespace PacketSerializer
