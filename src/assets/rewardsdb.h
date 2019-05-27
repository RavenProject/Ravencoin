// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RAVENCOIN_REWARDS_H
#define RAVENCOIN_REWARDS_H

#include <dbwrapper.h>

#include <string>
#include <vector>
#include <set>

class CRewardsDBEntry
{
public:
    int heightForPayout;
    int64_t totalPayoutAmt;
    std::string payoutSrc;
    std::string tgtAssetName;
    std::string exceptionAddresses;

    bool operator<(const CRewardsDBEntry &rhs) const {
        return heightForPayout < rhs.heightForPayout;
    }

    // Serialization methods
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action) {
        READWRITE(heightForPayout);
        READWRITE(totalPayoutAmt);
        READWRITE(payoutSrc);
        READWRITE(tgtAssetName);
        READWRITE(exceptionAddresses);
    }
};

class CRewardsDB  : public CDBWrapper
{
public:
    explicit CRewardsDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CRewardsDB(const CRewardsDB&) = delete;
    CRewardsDB& operator=(const CRewardsDB&) = delete;

    // Add reward record
    bool SchedulePendingReward(const CRewardsDBEntry & p_newReward);

    // Add reward record
    bool RemoveCompletedReward(const CRewardsDBEntry & p_completedReward);

    // Retrieve all reward records lower than the provided block height
    bool LoadPayableRewards(std::set<CRewardsDBEntry> & p_dbEntries, const int & p_minBlockHeight);

    bool Flush();
};


#endif //RAVENCOIN_REWARDS_H
