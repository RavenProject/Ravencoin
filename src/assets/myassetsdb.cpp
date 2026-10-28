// Copyright (c) 2018-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "validation.h"
#include "assets.h"
#include "myassetsdb.h"
#include "messages.h"
#include <boost/thread.hpp>
#include <algorithm>

#include <boost/thread.hpp>

static const char MESSAGE_FLAG = 'Z'; // Message
static const char WALLET_MESSAGE_FLAG = 'W';
static const char WALLET_MESSAGE_SCOPE_FLAG = 'w';
static const char WALLET_MESSAGE_TOMBSTONE = 'x';
static const char MY_MESSAGE_CHANNEL = 'C'; // My followed Channels
static const char MY_SEEN_ADDRESSES = 'S'; // Addresses that have been seen on the chain
static const char WALLET_MESSAGE_CHANNEL = 'c';
static const char WALLET_CHANNEL_SUPPRESSION = 'u';
static const char WALLET_CHANNEL_PREFERENCE = 'p';
static const char WALLET_CHANNEL_SUBSCRIPTION_ANCHOR = 'a';
static const char WALLET_CHANNEL_EVENT = 'e';
static const char WALLET_AUTO_CHANNEL_EVENT = 'f';
static const char WALLET_SEEN_ADDRESS = 's';
static const char WALLET_MESSAGE_SCAN_TIP = 't';
static const char DB_FLAG = 'D'; // Database Flags

static const char MY_TAGGED_ADDRESSES = 'T'; // Addresses that have been tagged
static const char MY_RESTRICTED_ADDRESSES = 'R'; // Addresses that have been restricted
static const char MY_RESTRICTED_UNDO = 'U';

using RestrictedEventKey = std::pair<char, std::pair<std::string, std::string>>;
using RestrictedEventValue = std::pair<int, uint32_t>;
using RestrictedBlockUndo = std::vector<std::pair<RestrictedEventKey,
                                                   std::pair<bool, RestrictedEventValue>>>;

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

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        char flag;
        if (!pcursor->GetKey(flag))
            return false;
        if (flag != MESSAGE_FLAG)
            break;
        std::pair<char, COutPoint> key;
        if (!pcursor->GetKey(key))
            return false;
        CMessage message;
        if (!pcursor->GetValue(message) || message.out != key.second)
            return false;
        setMessages.insert(message);
        pcursor->Next();
    }
    return true;
}

bool CMessageDB::EraseAllMessages(int& count)
{
    std::set<CMessage> setMessages;
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

    count += setMessages.size();
    for (auto message : setMessages)
        EraseMessage(message.out);

    return true;
}

bool CMessageDB::WriteWalletMessage(const std::string& walletScope, const CMessage& message)
{
    if (!Write(std::make_pair(WALLET_MESSAGE_SCOPE_FLAG, walletScope), 1))
        return false;
    return Write(std::make_pair(WALLET_MESSAGE_FLAG, std::make_pair(walletScope, message.out)), message);
}

bool CMessageDB::ReadWalletMessage(const std::string& walletScope, const COutPoint& out, CMessage& message)
{
    return Read(std::make_pair(WALLET_MESSAGE_FLAG, std::make_pair(walletScope, out)), message);
}

bool CMessageDB::EraseWalletMessage(const std::string& walletScope, const COutPoint& out)
{
    return Erase(std::make_pair(WALLET_MESSAGE_FLAG, std::make_pair(walletScope, out)));
}

bool CMessageDB::LoadWalletMessages(const std::string& walletScope, std::set<CMessage>& messages)
{
    std::unique_ptr<CDBIterator> cursor(NewIterator());
    cursor->Seek(std::make_pair(WALLET_MESSAGE_FLAG,
                                std::make_pair(walletScope, COutPoint())));
    while (cursor->Valid()) {
        boost::this_thread::interruption_point();
        char flag;
        if (!cursor->GetKey(flag) || flag != WALLET_MESSAGE_FLAG)
            break;
        std::pair<char, std::pair<std::string, COutPoint>> key;
        if (!cursor->GetKey(key))
            return false;
        if (key.second.first != walletScope)
            break;
        CMessage message;
        if (!cursor->GetValue(message))
            return false;
        messages.insert(message);
        cursor->Next();
    }
    return true;
}

bool CMessageDB::LoadWalletMessagesForOut(
    const COutPoint& out, std::vector<std::pair<std::string, CMessage>>& messages)
{
    std::unique_ptr<CDBIterator> cursor(NewIterator());
    cursor->Seek(std::make_pair(WALLET_MESSAGE_SCOPE_FLAG, std::string()));
    std::vector<std::string> scopes;
    while (cursor->Valid()) {
        boost::this_thread::interruption_point();
        char flag;
        if (!cursor->GetKey(flag) || flag != WALLET_MESSAGE_SCOPE_FLAG)
            break;
        std::pair<char, std::string> key;
        if (!cursor->GetKey(key))
            return false;
        scopes.push_back(key.second);
        cursor->Next();
    }
    for (const std::string& scope : scopes) {
        CMessage message;
        if (ReadWalletMessage(scope, out, message))
            messages.emplace_back(scope, message);
    }
    return true;
}

bool CMessageDB::WriteWalletMessageTombstone(const std::string& walletScope,
                                             const COutPoint& out)
{
    return Write(std::make_pair(WALLET_MESSAGE_TOMBSTONE,
                                std::make_pair(walletScope, out)), 1);
}

bool CMessageDB::ReadWalletMessageTombstone(const std::string& walletScope,
                                            const COutPoint& out)
{
    int value = 0;
    return Read(std::make_pair(WALLET_MESSAGE_TOMBSTONE,
                               std::make_pair(walletScope, out)), value);
}

bool CMessageDB::EraseAllWalletMessages(const std::string& walletScope, int& count)
{
    std::set<CMessage> messages;
    if (!LoadWalletMessages(walletScope, messages))
        return false;
    for (const CMessage& message : messages) {
        if (!EraseWalletMessage(walletScope, message.out))
            return false;
        ++count;
    }
    return true;
}

bool CMessageDB::OrphanWalletMessages(const std::string& walletScope)
{
    std::set<CMessage> messages;
    if (!LoadWalletMessages(walletScope, messages))
        return false;
    CDBBatch batch(*this);
    for (CMessage message : messages) {
        if (message.status == MessageStatus::ORPHAN)
            continue;
        message.status = MessageStatus::ORPHAN;
        batch.Write(std::make_pair(WALLET_MESSAGE_FLAG,
                                   std::make_pair(walletScope, message.out)), message);
    }
    return WriteBatch(batch, true);
}

bool CMessageDB::Flush() {
    try {

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

        for (const auto& entry : setDirtyWalletMessagesRemove) {
            if (!EraseWalletMessage(entry.first, entry.second))
                return error("%s: failed to erase wallet message %s", __func__, entry.second.ToString());
        }
        for (const auto& entry : mapDirtyWalletMessagesAdd) {
            if (!WriteWalletMessage(entry.first.first, entry.second))
                return error("%s: failed to write wallet message %s", __func__, entry.second.ToString());
        }
        for (const auto& entry : mapDirtyWalletMessagesOrphaned) {
            CMessage orphan = entry.second;
            orphan.status = MessageStatus::ORPHAN;
            if (!WriteWalletMessage(entry.first.first, orphan))
                return error("%s: failed to write wallet orphan message %s", __func__, orphan.ToString());
        }

        setDirtyMessagesRemove.clear();
        mapDirtyMessagesAdd.clear();
        mapDirtyMessagesOrphaned.clear();
        setDirtyWalletMessagesRemove.clear();
        mapDirtyWalletMessagesAdd.clear();
        mapDirtyWalletMessagesOrphaned.clear();
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

bool CMessageChannelDB::LoadMyMessageChannels(std::set<std::string>& setChannels)
{
    setChannels.clear();
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(MY_MESSAGE_CHANNEL, std::string()));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        char flag;
        if (!pcursor->GetKey(flag))
            return false;
        if (flag != MY_MESSAGE_CHANNEL)
            break;
        std::pair<char, std::string> key;
        int value = 0;
        if (!pcursor->GetKey(key) || !pcursor->GetValue(value) || value != 1)
            return false;
        setChannels.insert(key.second);
        pcursor->Next();
    }

    return true;
}

bool CMessageChannelDB::WriteWalletMessageChannel(const std::string& walletScope,
                                                   const std::string& channelname)
{
    return Write(std::make_pair(WALLET_MESSAGE_CHANNEL,
                                std::make_pair(walletScope, channelname)), 1);
}

bool CMessageChannelDB::ReadWalletMessageChannel(const std::string& walletScope,
                                                  const std::string& channelname)
{
    int value = 0;
    return Read(std::make_pair(WALLET_MESSAGE_CHANNEL,
                               std::make_pair(walletScope, channelname)), value);
}

bool CMessageChannelDB::EraseWalletMessageChannel(const std::string& walletScope,
                                                   const std::string& channelname)
{
    return Erase(std::make_pair(WALLET_MESSAGE_CHANNEL,
                                std::make_pair(walletScope, channelname)));
}

bool CMessageChannelDB::LoadWalletMessageChannels(const std::string& walletScope,
                                                   std::set<std::string>& channels)
{
    channels.clear();
    std::unique_ptr<CDBIterator> cursor(NewIterator());
    cursor->Seek(std::make_pair(WALLET_MESSAGE_CHANNEL,
                                std::make_pair(walletScope, std::string())));
    while (cursor->Valid()) {
        boost::this_thread::interruption_point();
        char flag;
        if (!cursor->GetKey(flag) || flag != WALLET_MESSAGE_CHANNEL)
            break;
        std::pair<char, std::pair<std::string, std::string>> key;
        if (!cursor->GetKey(key))
            return false;
        if (key.second.first != walletScope)
            break;
        channels.insert(key.second.second);
        cursor->Next();
    }
    return true;
}

bool CMessageChannelDB::WriteWalletChannelSuppression(const std::string& walletScope,
                                                       const std::string& channelname,
                                                       int height)
{
    return Write(std::make_pair(WALLET_CHANNEL_SUPPRESSION,
                                std::make_pair(walletScope, channelname)), height);
}

bool CMessageChannelDB::ReadWalletChannelSuppression(const std::string& walletScope,
                                                      const std::string& channelname,
                                                      int& height)
{
    return Read(std::make_pair(WALLET_CHANNEL_SUPPRESSION,
                               std::make_pair(walletScope, channelname)), height);
}

bool CMessageChannelDB::EraseWalletChannelSuppression(const std::string& walletScope,
                                                       const std::string& channelname)
{
    return Erase(std::make_pair(WALLET_CHANNEL_SUPPRESSION,
                                std::make_pair(walletScope, channelname)));
}

bool CMessageChannelDB::WriteWalletChannelPreference(const std::string& walletScope,
                                                      const std::string& channelname)
{
    return Write(std::make_pair(WALLET_CHANNEL_PREFERENCE,
                                std::make_pair(walletScope, channelname)), 1);
}

bool CMessageChannelDB::ReadWalletChannelPreference(const std::string& walletScope,
                                                     const std::string& channelname)
{
    int value = 0;
    return Read(std::make_pair(WALLET_CHANNEL_PREFERENCE,
                               std::make_pair(walletScope, channelname)), value);
}

bool CMessageChannelDB::EraseWalletChannelPreference(const std::string& walletScope,
                                                      const std::string& channelname)
{
    return Erase(std::make_pair(WALLET_CHANNEL_PREFERENCE,
                                std::make_pair(walletScope, channelname)));
}

bool CMessageChannelDB::WriteWalletChannelSubscription(const std::string& walletScope,
                                                        const std::string& channelname,
                                                        int height, const uint256& blockHash)
{
    const auto key = std::make_pair(walletScope, channelname);
    const auto eventKey = std::make_pair(WALLET_CHANNEL_EVENT, key);
    std::vector<WalletChannelEvent> events;
    if (Exists(eventKey) && !Read(eventKey, events))
        return false;
    events.emplace_back(std::make_pair(height, blockHash), true);
    CDBBatch batch(*this);
    batch.Write(eventKey, events);
    batch.Write(std::make_pair(WALLET_CHANNEL_SUBSCRIPTION_ANCHOR, key),
                std::make_pair(height, blockHash));
    batch.Write(std::make_pair(WALLET_CHANNEL_PREFERENCE, key), 1);
    batch.Erase(std::make_pair(WALLET_CHANNEL_SUPPRESSION, key));
    return WriteBatch(batch, true);
}

bool CMessageChannelDB::WriteWalletChannelUnsubscription(const std::string& walletScope,
                                                          const std::string& channelname,
                                                          int height, const uint256& blockHash)
{
    const auto key = std::make_pair(walletScope, channelname);
    const auto eventKey = std::make_pair(WALLET_CHANNEL_EVENT, key);
    std::vector<WalletChannelEvent> events;
    if (Exists(eventKey) && !Read(eventKey, events))
        return false;
    events.emplace_back(std::make_pair(height, blockHash), false);
    CDBBatch batch(*this);
    batch.Write(eventKey, events);
    batch.Erase(std::make_pair(WALLET_CHANNEL_PREFERENCE, key));
    batch.Write(std::make_pair(WALLET_CHANNEL_SUPPRESSION, key), height);
    return WriteBatch(batch, true);
}

bool CMessageChannelDB::LoadWalletChannelEvents(
    const std::string& walletScope,
    std::map<std::string, std::vector<WalletChannelEvent>>& events)
{
    std::unique_ptr<CDBIterator> cursor(NewIterator());
    cursor->Seek(std::make_pair(WALLET_CHANNEL_EVENT,
                                std::make_pair(walletScope, std::string())));
    while (cursor->Valid()) {
        char flag;
        if (!cursor->GetKey(flag))
            return false;
        if (flag != WALLET_CHANNEL_EVENT)
            break;
        std::pair<char, std::pair<std::string, std::string>> key;
        if (!cursor->GetKey(key))
            return false;
        if (key.second.first != walletScope)
            break;
        std::vector<WalletChannelEvent> channelEvents;
        if (!cursor->GetValue(channelEvents))
            return false;
        events.emplace(key.second.second, std::move(channelEvents));
        cursor->Next();
    }
    return true;
}

bool CMessageChannelDB::WriteWalletChannelEvents(
    const std::string& walletScope, const std::string& channelname,
    const std::vector<WalletChannelEvent>& events,
    bool updateSuppression, int suppressionHeight)
{
    const auto key = std::make_pair(walletScope, channelname);
    CDBBatch batch(*this);
    batch.Write(std::make_pair(WALLET_CHANNEL_EVENT, key), events);
    if (updateSuppression) {
        if (suppressionHeight >= 0)
            batch.Write(std::make_pair(WALLET_CHANNEL_SUPPRESSION, key),
                        suppressionHeight);
        else
            batch.Erase(std::make_pair(WALLET_CHANNEL_SUPPRESSION, key));
    }
    return WriteBatch(batch, true);
}

bool CMessageChannelDB::ReadWalletAutoChannelEvents(
    const std::string& walletScope, const std::string& channelname,
    std::vector<WalletAutoChannelEvent>& events)
{
    return Read(std::make_pair(WALLET_AUTO_CHANNEL_EVENT,
                               std::make_pair(walletScope, channelname)), events);
}

bool CMessageChannelDB::WriteWalletAutoChannelEvents(
    const std::string& walletScope, const std::string& channelname,
    const std::vector<WalletAutoChannelEvent>& events)
{
    return Write(std::make_pair(WALLET_AUTO_CHANNEL_EVENT,
                                std::make_pair(walletScope, channelname)), events);
}

bool CMessageChannelDB::LoadWalletAutoChannelEvents(
    const std::string& walletScope,
    std::map<std::string, std::vector<WalletAutoChannelEvent>>& events)
{
    std::unique_ptr<CDBIterator> cursor(NewIterator());
    cursor->Seek(std::make_pair(WALLET_AUTO_CHANNEL_EVENT,
                                std::make_pair(walletScope, std::string())));
    while (cursor->Valid()) {
        char flag;
        if (!cursor->GetKey(flag))
            return false;
        if (flag != WALLET_AUTO_CHANNEL_EVENT)
            break;
        std::pair<char, std::pair<std::string, std::string>> key;
        if (!cursor->GetKey(key))
            return false;
        if (key.second.first != walletScope)
            break;
        std::vector<WalletAutoChannelEvent> channelEvents;
        if (!cursor->GetValue(channelEvents))
            return false;
        events.emplace(key.second.second, std::move(channelEvents));
        cursor->Next();
    }
    return true;
}

bool CMessageChannelDB::WriteWalletChannelSubscriptionAnchor(const std::string& walletScope,
                                                              const std::string& channelname,
                                                              int height, const uint256& blockHash)
{
    return Write(std::make_pair(WALLET_CHANNEL_SUBSCRIPTION_ANCHOR,
                                std::make_pair(walletScope, channelname)),
                 std::make_pair(height, blockHash), true);
}

bool CMessageChannelDB::ReadWalletChannelSubscriptionAnchor(const std::string& walletScope,
                                                             const std::string& channelname,
                                                             int& height, uint256& blockHash)
{
    std::pair<int, uint256> anchor;
    if (!Read(std::make_pair(WALLET_CHANNEL_SUBSCRIPTION_ANCHOR,
                             std::make_pair(walletScope, channelname)), anchor))
        return false;
    height = anchor.first;
    blockHash = anchor.second;
    return true;
}

bool CMessageChannelDB::LoadWalletChannelPreferences(const std::string& walletScope,
                                                      std::set<std::string>& channels)
{
    std::unique_ptr<CDBIterator> cursor(NewIterator());
    cursor->Seek(std::make_pair(WALLET_CHANNEL_PREFERENCE,
                                std::make_pair(walletScope, std::string())));
    while (cursor->Valid()) {
        char flag;
        if (!cursor->GetKey(flag) || flag != WALLET_CHANNEL_PREFERENCE)
            break;
        std::pair<char, std::pair<std::string, std::string>> key;
        if (!cursor->GetKey(key))
            return false;
        if (key.second.first != walletScope)
            break;
        channels.insert(key.second.second);
        cursor->Next();
    }
    return true;
}

bool CMessageChannelDB::EraseWalletDerivedMessageState(const std::string& walletScope)
{
    for (char flag : {WALLET_MESSAGE_CHANNEL, WALLET_SEEN_ADDRESS,
                      WALLET_AUTO_CHANNEL_EVENT}) {
        std::vector<std::pair<char, std::pair<std::string, std::string>>> keys;
        std::unique_ptr<CDBIterator> cursor(NewIterator());
        cursor->Seek(std::make_pair(flag, std::make_pair(walletScope, std::string())));
        while (cursor->Valid()) {
            char currentFlag;
            if (!cursor->GetKey(currentFlag) || currentFlag != flag)
                break;
            std::pair<char, std::pair<std::string, std::string>> key;
            if (!cursor->GetKey(key))
                return false;
            if (key.second.first != walletScope)
                break;
            keys.push_back(key);
            cursor->Next();
        }
        for (const auto& key : keys) {
            if (!Erase(key))
                return false;
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


bool CMessageChannelDB::WriteUsedAddress(const std::string& address)
{
    return Write(std::make_pair(MY_SEEN_ADDRESSES, address), 1);
}
bool CMessageChannelDB::ReadUsedAddress(const std::string& address)
{
    int i;
    return Read(std::make_pair(MY_SEEN_ADDRESSES, address), i);
}
bool CMessageChannelDB::EraseUsedAddress(const std::string& address)
{
    return Erase(std::make_pair(MY_SEEN_ADDRESSES, address));
}

bool CMessageChannelDB::WriteWalletUsedAddress(const std::string& walletScope,
                                                const std::string& address)
{
    return Write(std::make_pair(WALLET_SEEN_ADDRESS,
                                std::make_pair(walletScope, address)), 1);
}

bool CMessageChannelDB::ReadWalletUsedAddress(const std::string& walletScope,
                                               const std::string& address)
{
    int value = 0;
    return Read(std::make_pair(WALLET_SEEN_ADDRESS,
                               std::make_pair(walletScope, address)), value);
}

bool CMessageChannelDB::WriteWalletMessageScanTip(const std::string& walletScope,
                                                   int height, const uint256& blockHash)
{
    return Write(std::make_pair(WALLET_MESSAGE_SCAN_TIP, walletScope),
                 std::make_pair(height, blockHash));
}

bool CMessageChannelDB::CompleteWalletMessageScan(const std::string& walletScope,
                                                   int height, const uint256& blockHash)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(WALLET_MESSAGE_SCAN_TIP, walletScope),
                std::make_pair(height, blockHash));
    batch.Write(std::make_pair(DB_FLAG, "init:" + walletScope), '1');
    return WriteBatch(batch, true);
}

bool CMessageChannelDB::ReadWalletMessageScanTip(const std::string& walletScope,
                                                  int& height, uint256& blockHash)
{
    std::pair<int, uint256> tip;
    if (!Read(std::make_pair(WALLET_MESSAGE_SCAN_TIP, walletScope), tip))
        return false;
    height = tip.first;
    blockHash = tip.second;
    return true;
}

bool CMessageChannelDB::LoadWalletMessageScanScopes(std::set<std::string>& scopes)
{
    std::unique_ptr<CDBIterator> cursor(NewIterator());
    cursor->Seek(std::make_pair(WALLET_MESSAGE_SCAN_TIP, std::string()));
    while (cursor->Valid()) {
        char flag;
        if (!cursor->GetKey(flag))
            return false;
        if (flag != WALLET_MESSAGE_SCAN_TIP)
            break;
        std::pair<char, std::string> key;
        if (!cursor->GetKey(key))
            return false;
        scopes.insert(key.second);
        cursor->Next();
    }
    return true;
}

bool CMessageChannelDB::InvalidateWalletMessageScan(const std::string& walletScope)
{
    CDBBatch batch(*this);
    batch.Write(std::make_pair(WALLET_MESSAGE_SCAN_TIP, walletScope),
                std::make_pair(-1, uint256()));
    batch.Write(std::make_pair(DB_FLAG, "init:" + walletScope), '0');
    return WriteBatch(batch, true);
}

bool CMessageChannelDB::WriteFlag(const std::string &name, bool fValue)
{
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CMessageChannelDB::ReadFlag(const std::string &name, bool &fValue)
{
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CMessageChannelDB::Flush() {
    try {
        LogPrintf("%s: Flushing messagechannelsdb addSize:%u, removeSize:%u, seenAddressSize:%u\n", __func__, setDirtyChannelsAdd.size(), setDirtyChannelsRemove.size(), setDirtySeenAddressAdd.size());

        for (auto channelRemove : setDirtyChannelsRemove) {
            if (!EraseMyMessageChannel(channelRemove))
                return error("%s: failed to erase messagechannel %s", __func__, channelRemove);
        }

        for (auto channelAdd : setDirtyChannelsAdd) {
            if (!WriteMyMessageChannel(channelAdd))
                return error("%s: failed to write messagechannel %s", __func__, channelAdd);
        }

        for (auto seenAddress : setDirtySeenAddressAdd) {
            if (!WriteUsedAddress(seenAddress))
                return error("%s: failed to write seenaddress %s", __func__, seenAddress);
        }

        for (const auto& channel : setDirtyWalletChannelsRemove) {
            if (!EraseWalletMessageChannel(channel.first, channel.second))
                return error("%s: failed to erase wallet channel %s", __func__, channel.second);
        }
        for (const auto& channel : setDirtyWalletChannelsAdd) {
            if (!WriteWalletMessageChannel(channel.first, channel.second))
                return error("%s: failed to write wallet channel %s", __func__, channel.second);
        }
        for (const auto& address : setDirtyWalletSeenAddressesAdd) {
            if (!WriteWalletUsedAddress(address.first, address.second))
                return error("%s: failed to write wallet seen address %s", __func__, address.second);
        }
        for (const auto& entry : mapDirtyWalletAutoEvents) {
            std::vector<WalletAutoChannelEvent> events;
            const auto key = std::make_pair(WALLET_AUTO_CHANNEL_EVENT, entry.first);
            if (Exists(key) && !ReadWalletAutoChannelEvents(
                                   entry.first.first, entry.first.second, events))
                return error("%s: failed to read wallet auto channel events", __func__);
            for (const auto& event : entry.second) {
                if (std::find(events.begin(), events.end(), event) == events.end())
                    events.push_back(event);
            }
            if (!WriteWalletAutoChannelEvents(entry.first.first,
                                              entry.first.second, events))
                return error("%s: failed to write wallet auto channel events", __func__);
        }

        setDirtyChannelsRemove.clear();
        setDirtyChannelsAdd.clear();
        setDirtySeenAddressAdd.clear();
        setSubscribedChannelsAskedForFalse.clear();
        setAddressAskedForFalse.clear();
        setDirtyWalletChannelsRemove.clear();
        setDirtyWalletChannelsAdd.clear();
        setWalletChannelsAskedForFalse.clear();
        setDirtyWalletSeenAddressesAdd.clear();
        setWalletAddressesAskedForFalse.clear();
        mapDirtyWalletAutoEvents.clear();
    } catch (const std::runtime_error& e) {
        return error("%s : %s ", __func__, std::string("System error while flushing messagechannels: ") + e.what());
    }

    return true;
}


CMyRestrictedDB::CMyRestrictedDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "myrestricted", nCacheSize, fMemory, fWipe) {
}

bool CMyRestrictedDB::WriteTaggedAddress(const std::string& address, const std::string& tag_name, const bool fAdd, const uint32_t& nHeight)
{
    return Write(std::make_pair(MY_TAGGED_ADDRESSES, std::make_pair(address, tag_name)), std::make_pair(fAdd ? 1 : 0, nHeight));
}
bool CMyRestrictedDB::ReadTaggedAddress(const std::string& address, const std::string& tag_name, bool& fAdd, uint32_t& nHeight)
{
    std::pair<int, uint32_t> value;
    bool ret = Read(std::make_pair(MY_TAGGED_ADDRESSES, std::make_pair(address, tag_name)), value);
    fAdd = value.first;
    nHeight = value.second;
    return ret;
}
bool CMyRestrictedDB::EraseTaggedAddress(const std::string& address, const std::string& tag_name)
{
    return Erase(std::make_pair(MY_TAGGED_ADDRESSES, std::make_pair(address, tag_name)));
}
bool CMyRestrictedDB::LoadMyTaggedAddresses(std::vector<std::tuple<std::string, std::string, bool, uint32_t> >& vecTaggedAddresses)
{
    vecTaggedAddresses.clear();
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(MY_TAGGED_ADDRESSES, std::make_pair(std::string(), std::string())));

    // Load messages
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, std::pair<std::string, std::string>> key;
        if (pcursor->GetKey(key) && key.first == MY_TAGGED_ADDRESSES) {
            std::pair<int, uint32_t> value;
            if (pcursor->GetValue(value)) {
                vecTaggedAddresses.emplace_back(std::make_tuple(key.second.first, key.second.second, value.first ? true : false, value.second));
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CMyRestrictedDB::WriteRestrictedAddress(const std::string& address, const std::string& tag_name, const bool fAdd, const uint32_t& nHeight)
{
    return Write(std::make_pair(MY_RESTRICTED_ADDRESSES, std::make_pair(address, tag_name)), std::make_pair(fAdd ? 1 : 0, nHeight));
}

bool CMyRestrictedDB::ReadRestrictedAddress(const std::string& address, const std::string& tag_name, bool& fAdd, uint32_t& nHeight)
{
    std::pair<int, uint32_t> value;
    bool ret = Read(std::make_pair(MY_RESTRICTED_ADDRESSES, std::make_pair(address, tag_name)), value);
    fAdd = value.first;
    nHeight = value.second;
    return ret;
}

bool CMyRestrictedDB::EraseRestrictedAddress(const std::string& address, const std::string& tag_name)
{
    return Erase(std::make_pair(MY_RESTRICTED_ADDRESSES, std::make_pair(address, tag_name)));
}

bool CMyRestrictedDB::LoadMyRestrictedAddresses(std::vector<std::tuple<std::string, std::string, bool, uint32_t> >& vecRestrictedAddresses)
{
    vecRestrictedAddresses.clear();
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(MY_RESTRICTED_ADDRESSES, std::make_pair(std::string(), std::string())));

    // Load messages
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, std::pair<std::string, std::string>> key;
        if (pcursor->GetKey(key) && key.first == MY_RESTRICTED_ADDRESSES) {
            std::pair<int, uint32_t> value;
            if (pcursor->GetValue(value)) {
                vecRestrictedAddresses.emplace_back(std::make_tuple(key.second.first, key.second.second, value.first ? true : false, value.second));
            }
            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}

bool CMyRestrictedDB::EraseAllRestrictedEvents()
{
    for (char prefix : {MY_TAGGED_ADDRESSES, MY_RESTRICTED_ADDRESSES}) {
        std::unique_ptr<CDBIterator> cursor(NewIterator());
        cursor->Seek(std::make_pair(prefix, std::make_pair(std::string(), std::string())));
        while (cursor->Valid()) {
            CDBBatch batch(*this);
            size_t count = 0;
            while (cursor->Valid() && count < 1024) {
                std::pair<char, std::pair<std::string, std::string>> key;
                if (!cursor->GetKey(key) || key.first != prefix)
                    break;
                batch.Erase(key);
                cursor->Next();
                ++count;
            }
            if (!count)
                break;
            if (!WriteBatch(batch))
                return false;
            boost::this_thread::interruption_point();
        }
    }
    std::unique_ptr<CDBIterator> cursor(NewIterator());
    cursor->Seek(std::make_pair(MY_RESTRICTED_UNDO, uint256()));
    while (cursor->Valid()) {
        CDBBatch batch(*this);
        size_t count = 0;
        while (cursor->Valid() && count < 1024) {
            std::pair<char, uint256> key;
            if (!cursor->GetKey(key) || key.first != MY_RESTRICTED_UNDO)
                break;
            batch.Erase(key);
            cursor->Next();
            ++count;
        }
        if (!count)
            break;
        if (!WriteBatch(batch))
            return false;
        boost::this_thread::interruption_point();
    }
    return true;
}

bool CMyRestrictedDB::WriteRestrictedBlock(
    const std::vector<std::pair<std::string, CNullAssetTxData>>& events,
    uint32_t blockTime, const uint256& hash)
{
    CDBBatch batch(*this);
    std::set<RestrictedEventKey> seen;
    RestrictedBlockUndo undo;
    for (const auto& event : events) {
        char prefix = 0;
        if (IsAssetNameAQualifier(event.second.asset_name)) {
            prefix = MY_TAGGED_ADDRESSES;
        } else if (IsAssetNameAnRestricted(event.second.asset_name)) {
            prefix = MY_RESTRICTED_ADDRESSES;
        }
        if (!prefix)
            continue;
        const RestrictedEventKey key(prefix, std::make_pair(event.first, event.second.asset_name));
        if (seen.insert(key).second) {
            RestrictedEventValue prior(0, 0);
            const bool existed = Read(key, prior);
            undo.emplace_back(key, std::make_pair(existed, prior));
        }
        batch.Write(key, RestrictedEventValue(event.second.flag ? 1 : 0, blockTime));
    }
    if (!undo.empty())
        batch.Write(std::make_pair(MY_RESTRICTED_UNDO, hash), undo);
    batch.Write(std::make_pair(DB_FLAG, std::string("restricted_global_index_tip_v1")), hash);
    return WriteBatch(batch);
}

bool CMyRestrictedDB::UndoRestrictedBlock(
    const std::vector<std::pair<std::string, CNullAssetTxData>>& events,
    const uint256& hash, const uint256& previousHash,
    std::vector<std::tuple<std::string, std::string, int, uint32_t>>& restored)
{
    restored.clear();
    std::set<RestrictedEventKey> expected;
    for (const auto& event : events) {
        char prefix = 0;
        if (IsAssetNameAQualifier(event.second.asset_name))
            prefix = MY_TAGGED_ADDRESSES;
        else if (IsAssetNameAnRestricted(event.second.asset_name))
            prefix = MY_RESTRICTED_ADDRESSES;
        if (prefix)
            expected.emplace(prefix, std::make_pair(event.first, event.second.asset_name));
    }

    CDBBatch batch(*this);
    if (!expected.empty()) {
        RestrictedBlockUndo undo;
        const auto undoKey = std::make_pair(MY_RESTRICTED_UNDO, hash);
        if (!Read(undoKey, undo) || undo.size() != expected.size())
            return false;
        for (const auto& item : undo) {
            if (!expected.erase(item.first))
                return false;
            if (item.second.first)
                batch.Write(item.first, item.second.second);
            else
                batch.Erase(item.first);
            restored.emplace_back(item.first.second.first, item.first.second.second,
                                  item.second.first ? item.second.second.first : -1,
                                  item.second.first ? item.second.second.second : 0);
        }
        batch.Erase(undoKey);
    }
    batch.Write(std::make_pair(DB_FLAG, std::string("restricted_global_index_tip_v1")), previousHash);
    return WriteBatch(batch);
}

bool CMyRestrictedDB::WriteRestrictedIndexTip(const uint256& hash)
{
    return Write(std::make_pair(DB_FLAG, std::string("restricted_global_index_tip_v1")), hash);
}

bool CMyRestrictedDB::ReadRestrictedIndexTip(uint256& hash)
{
    return Read(std::make_pair(DB_FLAG, std::string("restricted_global_index_tip_v1")), hash);
}

bool CMyRestrictedDB::WriteRestrictedIndexState(char state)
{
    return Write(std::make_pair(DB_FLAG, std::string("restricted_global_index_state_v1")), state);
}

bool CMyRestrictedDB::ReadRestrictedIndexState(char& state)
{
    return Read(std::make_pair(DB_FLAG, std::string("restricted_global_index_state_v1")), state);
}


bool CMyRestrictedDB::WriteFlag(const std::string &name, bool fValue)
{
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CMyRestrictedDB::ReadFlag(const std::string &name, bool &fValue)
{
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}
