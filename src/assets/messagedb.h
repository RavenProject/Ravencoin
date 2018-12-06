// Copyright (c) 2018 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_MESSAGEDB_H
#define RAVENCOIN_MESSAGEDB_H



#include <dbwrapper.h>

class CMessage;
class COutPoint;

class CMessageDB  : public CDBWrapper {

public:
    explicit CMessageDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CMessageDB(const CMessageDB&) = delete;
    CMessageDB& operator=(const CMessageDB&) = delete;

    // Full database of messages
    bool WriteMessage(const CMessage& message);
    bool ReadMessage(const COutPoint& out, CMessage& message);
    bool EraseMessage(const COutPoint& out);
    bool LoadMessages(std::set<CMessage>& setMessages);

    // My messages to show
    bool WriteMyMessageOutPoint(const COutPoint &out);
    bool EraseMyMessageOutPoint(const COutPoint &out);
    bool LoadMyMessageOutPoints();
};


#endif //RAVENCOIN_MESSAGEDB_H
