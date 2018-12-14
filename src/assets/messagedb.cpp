// Copyright (c) 2018 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <validation.h>
#include "messagedb.h"
#include "messages.h"

static const char MESSAGE_FLAG = 'Z'; // Message
static const char MY_MESSAGE_CHANNEL = 'C'; // My followed Channels
static const char DB_FLAG = 'D'; // Database Flags

CMessageDB::CMessageDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "messages" / "messages", nCacheSize, fMemory, fWipe) {
}

bool CMessageDB::WriteMessage(const CMessage &message)
{
    return Write(std::make_pair(MESSAGE_FLAG, message.out), message);
}

bool CMessageDB::ReadMessage(const COutPoint &out, CMessage &message)
{
    return Read(std::make_pair(MESSAGE_FLAG, out), message);
}

bool CMessageDB::EraseMessage(const COutPoint &out)
{
    return Erase(std::make_pair(MESSAGE_FLAG, out));
}

bool CMessageDB::LoadMessages(std::set<CMessage>& setMessages)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(MESSAGE_FLAG, COutPoint()));

    // Load messages
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, COutPoint> key;
        if (pcursor->GetKey(key) && key.first == MESSAGE_FLAG) {
            CMessage message;
            if (pcursor->GetValue(message)) {
                setMessages.insert(message);
                pcursor->Next();
            } else {
                LogPrintf("%s: failed to read message\n", __func__);
                pcursor->Next();
            }
        } else {
            break;
        }
    }
    return true;
}

bool CMessageDB::Flush() {
    try {
        LogPrintf("%s: Flushing messages to database removeSize:%u, addSize:%u, orphanSize:%u\n", __func__, setDirtyMessagesRemove.size(), mapDirtyMessagesAdd.size(), mapDirtyMessagesOrphaned.size());
        for (auto messageRemove : setDirtyMessagesRemove) {
            if (!EraseMessage(messageRemove))
                return error("%s: failed to erase message %s", __func__, messageRemove.ToString());
        }

        for (auto messageAdd : mapDirtyMessagesAdd) {
            if (!WriteMessage(messageAdd.second))
                return error("%s: failed to write message %s", __func__, messageAdd.second.ToString());

            mapDirtyMessagesOrphaned.erase(messageAdd.first);
        }

        for (auto orphans : mapDirtyMessagesOrphaned) {
            CMessage msg = orphans.second;
            msg.status = MessageStatus::ORPHAN;
            if (!WriteMessage(msg))
                return error("%s: failed to write message orphan %s", __func__, msg.ToString());
        }

        setDirtyMessagesRemove.clear();
        mapDirtyMessagesAdd.clear();
        mapDirtyMessagesOrphaned.clear();
    } catch (const std::runtime_error& e) {
        return error("%s : %s ", __func__, std::string("System error while flushing messages: ") + e.what());
    }

    return true;
}




CMessageChannelDB::CMessageChannelDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "messages" / "channels", nCacheSize, fMemory, fWipe) {
}

bool CMessageChannelDB::WriteMyMessageChannel(const std::string& channelname)
{
    return Write(std::make_pair(MY_MESSAGE_CHANNEL, channelname), 1);
}

bool CMessageChannelDB::ReadMyMessageChannel(const std::string& channelname)
{
    int i = 1;
    return Read(std::make_pair(MY_MESSAGE_CHANNEL, channelname), i);
}

bool CMessageChannelDB::EraseMyMessageChannel(const std::string& channelname)
{
    return Erase(std::make_pair(MY_MESSAGE_CHANNEL, channelname));
}

bool CMessageChannelDB::LoadMyMessageChannels()
{
    pMessagesChannelsCache->Clear();
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(MY_MESSAGE_CHANNEL, std::string()));

    // Load messages
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, std::string> key;
        if (pcursor->GetKey(key) && key.first == MY_MESSAGE_CHANNEL) {
            pMessagesChannelsCache->Put(key.second, 1);
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CMessageDB::WriteFlag(const std::string &name, bool fValue)
{
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CMessageDB::ReadFlag(const std::string &name, bool &fValue)
{
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}