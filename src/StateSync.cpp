// ────────────────────────────────────────────────────────────────────
// GameServer state sync and transport helpers
// ────────────────────────────────────────────────────────────────────

extern "C"
{
#include <nbnet.h>
}

#include "GameServer.h"

static uint8_t MapChannel(uint8_t ourChannel)
{
    // our convention: 0 = reliable, 1 = unreliable
    return (ourChannel == 0) ? NBN_CHANNEL_RESERVED_RELIABLE : NBN_CHANNEL_RESERVED_UNRELIABLE;
}

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
