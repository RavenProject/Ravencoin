// Copyright (c) 2018 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include "messages.h"
#include "messagedb.h"


std::set<COutPoint> setMyMessageOutPoints;
std::set<CMessage> setMyMessages;

CMessage::CMessage(const COutPoint& out, const std::string& strName, const std::string& ipfsHash, const bool fExpired, const int64_t& time)
{
    this->out = out;
    this->strName = strName;
    this->ipfsHash = ipfsHash;
    this->fExpired = fExpired;
    this->time = time;
}

void GetMyMessages()
{
    CMessageDB messageDB(10000);

    for (auto out : setMyMessageOutPoints) {
        // if we already have the message is our set of messages
        CMessage finder;
        finder.out = out;
        if (setMyMessages.count(finder)) {
            continue;
        }

        // Try to find it, either in the cache, or database
        CMessage messageTmp;
        if (pMessagesCache->Exists(out.ToSerializedString())) {
            setMyMessages.insert(pMessagesCache->Get(out.ToSerializedString()));
        } else if (messageDB.ReadMessage(out, messageTmp)) {
            setMyMessages.insert(messageTmp);
            pMessagesCache->Put(out.ToSerializedString(), messageTmp);
        } else {
            LogPrintf("Can't find message with COutPoint: %s\n", out.ToString());
        }
    }
}