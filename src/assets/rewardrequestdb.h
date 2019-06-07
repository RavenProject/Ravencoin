// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef REWARDREQUESTDB_H
#define REWARDREQUESTDB_H

#include <dbwrapper.h>

#include <string>
#include <vector>
#include <set>

#include "amount.h"

class CRewardRequestDBEntry
{
public:
    std::string walletName;
    int heightForPayout;
    CAmount totalPayoutAmt;
    std::string payoutSrc;
    std::string tgtAssetName;
    std::string exceptionAddresses;

    CRewardRequestDBEntry();
    CRewardRequestDBEntry(
        const std::string & p_walletName, const int p_heightForPayout, const CAmount p_totalPayoutAmt,
        const std::string & p_payoutSrc, const std::string & p_tgtAssetName, const std::string & p_exceptionAddresses
    );

    void SetNull()
    {
        walletName = "";
        heightForPayout = 0;
        totalPayoutAmt = 0;
        payoutSrc = "";
        exceptionAddresses = "";
    }

    bool operator<(const CRewardRequestDBEntry &rhs) const
    {
        return heightForPayout < rhs.heightForPayout;
    }

    // Serialization methods
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(walletName);
        READWRITE(heightForPayout);
        READWRITE(totalPayoutAmt);
        READWRITE(payoutSrc);
        READWRITE(tgtAssetName);
        READWRITE(exceptionAddresses);
    }
};

class CRewardRequestDB  : public CDBWrapper
{
public:
    explicit CRewardRequestDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CRewardRequestDB(const CRewardRequestDB&) = delete;
    CRewardRequestDB& operator=(const CRewardRequestDB&) = delete;

    // Schedule a pending reward payout
    bool SchedulePendingReward(const CRewardRequestDBEntry & p_newReward);

    // Remove a reward that has been paid out
    bool RemoveCompletedReward(const CRewardRequestDBEntry & p_completedReward);

    // Find out if any reward payments are scheduled at the specified height
    bool AreRewardsScheduledForHeight(const int & p_blockHeight);

    // Retrieve all reward records *for a specific asset* (if specified) at the provided block height
    bool LoadPayableRewardsForAsset(
        const std::string & p_assetName, const int & p_blockHeight,
        std::set<CRewardRequestDBEntry> & p_dbEntries);

    bool Flush();
};


#endif //REWARDREQUESTDB_H
