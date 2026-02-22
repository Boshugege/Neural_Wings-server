#pragma once
#include "Engine/Network/Protocol/Messages.h"
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <algorithm>

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
        const std::vector<NetBroadcastEntry> &entries,
        uint32_t serverTick)
    {
        MsgPositionBroadcast hdr;
        hdr.serverTick = serverTick;
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

    inline std::vector<uint8_t> WriteHeartbeat(ClientID cid)
    {
        MsgHeartbeat msg;
        msg.clientID = cid;
        std::vector<uint8_t> buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        return buf;
    }

    inline std::vector<uint8_t> WriteObjectRelease(ClientID cid, NetObjectID oid)
    {
        MsgObjectRelease msg;
        msg.clientID = cid;
        msg.objectID = oid;
        std::vector<uint8_t> buf(sizeof(msg));
        std::memcpy(buf.data(), &msg, sizeof(msg));
        return buf;
    }

    inline std::vector<uint8_t> WriteObjectDespawn(ClientID ownerClientID, NetObjectID objectID)
    {
        MsgObjectDespawn msg;
        msg.ownerClientID = ownerClientID;
        msg.objectID = objectID;
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

    struct PositionBroadcastData
    {
        uint32_t serverTick = 0;
        std::vector<NetBroadcastEntry> entries;
    };

    inline PositionBroadcastData ReadPositionBroadcast(
        const uint8_t *data, size_t len)
    {
        auto hdr = Read<MsgPositionBroadcast>(data, len);
        PositionBroadcastData out{};
        out.serverTick = hdr.serverTick;
        out.entries.resize(hdr.entryCount);
        if (hdr.entryCount > 0)
        {
            size_t offset = sizeof(MsgPositionBroadcast);
            std::memcpy(out.entries.data(), data + offset,
                        hdr.entryCount * sizeof(NetBroadcastEntry));
        }
        return out;
    }

    /// Read the variable-length broadcast entries that follow MsgPositionBroadcast.
    inline std::vector<NetBroadcastEntry> ReadBroadcastEntries(
        const uint8_t *data, size_t len)
    {
        return ReadPositionBroadcast(data, len).entries;
    }

    // ────────────────────── Chat Writers ──────────────────────

    /// Build a ChatRequest packet (C→S).
    inline std::vector<uint8_t> WriteChatRequest(ChatMessageType chatType,
                                                 ClientID targetID,
                                                 const std::string &text)
    {
        uint16_t textLen = static_cast<uint16_t>(
            std::min(text.size(), static_cast<size_t>(512)));
        size_t totalSize = sizeof(MsgChatRequest) + textLen;
        std::vector<uint8_t> buf(totalSize);

        MsgChatRequest hdr;
        hdr.chatType = chatType;
        hdr.targetClientID = targetID;
        hdr.textLength = textLen;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        if (textLen > 0)
            std::memcpy(buf.data() + sizeof(hdr), text.data(), textLen);
        return buf;
    }

    /// Build a ChatBroadcast packet (S→C).
    inline std::vector<uint8_t> WriteChatBroadcast(ChatMessageType chatType,
                                                   ClientID senderID,
                                                   const std::string &senderName,
                                                   const std::string &text)
    {
        uint8_t nameLen = static_cast<uint8_t>(
            std::min(senderName.size(), static_cast<size_t>(255)));
        uint16_t textLen = static_cast<uint16_t>(
            std::min(text.size(), static_cast<size_t>(512)));
        size_t totalSize = sizeof(MsgChatBroadcast) + nameLen + sizeof(uint16_t) + textLen;
        std::vector<uint8_t> buf(totalSize);

        MsgChatBroadcast hdr;
        hdr.chatType = chatType;
        hdr.senderClientID = senderID;
        hdr.senderNameLength = nameLen;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        size_t offset = sizeof(hdr);
        if (nameLen > 0)
        {
            std::memcpy(buf.data() + offset, senderName.data(), nameLen);
            offset += nameLen;
        }
        uint16_t textLenNet = textLen;
        std::memcpy(buf.data() + offset, &textLenNet, sizeof(textLenNet));
        offset += sizeof(textLenNet);
        if (textLen > 0)
            std::memcpy(buf.data() + offset, text.data(), textLen);
        return buf;
    }

    // ────────────────────── Chat Readers ──────────────────────

    struct ChatRequestData
    {
        ChatMessageType chatType = ChatMessageType::Public;
        ClientID targetClientID = INVALID_CLIENT_ID;
        std::string text;
    };

    inline ChatRequestData ReadChatRequest(const uint8_t *data, size_t len)
    {
        auto hdr = Read<MsgChatRequest>(data, len);
        ChatRequestData out;
        out.chatType = hdr.chatType;
        out.targetClientID = hdr.targetClientID;
        size_t offset = sizeof(MsgChatRequest);
        uint16_t textLen = hdr.textLength;
        if (textLen > 0 && offset + textLen <= len)
            out.text.assign(reinterpret_cast<const char *>(data + offset), textLen);
        return out;
    }

    struct ChatBroadcastData
    {
        ChatMessageType chatType = ChatMessageType::Public;
        ClientID senderClientID = INVALID_CLIENT_ID;
        std::string senderName;
        std::string text;
    };

    inline ChatBroadcastData ReadChatBroadcast(const uint8_t *data, size_t len)
    {
        auto hdr = Read<MsgChatBroadcast>(data, len);
        ChatBroadcastData out;
        out.chatType = hdr.chatType;
        out.senderClientID = hdr.senderClientID;

        size_t offset = sizeof(MsgChatBroadcast);
        uint8_t nameLen = hdr.senderNameLength;
        if (nameLen > 0 && offset + nameLen <= len)
        {
            out.senderName.assign(reinterpret_cast<const char *>(data + offset), nameLen);
            offset += nameLen;
        }
        if (offset + sizeof(uint16_t) <= len)
        {
            uint16_t textLen = 0;
            std::memcpy(&textLen, data + offset, sizeof(textLen));
            offset += sizeof(textLen);
            if (textLen > 0 && offset + textLen <= len)
                out.text.assign(reinterpret_cast<const char *>(data + offset), textLen);
        }
        return out;
    }

    // ────────────────────── Nickname Writers ──────────────────────

    inline std::vector<uint8_t> WriteNicknameUpdateRequest(const std::string &nickname)
    {
        uint8_t len8 = static_cast<uint8_t>(
            std::min(nickname.size(), static_cast<size_t>(255)));

        size_t totalSize = sizeof(MsgNicknameUpdateRequest) + len8;
        std::vector<uint8_t> buf(totalSize);

        MsgNicknameUpdateRequest hdr;
        hdr.nicknameLength = len8;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        if (len8 > 0)
            std::memcpy(buf.data() + sizeof(hdr), nickname.data(), len8);
        return buf;
    }

    inline std::vector<uint8_t> WriteNicknameUpdateResult(
        NicknameUpdateStatus status, const std::string &nickname)
    {
        uint8_t len8 = static_cast<uint8_t>(
            std::min(nickname.size(), static_cast<size_t>(255)));

        size_t totalSize = sizeof(MsgNicknameUpdateResult) + len8;
        std::vector<uint8_t> buf(totalSize);

        MsgNicknameUpdateResult hdr;
        hdr.status = status;
        hdr.nicknameLength = len8;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        if (len8 > 0)
            std::memcpy(buf.data() + sizeof(hdr), nickname.data(), len8);
        return buf;
    }

    // ────────────────────── Nickname Readers ──────────────────────

    struct NicknameUpdateRequestData
    {
        std::string nickname;
    };

    inline NicknameUpdateRequestData ReadNicknameUpdateRequest(
        const uint8_t *data, size_t len)
    {
        auto hdr = Read<MsgNicknameUpdateRequest>(data, len);
        NicknameUpdateRequestData out;

        size_t offset = sizeof(MsgNicknameUpdateRequest);
        if (hdr.nicknameLength > 0 &&
            offset + hdr.nicknameLength <= len)
        {
            out.nickname.assign(
                reinterpret_cast<const char *>(data + offset),
                hdr.nicknameLength);
        }
        return out;
    }

    struct NicknameUpdateResultData
    {
        NicknameUpdateStatus status = NicknameUpdateStatus::Accepted;
        std::string nickname;
    };

    inline NicknameUpdateResultData ReadNicknameUpdateResult(
        const uint8_t *data, size_t len)
    {
        auto hdr = Read<MsgNicknameUpdateResult>(data, len);
        NicknameUpdateResultData out;
        out.status = hdr.status;

        size_t offset = sizeof(MsgNicknameUpdateResult);
        if (hdr.nicknameLength > 0 &&
            offset + hdr.nicknameLength <= len)
        {
            out.nickname.assign(
                reinterpret_cast<const char *>(data + offset),
                hdr.nicknameLength);
        }
        return out;
    }

} // namespace PacketSerializer
