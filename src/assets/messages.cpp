// Copyright (c) 2018 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include "messages.h"
#include "messagedb.h"


std::set<COutPoint> setDirtyMessagesRemove;
std::map<COutPoint, CMessage> mapDirtyMessagesAdd;
std::set<std::string> setDirtyChannelsAdd;
std::set<std::string> setDirtyChannelsRemove;
std::set<std::string> setChannelsAskedForFalse;
std::map<COutPoint, CMessage> mapDirtyMessagesOrphaned;
CCriticalSection cs_messaging;


int8_t IntFromMessageStatus(MessageStatus status)
{
    return (int8_t)status;
}

MessageStatus MessageStatusFromInt(int8_t nStatus)
{
    return (MessageStatus)nStatus;
}

std::string MessageStatusToString(MessageStatus status)
{
    switch (status) {
        case MessageStatus::READ: return "READ";
        case MessageStatus::UNREAD: return "UNREAD";
        case MessageStatus::ORPHAN: return "ORPHAN";
        case MessageStatus::EXPIRED: return "EXPIRED";
        case MessageStatus::SPAM: return "SPAM";
        case MessageStatus::HIDDEN: return "HIDDEN";
        default: return "ERROR";
    }
}

CMessage::CMessage() {
    SetNull();
}

CMessage::CMessage(const COutPoint& out, const std::string& strName, const std::string& ipfsHash, const int64_t& nExpiredTime, const int64_t& time)
{
    SetNull();
    this->out = out;
    this->strName = strName;
    this->ipfsHash = ipfsHash;
    this->nExpiredTime = nExpiredTime;
    this->time = time;
    status = MessageStatus::UNREAD;
}

bool IsChannelWatched(const std::string& name)
{

    return true;
    if (!pMessagesChannelsCache || !pmessagechanneldb)
        return false;

    // Check Dirty Cache for newly added channel additions
    if (setDirtyChannelsAdd.count(name))
        return true;

    // Check Dirty Cache for newly removed channels
    if (setDirtyChannelsRemove.count(name))
        return false;

    // Check the Channel Cache and see if it is in the Cache
    if (pMessagesChannelsCache->Exists(name))
        return true;

    // Check if we have already searched for this before
    if (setChannelsAskedForFalse.count(name))
        return false;

    // Check to see if the message database contains the asset
    if (pmessagechanneldb->ReadMyMessageChannel(name)) {
        pMessagesChannelsCache->Put(name, 1);
        return true;
    }

    // Help prevent spam and unneeded database reads
    setChannelsAskedForFalse.insert(name);

    return false;
}

bool GetMessage(const COutPoint& out, CMessage& message)
{
    if (!pmessagedb || !pMessagesCache)
        return false;

    // Check the dirty add cache
    if (mapDirtyMessagesAdd.count(out)) {
        message = mapDirtyMessagesAdd.at(out);
        return true;
    }

    // Check the dirty remove cache
    if (setDirtyMessagesRemove.count(out))
        return false;

    // Check database cache
    if (pMessagesCache->Exists(out.ToSerializedString())) {
        message = pMessagesCache->Get(out.ToSerializedString());
        return true;
    }

    // Check the database
    if (pmessagedb->ReadMessage(out, message)) {
        pMessagesCache->Put(out.ToSerializedString(), message);
        return true;
    }

    return false;
}

void AddChannel(const std::string& name)
{
    // Add channel to dirty cache to add
    setDirtyChannelsAdd.insert(name);

    // If the channel name is in the dirty remove cache. Remove it so it doesn't get deleted on flush
    setDirtyChannelsRemove.erase(name);
}

void RemoveChannel(const std::string& name)
{
    // Add channel to dirty cache to remove
    setDirtyChannelsRemove.insert(name);

    // If the channel name is in the dirty add cache. Remove it so it doesn't get added on flush
    setDirtyChannelsAdd.erase(name);
}

void AddMessage(const CMessage& message)
{
    // Add message to dirty map cache to add
    mapDirtyMessagesAdd.insert(std::make_pair(message.out, message));

    // Remove message Out from dirty set Cache to remove
    setDirtyMessagesRemove.erase(message.out);
    mapDirtyMessagesOrphaned.erase(message.out);
}

void RemoveMessage(const CMessage& message)
{
    RemoveMessage(message.out);
}

void RemoveMessage(const COutPoint &out)
{
    // Add message out to dirty set Cache to remove
    setDirtyMessagesRemove.insert(out);

    // Remove message from map Dirty Message to add
    mapDirtyMessagesAdd.erase(out);
    mapDirtyMessagesOrphaned.erase(out);
}

void OrphanMessage(const COutPoint &out)
{
    CMessage message;
    if (GetMessage(out, message))
        OrphanMessage(message);
}

void OrphanMessage(const CMessage& message)
{
    mapDirtyMessagesOrphaned[message.out] = message;

    // Remove from other dirty caches
    mapDirtyMessagesAdd.erase(message.out);
}