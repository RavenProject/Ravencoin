// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_MYASSETSDB_H
#define RAVENCOIN_MYASSETSDB_H

#include <dbwrapper.h>
#include <uint256.h>
#include <map>
#include <vector>

class CMessage;
class COutPoint;
class CNullAssetTxData;
using WalletChannelEvent = std::pair<std::pair<int, uint256>, bool>;
using WalletAutoChannelEvent = std::pair<int, uint256>;

class CMessageDB  : public CDBWrapper {

public:
    explicit CMessageDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CMessageDB(const CMessageDB&) = delete;
    CMessageDB& operator=(const CMessageDB&) = delete;

    // Database of messages
    bool WriteMessage(const CMessage& message);
    bool ReadMessage(const COutPoint& out, CMessage& message);
    bool EraseMessage(const COutPoint& out);
    bool LoadMessages(std::set<CMessage>& setMessages);
    bool EraseAllMessages(int& count);
    bool WriteWalletMessage(const std::string& walletScope, const CMessage& message);
    bool ReadWalletMessage(const std::string& walletScope, const COutPoint& out, CMessage& message);
    bool LoadWalletMessages(const std::string& walletScope, std::set<CMessage>& messages);
    bool LoadWalletMessagesForOut(const COutPoint& out,
                                  std::vector<std::pair<std::string, CMessage>>& messages);
    bool EraseWalletMessage(const std::string& walletScope, const COutPoint& out);
    bool EraseAllWalletMessages(const std::string& walletScope, int& count);
    bool OrphanWalletMessages(const std::string& walletScope);
    bool WriteWalletMessageTombstone(const std::string& walletScope, const COutPoint& out);
    bool ReadWalletMessageTombstone(const std::string& walletScope, const COutPoint& out);

    // Write / Read Database flags
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);

    bool Flush();
};

class CMessageChannelDB  : public CDBWrapper {
public:
    explicit CMessageChannelDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CMessageChannelDB(const CMessageChannelDB&) = delete;
    CMessageChannelDB& operator=(const CMessageChannelDB&) = delete;

    // My message channels
    bool WriteMyMessageChannel(const std::string& channelname);
    bool ReadMyMessageChannel(const std::string& channelname);
    bool EraseMyMessageChannel(const std::string& channelname);
    bool LoadMyMessageChannels(std::set<std::string>& setChannels);
    bool WriteWalletMessageChannel(const std::string& walletScope, const std::string& channelname);
    bool ReadWalletMessageChannel(const std::string& walletScope, const std::string& channelname);
    bool EraseWalletMessageChannel(const std::string& walletScope, const std::string& channelname);
    bool LoadWalletMessageChannels(const std::string& walletScope, std::set<std::string>& channels);
    bool WriteWalletChannelSuppression(const std::string& walletScope,
                                       const std::string& channelname, int height);
    bool ReadWalletChannelSuppression(const std::string& walletScope,
                                      const std::string& channelname, int& height);
    bool EraseWalletChannelSuppression(const std::string& walletScope, const std::string& channelname);
    bool WriteWalletChannelPreference(const std::string& walletScope, const std::string& channelname);
    bool ReadWalletChannelPreference(const std::string& walletScope, const std::string& channelname);
    bool EraseWalletChannelPreference(const std::string& walletScope, const std::string& channelname);
    bool WriteWalletChannelSubscription(const std::string& walletScope,
                                        const std::string& channelname, int height,
                                        const uint256& blockHash);
    bool WriteWalletChannelUnsubscription(const std::string& walletScope,
                                          const std::string& channelname, int height,
                                          const uint256& blockHash);
    bool LoadWalletChannelEvents(
        const std::string& walletScope,
        std::map<std::string, std::vector<WalletChannelEvent>>& events);
    bool WriteWalletChannelEvents(const std::string& walletScope,
                                  const std::string& channelname,
                                  const std::vector<WalletChannelEvent>& events,
                                  bool updateSuppression, int suppressionHeight);
    bool LoadWalletAutoChannelEvents(
        const std::string& walletScope,
        std::map<std::string, std::vector<WalletAutoChannelEvent>>& events);
    bool ReadWalletAutoChannelEvents(const std::string& walletScope,
                                     const std::string& channelname,
                                     std::vector<WalletAutoChannelEvent>& events);
    bool WriteWalletAutoChannelEvents(const std::string& walletScope,
                                      const std::string& channelname,
                                      const std::vector<WalletAutoChannelEvent>& events);
    bool WriteWalletChannelSubscriptionAnchor(const std::string& walletScope,
                                              const std::string& channelname, int height,
                                              const uint256& blockHash);
    bool ReadWalletChannelSubscriptionAnchor(const std::string& walletScope,
                                             const std::string& channelname, int& height,
                                             uint256& blockHash);
    bool LoadWalletChannelPreferences(const std::string& walletScope, std::set<std::string>& channels);
    bool EraseWalletDerivedMessageState(const std::string& walletScope);

    bool WriteUsedAddress(const std::string& address);
    bool ReadUsedAddress(const std::string& address);
    bool EraseUsedAddress(const std::string& address);
    bool WriteWalletUsedAddress(const std::string& walletScope, const std::string& address);
    bool ReadWalletUsedAddress(const std::string& walletScope, const std::string& address);
    bool WriteWalletMessageScanTip(const std::string& walletScope, int height,
                                   const uint256& blockHash);
    bool CompleteWalletMessageScan(const std::string& walletScope, int height,
                                   const uint256& blockHash);
    bool ReadWalletMessageScanTip(const std::string& walletScope, int& height,
                                  uint256& blockHash);
    bool LoadWalletMessageScanScopes(std::set<std::string>& scopes);
    bool InvalidateWalletMessageScan(const std::string& walletScope);

    // Write / Read Database flags
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);

    bool Flush();
};

class CMyRestrictedDB : public CDBWrapper {
public:
    explicit CMyRestrictedDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CMyRestrictedDB(const CMyRestrictedDB&) = delete;
    CMyRestrictedDB& operator=(const CMyRestrictedDB&) = delete;

    bool WriteTaggedAddress(const std::string& address, const std::string& tag_name, const bool fAdd, const uint32_t& nHeight);
    bool ReadTaggedAddress(const std::string& address, const std::string& tag_name, bool& fAdd, uint32_t& nHeight);
    bool EraseTaggedAddress(const std::string& address, const std::string& tag_name);
    bool LoadMyTaggedAddresses(std::vector<std::tuple<std::string, std::string, bool, uint32_t> >& vecTaggedAddresses);

    bool WriteRestrictedAddress(const std::string& address, const std::string& tag_name, const bool fAdd, const uint32_t& nHeight);
    bool ReadRestrictedAddress(const std::string& address, const std::string& tag_name, bool& fAdd, uint32_t& nHeight);
    bool EraseRestrictedAddress(const std::string& address, const std::string& tag_name);
    bool LoadMyRestrictedAddresses(std::vector<std::tuple<std::string, std::string, bool, uint32_t> >& vecRestrictedAddresses);

    bool EraseAllRestrictedEvents();
    bool WriteRestrictedBlock(const std::vector<std::pair<std::string, CNullAssetTxData>>& events,
                              uint32_t blockTime, const uint256& hash);
    bool UndoRestrictedBlock(const std::vector<std::pair<std::string, CNullAssetTxData>>& events,
                             const uint256& hash, const uint256& previousHash,
                             std::vector<std::tuple<std::string, std::string, int, uint32_t>>& restored);
    bool WriteRestrictedIndexTip(const uint256& hash);
    bool ReadRestrictedIndexTip(uint256& hash);
    bool WriteRestrictedIndexState(char state);
    bool ReadRestrictedIndexState(char& state);

    // Write / Read Database flags
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
};


#endif //RAVENCOIN_MYASSETSDB_H
