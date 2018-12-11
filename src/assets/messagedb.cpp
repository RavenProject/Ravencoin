// Copyright (c) 2018 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <validation.h>
#include "messagedb.h"
#include "messages.h"

static const char MESSAGE_FLAG = 'M'; // Message
static const char MY_MESSAGE_CHANNEL = 'C'; // My followed Channels
static const char DB_FLAG = 'D'; // Database Flags

CMessageDB::CMessageDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "messages", nCacheSize, fMemory, fWipe) {
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
                return error("%s: failed to read message", __func__);
            }
        } else {
            break;
        }
    }
    return true;
}

bool CMessageDB::WriteMyMessageChannel(const std::string& channelname)
{
    return Write(std::make_pair(MY_MESSAGE_CHANNEL, channelname), 1);
}

bool CMessageDB::ReadMyMessageChannel(const std::string& channelname)
{
    int i = 1;
    return Read(std::make_pair(MY_MESSAGE_CHANNEL, channelname), i);
}

bool CMessageDB::EraseMyMessageChannel(const std::string& channelname)
{
    return Erase(std::make_pair(MY_MESSAGE_CHANNEL, channelname));
}

bool CMessageDB::LoadMyMessageChannels()
{
    setMyMessageOutPoints.clear();

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