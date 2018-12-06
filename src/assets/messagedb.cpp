// Copyright (c) 2018 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "messagedb.h"
#include "messages.h"

static const char MESSAGE_FLAG = 'M'; // Message
static const char MY_MESSAGE_OUTPOINT_FLAG = 'O'; // Outpoints of messages we need to show

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

bool CMessageDB::WriteMyMessageOutPoint(const COutPoint &out)
{
    return Write(std::make_pair(MY_MESSAGE_OUTPOINT_FLAG, out), 0);
}

bool CMessageDB::EraseMyMessageOutPoint(const COutPoint &out)
{
    return Erase(std::make_pair(MY_MESSAGE_OUTPOINT_FLAG, out));
}

bool CMessageDB::LoadMyMessageOutPoints()
{
    setMyMessageOutPoints.clear();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(MY_MESSAGE_OUTPOINT_FLAG, COutPoint()));

    // Load messages
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, COutPoint> key;
        if (pcursor->GetKey(key) && key.first == MY_MESSAGE_OUTPOINT_FLAG) {
            setMyMessageOutPoints.insert(key.second);
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}