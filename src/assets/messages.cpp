// Copyright (c) 2018-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <validation.h>
#include <chainparams.h>
#include <wallet/wallet.h>
#include <wallet/db.h>
#include <script/ismine.h>
#include <base58.h>
#include <random.h>
#include <utilstrencodings.h>
#include "messages.h"
#include "myassetsdb.h"
#include <primitives/block.h>
#include <algorithm>


std::set<COutPoint> setDirtyMessagesRemove;
std::map<COutPoint, CMessage> mapDirtyMessagesAdd;
std::map<COutPoint, CMessage> mapDirtyMessagesOrphaned;
std::set<WalletMessageKey> setDirtyWalletMessagesRemove;
std::map<WalletMessageKey, CMessage> mapDirtyWalletMessagesAdd;
std::map<WalletMessageKey, CMessage> mapDirtyWalletMessagesOrphaned;

std::set<std::string> setDirtyChannelsAdd;
std::set<std::string> setDirtyChannelsRemove;
std::set<std::string> setSubscribedChannelsAskedForFalse;
std::set<WalletChannelKey> setDirtyWalletChannelsAdd;
std::set<WalletChannelKey> setDirtyWalletChannelsRemove;
std::set<WalletChannelKey> setWalletChannelsAskedForFalse;
std::map<WalletChannelKey, std::vector<WalletAutoChannelEvent>> mapDirtyWalletAutoEvents;

std::set<std::string> setDirtySeenAddressAdd;
std::set<std::string> setAddressAskedForFalse;
std::set<WalletChannelKey> setDirtyWalletSeenAddressesAdd;
std::set<WalletChannelKey> setWalletAddressesAskedForFalse;

CCriticalSection cs_messaging;

#ifdef ENABLE_WALLET
static std::set<std::string> setInvalidMessageScopes;

bool GetMessageWalletScope(CWallet& wallet, std::string& scope)
{
    LOCK(cs_messaging);
    // The random marker lives in the wallet file. The file name also scopes a
    // copied wallet loaded beside its source and prevents name reuse leakage.
    CDB db(wallet.GetDBHandle());
    std::string marker;
    const std::string key = "message_wallet_scope_v1";
    const int markerStatus = db.ExistsStatus(key);
    if (markerStatus == 0) {
        if (!db.Read(key, marker)) {
            LogPrintf("%s: could not read wallet message scope for %s\n",
                      __func__, wallet.GetName());
            return false;
        }
    } else if (markerStatus == DB_NOTFOUND) {
        marker = GetRandHash().GetHex();
        if (!db.Write(key, marker, false)) {
            LogPrintf("%s: failed to persist wallet message scope for %s\n",
                      __func__, wallet.GetName());
            return false;
        }
        std::string persisted;
        if (!db.Read(key, persisted) || persisted != marker)
            return false;
    } else {
        LogPrintf("%s: failed to inspect wallet message scope for %s\n",
                  __func__, wallet.GetName());
        return false;
    }
    if (marker.size() != 64 || !IsHex(marker)) {
        LogPrintf("%s: invalid wallet message scope for %s\n", __func__, wallet.GetName());
        return false;
    }
    scope = wallet.GetName() + ":" + marker;
    return true;
}

static bool IsScopedChannelSubscribed(const std::string& scope, const std::string& name)
{
    if (!pmessagechanneldb)
        return false;
    const WalletChannelKey key(scope, name);
    int suppressedAtHeight = -1;
    if (pmessagechanneldb->ReadWalletChannelSuppression(scope, name, suppressedAtHeight))
        return false;
    if (pmessagechanneldb->ReadWalletChannelPreference(scope, name))
        return true;
    if (setDirtyWalletChannelsAdd.count(key))
        return true;
    if (setDirtyWalletChannelsRemove.count(key) || setWalletChannelsAskedForFalse.count(key))
        return false;
    if (pmessagechanneldb->ReadWalletMessageChannel(scope, name))
        return true;
    setWalletChannelsAskedForFalse.insert(key);
    return false;
}

bool IsChannelSubscribed(CWallet& wallet, const std::string& name)
{
    LOCK(cs_messaging);
    std::string scope;
    return GetMessageWalletScope(wallet, scope) && IsScopedChannelSubscribed(scope, name);
}

bool IsMessageWalletScanComplete(CWallet& wallet)
{
    LOCK(cs_messaging);
    std::string scope;
    bool complete = false;
    return GetMessageWalletScope(wallet, scope) && pmessagechanneldb &&
           !setInvalidMessageScopes.count(scope) &&
           pmessagechanneldb->ReadFlag("init:" + scope, complete) && complete;
}
#endif


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

bool IsChannelSubscribed(const std::string &name)
{
#ifdef ENABLE_WALLET
    LOCK(cs_messaging);
    for (CWalletRef wallet : vpwallets) {
        std::string scope;
        if (GetMessageWalletScope(*wallet, scope) &&
            !setInvalidMessageScopes.count(scope) &&
            IsScopedChannelSubscribed(scope, name))
            return true;
    }
    return false;
#else
    return false;
#endif
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

void AddChannel(const std::string &name)
{
    // Add channel to dirty cache to add
    setDirtyChannelsAdd.insert(name);

    // If the channel name is in the dirty remove cache. Remove it so it doesn't get deleted on flush
    setDirtyChannelsRemove.erase(name);
    setSubscribedChannelsAskedForFalse.erase(name);
}

void RemoveChannel(const std::string &name)
{
    // Add channel to dirty cache to remove
    setDirtyChannelsRemove.insert(name);

    // If the channel name is in the dirty add cache. Remove it so it doesn't get added on flush
    setDirtyChannelsAdd.erase(name);
}

#ifdef ENABLE_WALLET
void AddChannel(CWallet& wallet, const std::string& name, int eventHeight,
                const uint256& blockHash)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!GetMessageWalletScope(wallet, scope))
        return;
    int suppressedAtHeight = -1;
    if (pmessagechanneldb->ReadWalletChannelSuppression(scope, name, suppressedAtHeight)) {
        if (eventHeight <= suppressedAtHeight ||
            !pmessagechanneldb->EraseWalletChannelSuppression(scope, name))
            return;
    }
    const WalletChannelKey key(scope, name);
    setDirtyWalletChannelsAdd.insert(key);
    setDirtyWalletChannelsRemove.erase(key);
    setWalletChannelsAskedForFalse.erase(key);
    auto& events = mapDirtyWalletAutoEvents[key];
    const WalletAutoChannelEvent event(eventHeight, blockHash);
    if (events.empty() || events.back() != event)
        events.push_back(event);
}

void RemoveChannel(CWallet& wallet, const std::string& name)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!GetMessageWalletScope(wallet, scope))
        return;
    const WalletChannelKey key(scope, name);
    setDirtyWalletChannelsRemove.insert(key);
    setDirtyWalletChannelsAdd.erase(key);
}

bool SetWalletChannelSubscription(CWallet& wallet, const std::string& name,
                                  bool subscribed)
{
    LOCK2(cs_main, cs_messaging);
    std::string scope;
    if (!pmessagechanneldb || !GetMessageWalletScope(wallet, scope))
        return false;
    if (subscribed) {
        if (!chainActive.Tip() ||
            !pmessagechanneldb->WriteWalletChannelSubscription(
                scope, name, chainActive.Height(), chainActive.Tip()->GetBlockHash()))
            return false;
        const WalletChannelKey key(scope, name);
        setDirtyWalletChannelsAdd.insert(key);
        setDirtyWalletChannelsRemove.erase(key);
        setWalletChannelsAskedForFalse.erase(key);
    } else {
        if (!chainActive.Tip() ||
            !pmessagechanneldb->WriteWalletChannelUnsubscription(
                scope, name, chainActive.Height(), chainActive.Tip()->GetBlockHash()))
            return false;
        RemoveChannel(wallet, name);
    }
    return true;
}

bool LoadWalletChannels(CWallet& wallet, std::set<std::string>& channels)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!pmessagechanneldb || !GetMessageWalletScope(wallet, scope) ||
        !pmessagechanneldb->LoadWalletMessageChannels(scope, channels) ||
        !pmessagechanneldb->LoadWalletChannelPreferences(scope, channels))
        return false;
    for (const auto& key : setDirtyWalletChannelsRemove) {
        if (key.first == scope)
            channels.erase(key.second);
    }
    for (const auto& key : setDirtyWalletChannelsAdd) {
        if (key.first == scope)
            channels.insert(key.second);
    }
    for (auto it = channels.begin(); it != channels.end();) {
        int suppressedAtHeight = -1;
        if (pmessagechanneldb->ReadWalletChannelSuppression(scope, *it,
                                                             suppressedAtHeight))
            it = channels.erase(it);
        else
            ++it;
    }
    return true;
}
#endif

void AddMessage(const CMessage& message)
{
#ifdef ENABLE_WALLET
    LOCK(cs_messaging);
    for (CWalletRef wallet : vpwallets) {
        std::string scope;
        if (!GetMessageWalletScope(*wallet, scope) ||
            setInvalidMessageScopes.count(scope) ||
            !IsScopedChannelSubscribed(scope, message.strName))
            continue;
        AddWalletMessage(*wallet, message);
    }
#endif
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

    if (!pmessagedb)
        return;
    std::vector<std::pair<std::string, CMessage>> persisted;
    if (!pmessagedb->LoadWalletMessagesForOut(out, persisted)) {
        LogPrintf("%s: failed to load wallet messages for %s\n", __func__, out.ToString());
        return;
    }
    for (const auto& entry : persisted) {
        const WalletMessageKey key(entry.first, out);
        if (!setDirtyWalletMessagesRemove.count(key))
            mapDirtyWalletMessagesOrphaned[key] = entry.second;
    }
    for (auto it = mapDirtyWalletMessagesAdd.begin(); it != mapDirtyWalletMessagesAdd.end();) {
        if (it->first.second == out) {
            mapDirtyWalletMessagesOrphaned[it->first] = it->second;
            it = mapDirtyWalletMessagesAdd.erase(it);
        } else {
            ++it;
        }
    }
}

void OrphanMessage(const CMessage& message)
{
    mapDirtyMessagesOrphaned[message.out] = message;

    // Remove from other dirty caches
    mapDirtyMessagesAdd.erase(message.out);
}

#ifdef ENABLE_WALLET
bool AddWalletMessage(CWallet& wallet, const CMessage& message)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!pmessagedb || !GetMessageWalletScope(wallet, scope))
        return false;
    if (pmessagedb->ReadWalletMessageTombstone(scope, message.out))
        return true;
    const WalletMessageKey key(scope, message.out);
    mapDirtyWalletMessagesAdd[key] = message;
    setDirtyWalletMessagesRemove.erase(key);
    mapDirtyWalletMessagesOrphaned.erase(key);
    return true;
}

bool LoadWalletMessages(CWallet& wallet, std::set<CMessage>& messages)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!pmessagedb || !GetMessageWalletScope(wallet, scope) ||
        !pmessagedb->LoadWalletMessages(scope, messages))
        return false;
    for (const auto& key : setDirtyWalletMessagesRemove) {
        if (key.first == scope) {
            CMessage probe;
            probe.out = key.second;
            messages.erase(probe);
        }
    }
    for (const auto& entry : mapDirtyWalletMessagesOrphaned) {
        if (entry.first.first != scope)
            continue;
        messages.erase(entry.second);
        CMessage orphan = entry.second;
        orphan.status = MessageStatus::ORPHAN;
        messages.insert(orphan);
    }
    for (const auto& entry : mapDirtyWalletMessagesAdd) {
        if (entry.first.first != scope)
            continue;
        messages.erase(entry.second);
        messages.insert(entry.second);
    }
    return true;
}

bool ClearWalletMessages(CWallet& wallet, int& count)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!pmessagedb || !GetMessageWalletScope(wallet, scope))
        return false;
    std::set<CMessage> visible;
    if (!LoadWalletMessages(wallet, visible))
        return false;
    for (const CMessage& message : visible) {
        if (!pmessagedb->WriteWalletMessageTombstone(scope, message.out))
            return false;
    }
    if (!pmessagedb->EraseAllWalletMessages(scope, count))
        return false;
    for (auto it = mapDirtyWalletMessagesAdd.begin(); it != mapDirtyWalletMessagesAdd.end();) {
        if (it->first.first == scope) {
            ++count;
            it = mapDirtyWalletMessagesAdd.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = mapDirtyWalletMessagesOrphaned.begin(); it != mapDirtyWalletMessagesOrphaned.end();) {
        if (it->first.first == scope)
            it = mapDirtyWalletMessagesOrphaned.erase(it);
        else
            ++it;
    }
    return true;
}

bool ResetWalletDerivedMessageState(CWallet& wallet)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!pmessagechanneldb || !GetMessageWalletScope(wallet, scope))
        return false;
    if (!pmessagechanneldb->EraseWalletDerivedMessageState(scope))
        return false;
    for (auto it = setDirtyWalletChannelsAdd.begin(); it != setDirtyWalletChannelsAdd.end();) {
        if (it->first == scope) it = setDirtyWalletChannelsAdd.erase(it);
        else ++it;
    }
    for (auto it = setDirtyWalletChannelsRemove.begin(); it != setDirtyWalletChannelsRemove.end();) {
        if (it->first == scope) it = setDirtyWalletChannelsRemove.erase(it);
        else ++it;
    }
    for (auto it = setWalletChannelsAskedForFalse.begin(); it != setWalletChannelsAskedForFalse.end();) {
        if (it->first == scope) it = setWalletChannelsAskedForFalse.erase(it);
        else ++it;
    }
    for (auto it = setDirtyWalletSeenAddressesAdd.begin(); it != setDirtyWalletSeenAddressesAdd.end();) {
        if (it->first == scope) it = setDirtyWalletSeenAddressesAdd.erase(it);
        else ++it;
    }
    for (auto it = setWalletAddressesAskedForFalse.begin(); it != setWalletAddressesAskedForFalse.end();) {
        if (it->first == scope) it = setWalletAddressesAskedForFalse.erase(it);
        else ++it;
    }
    for (auto it = mapDirtyWalletAutoEvents.begin(); it != mapDirtyWalletAutoEvents.end();) {
        if (it->first.first == scope) it = mapDirtyWalletAutoEvents.erase(it);
        else ++it;
    }
    return true;
}

bool InvalidateMessageWalletScans()
{
    LOCK(cs_messaging);
    if (!pmessagechanneldb)
        return true;
    std::set<std::string> scopes;
    if (!pmessagechanneldb->LoadWalletMessageScanScopes(scopes))
        return false;
    for (CWalletRef wallet : vpwallets) {
        std::string scope;
        if (!GetMessageWalletScope(*wallet, scope))
            return false;
        scopes.insert(scope);
    }
    for (const std::string& scope : scopes) {
        if (setInvalidMessageScopes.count(scope))
            continue;
        if (!pmessagechanneldb->InvalidateWalletMessageScan(scope))
            return false;
        setInvalidMessageScopes.insert(scope);
    }
    return true;
}
#endif

#ifdef ENABLE_WALLET
bool ScanForMessageChannels(CWallet& wallet, int startHeight, std::string& strError)
{
    LOCK2(cs_main, cs_messaging);

    LogPrintf("%s: scanning message channels for wallet %s\n", __func__, wallet.GetName());

    std::string scope;
    if (!GetMessageWalletScope(wallet, scope)) {
        strError = "Could not persist wallet message scope";
        return false;
    }
    if (!chainActive.Tip()) {
        strError = "Message chain tip is unavailable";
        return false;
    }
    const bool fullHistory = startHeight <= GetParams().GetAssetActivationHeight();

    std::set<std::string> manualChannels;
    if (!pmessagechanneldb->LoadWalletChannelPreferences(scope, manualChannels)) {
        strError = "Could not load manual channel subscriptions";
        return false;
    }
    std::map<std::string, std::vector<WalletChannelEvent>> manualEvents;
    if (!pmessagechanneldb->LoadWalletChannelEvents(scope, manualEvents)) {
        strError = "Could not load manual channel event history";
        return false;
    }
    std::map<std::string, std::vector<WalletAutoChannelEvent>> autoEvents;
    if (!pmessagechanneldb->LoadWalletAutoChannelEvents(scope, autoEvents)) {
        strError = "Could not load automatic channel event history";
        return false;
    }
    for (auto& channel : manualEvents) {
        bool reanchored = false;
        for (WalletChannelEvent& event : channel.second) {
            int& height = event.first.first;
            uint256& hash = event.first.second;
            if (height >= 0 && height <= chainActive.Height() &&
                chainActive[height]->GetBlockHash() == hash)
                continue;
            if (event.second) {
                // A subscription made on an orphan branch cannot authorize
                // earlier messages on its replacement branch.
                height = chainActive.Height();
            } else {
                // A removed unsubscribe must still exclude the replacement
                // branch after the common ancestor.
                const auto old = mapBlockIndex.find(hash);
                const CBlockIndex* ancestor = old == mapBlockIndex.end() ? nullptr : old->second;
                while (ancestor && !chainActive.Contains(ancestor))
                    ancestor = ancestor->pprev;
                height = ancestor ? ancestor->nHeight : 0;
            }
            hash = chainActive[height]->GetBlockHash();
            reanchored = true;
        }
        int suppressionHeight = -1;
        if (!channel.second.empty() && !channel.second.back().second)
            suppressionHeight = channel.second.back().first.first;
        if ((reanchored || fullHistory) && !pmessagechanneldb->WriteWalletChannelEvents(
                              scope, channel.first, channel.second,
                              true, suppressionHeight)) {
            strError = "Could not reanchor manual channel event history";
            return false;
        }
    }
    std::map<std::string, int> manualAnchorHeights;
    for (const std::string& name : manualChannels) {
        int anchorHeight = -1;
        uint256 anchorHash;
        if (!pmessagechanneldb->ReadWalletChannelSubscriptionAnchor(
                scope, name, anchorHeight, anchorHash) ||
            anchorHeight < 0 || anchorHeight > chainActive.Height() ||
            chainActive[anchorHeight]->GetBlockHash() != anchorHash) {
            // A missing or orphaned anchor cannot establish when this manual
            // subscription began on the active branch. Resume after the tip.
            anchorHeight = chainActive.Height();
            if (!pmessagechanneldb->WriteWalletChannelSubscriptionAnchor(
                    scope, name, anchorHeight, chainActive.Tip()->GetBlockHash())) {
                strError = "Could not persist manual channel subscription anchor";
                return false;
            }
        }
        manualAnchorHeights.emplace(name, anchorHeight);
        if (!manualEvents.count(name))
            manualEvents[name].emplace_back(std::make_pair(anchorHeight, anchorHash), true);
    }

    std::set<std::string> scanDerivedChannels;
    std::map<std::string, int> scanAutoHeights;
    if (!fullHistory) {
        for (const auto& channel : autoEvents) {
            for (const WalletAutoChannelEvent& event : channel.second) {
                if (event.first >= startHeight)
                    continue;
                if (event.first < 0 || event.first > chainActive.Height() ||
                    chainActive[event.first]->GetBlockHash() != event.second) {
                    LogPrintf("%s: invalid auto event %s at %d: %s, active %s\n", __func__,
                              channel.first, event.first, event.second.ToString(),
                              event.first >= 0 && event.first <= chainActive.Height()
                                  ? chainActive[event.first]->GetBlockHash().ToString() : "out of range");
                    strError = "Automatic channel history disagrees with the active chain";
                    return false;
                }
                scanAutoHeights[channel.first] = event.first;
            }
        }
    }
    const auto addScannedChannel = [&](const std::string& name, int eventHeight,
                                       const uint256& blockHash) {
        scanAutoHeights[name] = eventHeight;
        AddChannel(wallet, name, eventHeight, blockHash);
        int suppressedAtHeight = -1;
        if (!pmessagechanneldb->ReadWalletChannelSuppression(scope, name,
                                                              suppressedAtHeight))
            scanDerivedChannels.insert(name);
    };
    const auto subscribedAtBlock = [&](const std::string& name, int blockHeight) {
        bool manualSubscribed = false;
        int lastManualUnsubscribe = -1;
        bool hasManualEvent = false;
        const auto timeline = manualEvents.find(name);
        if (timeline != manualEvents.end()) {
            for (const WalletChannelEvent& event : timeline->second) {
                if (event.first.first >= blockHeight)
                    continue;
                hasManualEvent = true;
                manualSubscribed = event.second;
                if (!event.second)
                    lastManualUnsubscribe = event.first.first;
            }
        }
        const auto autoEvent = scanAutoHeights.find(name);
        const bool autoSubscribed = autoEvent != scanAutoHeights.end() &&
            (lastManualUnsubscribe < 0 || autoEvent->second > lastManualUnsubscribe);
        if (fullHistory || hasManualEvent || autoEvent != scanAutoHeights.end())
            return manualSubscribed || autoSubscribed;
        if (!IsScopedChannelSubscribed(scope, name))
            return false;
        const auto manual = manualAnchorHeights.find(name);
        return manual == manualAnchorHeights.end() ||
               blockHeight > manual->second || scanDerivedChannels.count(name) != 0;
    };

    // Genesis has no previous block or asset messages to recover.
    startHeight = std::max(startHeight, std::max(1, GetParams().GetAssetActivationHeight()));
    if (chainActive.Height() < startHeight)
        return true;

    CBlockIndex* blockIndex = chainActive[startHeight];

    while (blockIndex) {
        CBlock block;
        if (!ReadBlockFromDisk(block, blockIndex, GetParams().GetConsensus())) {
            strError = "Block not found on disk";
            return false;
        }
        std::set<CMessage> confirmedMessages;
        if (!CollectConfirmedAssetMessages(block, blockIndex, confirmedMessages)) {
            strError = "Message block undo data is unavailable or inconsistent";
            return false;
        }

        for (const auto& tx : block.vtx) {

            auto ptx = tx.get();
            if (!ptx) {
                strError = "Failed to get transaction pointer";
                return false;
            }

            for (auto out : ptx->vout) {
                int nType = -1;
                bool fOwner = false;
                if (wallet.IsMine(out, blockIndex) == ISMINE_SPENDABLE) { // Is the out mine
                    if (out.scriptPubKey.IsAssetScript(nType, fOwner)) {
                        CAssetOutputEntry assetData;
                        // Get the asset data from the script
                        if (GetAssetData(out.scriptPubKey, assetData)) {
                            AssetType type;
                            IsAssetNameValid(assetData.assetName, type);

                            if (assetData.type == TX_TRANSFER_ASSET) {
                                if (type == AssetType::MSGCHANNEL || type == AssetType::OWNER) { // Subscribe to any channels or owner tokens you own
                                    addScannedChannel(assetData.assetName, blockIndex->nHeight,
                                                      blockIndex->GetBlockHash());
                                    AddAddressSeen(wallet, EncodeDestination(assetData.destination));
                                } else if (type == AssetType::ROOT || type == AssetType::SUB) { // Subscribe to any assets you are sent, if they are sent to a new address
                                    const std::string parentChannel = GetParentName(assetData.assetName) + OWNER_TAG;
                                    if (!subscribedAtBlock(parentChannel, blockIndex->nHeight)) {
                                        if (!IsAddressSeen(wallet, EncodeDestination(assetData.destination))) {
                                            addScannedChannel(parentChannel, blockIndex->nHeight,
                                                              blockIndex->GetBlockHash());
                                            AddAddressSeen(wallet, EncodeDestination(assetData.destination));
                                        }
                                    }
                                }
                            } else if (assetData.type == TX_NEW_ASSET || assetData.type == TX_REISSUE_ASSET) {
                                if (fOwner || type == AssetType::MSGCHANNEL) {
                                    addScannedChannel(assetData.assetName, blockIndex->nHeight,
                                                      blockIndex->GetBlockHash());
                                    AddAddressSeen(wallet, EncodeDestination(assetData.destination));
                                } else if (type == AssetType::ROOT || type == AssetType::SUB || type == AssetType::RESTRICTED) {
                                    addScannedChannel(assetData.assetName + "!", blockIndex->nHeight,
                                                      blockIndex->GetBlockHash());
                                    AddAddressSeen(wallet, EncodeDestination(assetData.destination));
                                }
                            }
                        } else {
                            LogPrintf("%s : Failed to get GetAssetData call\n", __func__);
                        }
                    }
                } else if (out.scriptPubKey.IsAssetScript(nType, fOwner)) {
                    CAssetOutputEntry assetData;
                    AssetType type;
                    if (GetAssetData(out.scriptPubKey, assetData) &&
                        assetData.type == TX_NEW_ASSET &&
                        IsAssetNameValid(assetData.assetName, type) &&
                        type == AssetType::MSGCHANNEL &&
                        subscribedAtBlock(GetParentName(assetData.assetName) + OWNER_TAG,
                                          blockIndex->nHeight)) {
                        addScannedChannel(assetData.assetName, blockIndex->nHeight,
                                          blockIndex->GetBlockHash());
                    }
                }
            }
        }
        for (const CMessage& message : confirmedMessages) {
            if (subscribedAtBlock(message.strName, blockIndex->nHeight) &&
                !AddWalletMessage(wallet, message)) {
                strError = "Could not restore wallet message";
                return false;
            }
        }
        blockIndex = chainActive[blockIndex->nHeight + 1];
    }

    LogPrintf("%s: finished message channel scan for wallet %s\n", __func__, wallet.GetName());
    return true;
}
#endif

bool IsAddressSeen(const std::string &address)
{
    if (!pmessagechanneldb || !pMessagesSeenAddressCache)
        return false;

    if (setDirtySeenAddressAdd.count(address)) // Check dirty set
        return true;

    if (pMessagesSeenAddressCache->Exists(address)) {
        return true;
    }

    if (setAddressAskedForFalse.count(address)) {
        return false;
    }

    if (pmessagechanneldb->ReadUsedAddress(address)) {
        pMessagesSeenAddressCache->Put(address, 1);
        return true;
    }

    setAddressAskedForFalse.insert(address);

    return false;
}

void AddAddressSeen(const std::string &address)
{
    setDirtySeenAddressAdd.insert(address);
    setSubscribedChannelsAskedForFalse.erase(address);
}

#ifdef ENABLE_WALLET
bool IsAddressSeen(CWallet& wallet, const std::string& address)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!pmessagechanneldb || !GetMessageWalletScope(wallet, scope))
        return false;
    const WalletChannelKey key(scope, address);
    if (setDirtyWalletSeenAddressesAdd.count(key))
        return true;
    if (setWalletAddressesAskedForFalse.count(key))
        return false;
    if (pmessagechanneldb->ReadWalletUsedAddress(scope, address))
        return true;
    setWalletAddressesAskedForFalse.insert(key);
    return false;
}

void AddAddressSeen(CWallet& wallet, const std::string& address)
{
    LOCK(cs_messaging);
    std::string scope;
    if (!GetMessageWalletScope(wallet, scope))
        return;
    const WalletChannelKey key(scope, address);
    setDirtyWalletSeenAddressesAdd.insert(key);
    setWalletAddressesAskedForFalse.erase(key);
}
#endif

size_t GetMessageDirtyCacheSize()
{
    // COutPoint: 32 bytes
    // CNewAsset: Max 80 bytes
    // CAssetTransfer: Asset Name, CAmount ( 40 bytes)
    // CReissueAsset: Max 80 bytes
    // CAmount: 8 bytes
    // Asset Name: Max 32 bytes
    // Address: 40 bytes
    // Block hash: 32 bytes
    // CTxOut: CAmount + CScript (105 + 8 = 113 bytes)
    // CMessage: Max 123 Bytes


    size_t size = 0;
    // Messages Caches
    size += 32 * setDirtyMessagesRemove.size(); // COutPoint;
    size += (32 + 123) * mapDirtyMessagesAdd.size(); // COutPoint -> CMessage
    size += (32 + 123) * mapDirtyMessagesOrphaned.size(); // COutPoint -> CMessage
    size += (96 + 123) * mapDirtyWalletMessagesAdd.size();
    size += (96 + 123) * mapDirtyWalletMessagesOrphaned.size();
    size += 96 * setDirtyWalletMessagesRemove.size();


    // Message Channel Caches
    size += 32 * setDirtyChannelsAdd.size();
    size += 32 * setDirtyChannelsRemove.size();
    size += 32 * setSubscribedChannelsAskedForFalse.size();
    size += 96 * (setDirtyWalletChannelsAdd.size() + setDirtyWalletChannelsRemove.size() +
                  setWalletChannelsAskedForFalse.size());

    // Address Seen Caches
    size += 32 * setDirtySeenAddressAdd.size();
    size += 32 * setAddressAskedForFalse.size();
    size += 96 * (setDirtyWalletSeenAddressesAdd.size() +
                  setWalletAddressesAskedForFalse.size());

    return size;
}


std::string CZMQMessage::createJsonString()
{
    std::string str = "";
    str += "{";
    str += "\"blockheight\": " + std::to_string(this->blockHeight) + ", ";
    str += "\"assetname\": \"" + this->assetName + "\", ";
    str += "\"ipfshash\": \"" + EncodeAssetData(this->ipfsHash) + "\", ";
    str += "\"expiretime\": " + std::to_string(this->nExpireTime);
    str += "}";

    return str;
}
