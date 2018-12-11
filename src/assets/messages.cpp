// Copyright (c) 2018 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include "messages.h"
#include "messagedb.h"


std::set<COutPoint> setMyMessageOutPoints;
std::set<CMessage> setMyMessages;


int IntFromMessageStatus(MessageStatus status)
{
    return (int)status;
}

MessageStatus MessageStatusFromInt(int nStatus)
{
    return (MessageStatus)nStatus;
}

CMessage::CMessage(const COutPoint& out, const std::string& strName, const std::string& ipfsHash, const int64_t& nExpiredTime, const int64_t& time)
{
    this->out = out;
    this->strName = strName;
    this->ipfsHash = ipfsHash;
    this->nExpiredTime = nExpiredTime;
    this->time = time;
    status = MessageStatus::UNREAD;
}