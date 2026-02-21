// ────────────────────────────────────────────────────────────────────
// GameServer chat and nickname logic
// ────────────────────────────────────────────────────────────────────

#include "GameServer.h"

#include <cctype>

static std::string TrimSpaces(const std::string &text)
{
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }

    if (begin >= text.size())
        return "";

    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }

    return text.substr(begin, end - begin);
}

static constexpr size_t MAX_CHAT_TEXT_LEN = 256;
static constexpr size_t MAX_NICKNAME_LEN = 16;
static constexpr size_t MIN_NICKNAME_LEN = 3;
static constexpr std::chrono::milliseconds CHAT_RATE_LIMIT{300}; // 0.3 s

std::string GameServer::GetClientDisplayName(ClientID clientID) const
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end() || it->second.nickname.empty())
        return "Player " + std::to_string(clientID);
    return it->second.nickname;
}

std::string GameServer::NormalizeNickname(const std::string &nickname)
{
    std::string out;
    out.reserve(nickname.size());
    for (char ch : nickname)
    {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return out;
}

bool GameServer::IsValidNickname(const std::string &nickname)
{
    if (nickname.size() < MIN_NICKNAME_LEN || nickname.size() > MAX_NICKNAME_LEN)
        return false;
    for (char ch : nickname)
    {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) || c == '_')
            continue;
        return false;
    }
    return true;
}

void GameServer::SendNicknameUpdateResult(ClientID clientID,
                                          NicknameUpdateStatus status,
                                          const std::string &nickname)
{
    auto pkt = PacketSerializer::WriteNicknameUpdateResult(status, nickname);
    SendTo(clientID, pkt.data(), pkt.size(), 0);
}

void GameServer::HandleNicknameUpdateRequest(ClientID clientID,
                                             const uint8_t *data, size_t len)
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end() || !it->second.welcomed)
        return;

    auto req = PacketSerializer::ReadNicknameUpdateRequest(data, len);
    const std::string requested = req.nickname;
    const std::string currentNorm = NormalizeNickname(GetClientDisplayName(clientID));
    const std::string requestedNorm = NormalizeNickname(requested);

    if (requestedNorm == currentNorm)
    {
        // Idempotent update: keep quiet, only acknowledge.
        SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Accepted,
                                 GetClientDisplayName(clientID));
        return;
    }

    if (!IsValidNickname(requested))
    {
        SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Invalid,
                                 GetClientDisplayName(clientID));
        return;
    }

    auto existing = m_nicknameIndex.find(requestedNorm);
    if (existing != m_nicknameIndex.end() && existing->second != clientID)
    {
        SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Conflict,
                                 GetClientDisplayName(clientID));
        return;
    }

    if (!it->second.nickname.empty())
    {
        m_nicknameIndex.erase(NormalizeNickname(it->second.nickname));
    }
    it->second.nickname = requested;
    m_nicknameIndex[requestedNorm] = clientID;

    SendNicknameUpdateResult(clientID, NicknameUpdateStatus::Accepted, requested);
    SendSystemMessage("Your nickname is now '" + requested + "'.", clientID);
}

void GameServer::HandleChatRequest(ClientID clientID,
                                   const uint8_t *data, size_t len)
{
    auto it = m_clients.find(clientID);
    if (it == m_clients.end() || !it->second.welcomed)
        return;

    auto req = PacketSerializer::ReadChatRequest(data, len);

    // ── Validation ─────────────────────────────────────────────────
    // 1. Text length check
    if (req.text.empty() || req.text.size() > MAX_CHAT_TEXT_LEN)
    {
        std::cerr << "[GameServer] Chat rejected from " << clientID
                  << ": invalid text length (" << req.text.size() << ")\n";
        return;
    }

    // 2. Rate limit
    auto now = std::chrono::steady_clock::now();
    if ((now - it->second.lastChatTime) < CHAT_RATE_LIMIT)
    {
        std::cerr << "[GameServer] Chat rate-limited for " << clientID << "\n";
        SendSystemMessage("Message rate-limited. Please slow down.", clientID);
        return;
    }
    it->second.lastChatTime = now;

    const std::string senderName = GetClientDisplayName(clientID);

    auto setPublicMode = [&]()
    {
        it->second.whisperTargetID = INVALID_CLIENT_ID;
        it->second.whisperTargetNickname.clear();
    };

    auto sendHelp = [this, clientID]()
    {
        SendSystemMessage(
            "Available chat commands:\n"
            "/w <nickname> - enter whisper mode (supports spaces in nickname).\n"
            "/a - return to public chat.\n"
            "/help - show this help message.",
            clientID);
    };

    if (!req.text.empty() && req.text[0] == '/')
    {
        if (req.text == "/help")
        {
            sendHelp();
            return;
        }

        if (req.text.rfind("/a", 0) == 0 &&
            TrimSpaces(req.text.substr(2)).empty())
        {
            setPublicMode();
            SendSystemMessage(
                "[CHAT_MODE:PUBLIC] Switched to public chat.",
                clientID);
            return;
        }

        if (req.text == "/w" || req.text.rfind("/w ", 0) == 0)
        {
            const std::string targetNickname = TrimSpaces(req.text.substr(2));

            if (targetNickname.empty())
            {
                SendSystemMessage("Usage: /w <nickname>", clientID);
                return;
            }

            const std::string targetNorm = NormalizeNickname(targetNickname);
            auto targetIdIt = m_nicknameIndex.find(targetNorm);
            if (targetIdIt == m_nicknameIndex.end())
            {
                setPublicMode();
                SendSystemMessage(
                    "[CHAT_MODE:PUBLIC] Player '" + targetNickname +
                        "' is not online. Switched to public chat.",
                    clientID);
                return;
            }

            const ClientID targetID = targetIdIt->second;
            auto targetStateIt = m_clients.find(targetID);
            if (targetStateIt == m_clients.end() || !targetStateIt->second.welcomed)
            {
                setPublicMode();
                SendSystemMessage(
                    "[CHAT_MODE:PUBLIC] Player '" + targetNickname +
                        "' is not online. Switched to public chat.",
                    clientID);
                return;
            }

            const std::string targetDisplayName = GetClientDisplayName(targetID);
            it->second.whisperTargetID = targetID;
            it->second.whisperTargetNickname = targetDisplayName;
            SendSystemMessage(
                "[CHAT_MODE:WHISPER:" + targetDisplayName +
                    "] Whisper mode on for '" + targetDisplayName +
                    "'. Use /a to return to public chat.",
                clientID);
            return;
        }

        SendSystemMessage("Unknown command. Type /help for commands.", clientID);
        return;
    }

    if (it->second.whisperTargetID != INVALID_CLIENT_ID)
    {
        const ClientID targetID = it->second.whisperTargetID;
        auto targetStateIt = m_clients.find(targetID);
        if (targetStateIt == m_clients.end() || !targetStateIt->second.welcomed)
        {
            const std::string offlineName =
                it->second.whisperTargetNickname.empty()
                    ? "selected player"
                    : ("'" + it->second.whisperTargetNickname + "'");
            setPublicMode();
            SendSystemMessage(
                "[CHAT_MODE:PUBLIC] Whisper target " + offlineName +
                    " is offline. Switched to public chat.",
                clientID);
            return;
        }

        const std::string targetDisplayName = GetClientDisplayName(targetID);
        it->second.whisperTargetNickname = targetDisplayName;

        std::cout << "[Chat] [Whisper] " << senderName << " -> "
                  << targetDisplayName << ": " << req.text << "\n";

        SendChatTo(targetID, ChatMessageType::Whisper,
                   clientID, senderName, req.text);

        if (targetID != clientID)
        {
            SendChatTo(clientID, ChatMessageType::Whisper,
                       clientID, senderName, req.text);
        }
        return;
    }

    switch (req.chatType)
    {
    case ChatMessageType::Public:
    {
        std::cout << "[Chat] [Public] " << senderName << ": " << req.text << "\n";
        BroadcastChat(ChatMessageType::Public, clientID, senderName, req.text);
        break;
    }
    case ChatMessageType::Whisper:
    {
        SendSystemMessage("Use /w <nickname> to enter whisper mode.", clientID);
        break;
    }
    case ChatMessageType::System:
        // Clients are not allowed to send system messages
        std::cerr << "[GameServer] Client " << clientID
                  << " tried to send a system message\n";
        break;
    }
}

void GameServer::BroadcastChat(ChatMessageType chatType, ClientID senderID,
                               const std::string &senderName, const std::string &text)
{
    auto pkt = PacketSerializer::WriteChatBroadcast(chatType, senderID, senderName, text);
    for (auto &[id, cs] : m_clients)
    {
        (void)id;
        if (!cs.welcomed)
            continue;
        SendTo(cs.id, pkt.data(), pkt.size(), 0); // reliable
    }
}

void GameServer::SendChatTo(ClientID targetID, ChatMessageType chatType,
                            ClientID senderID, const std::string &senderName,
                            const std::string &text)
{
    auto pkt = PacketSerializer::WriteChatBroadcast(chatType, senderID, senderName, text);
    SendTo(targetID, pkt.data(), pkt.size(), 0); // reliable
}

void GameServer::SendSystemMessage(const std::string &text, ClientID targetID)
{
    if (targetID != INVALID_CLIENT_ID)
    {
        // Send to specific client
        auto pkt = PacketSerializer::WriteChatBroadcast(
            ChatMessageType::System, INVALID_CLIENT_ID, "System", text);
        SendTo(targetID, pkt.data(), pkt.size(), 0);
    }
    else
    {
        // Broadcast to all
        BroadcastChat(ChatMessageType::System, INVALID_CLIENT_ID, "System", text);
    }
}
