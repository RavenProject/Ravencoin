// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <set>

#include "rewardrequestdb.h"

static const char SCHEDULEDREWARD_FLAG = 'S';

CRewardRequestDBEntry::CRewardRequestDBEntry()
{
    SetNull();
}

CRewardRequestDBEntry::CRewardRequestDBEntry(
    const std::string & p_rewardID,
    const std::string & p_walletName, const int p_heightForPayout, const CAmount p_totalPayoutAmt,
    const std::string & p_payoutSrc, const std::string & p_tgtAssetName, const std::string & p_exceptionAddresses
)
{
    SetNull();

    rewardID = p_rewardID;
    walletName = p_walletName;
    heightForPayout = p_heightForPayout;
    totalPayoutAmt = p_totalPayoutAmt;
    payoutSrc = p_payoutSrc;
    tgtAssetName = p_tgtAssetName;
    exceptionAddresses = p_exceptionAddresses;
}

CRewardRequestDB::CRewardRequestDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "rewardrequest", nCacheSize, fMemory, fWipe) {
}

bool CRewardRequestDB::SchedulePendingReward(const CRewardRequestDBEntry & p_newReward)
{
    LogPrintf("%s : Scheduling reward: rewardID='%s', wallet='%s', height=%d, amt=%lld, srcAsset='%s', tgtAsset='%s', exceptions='%s'\n",
        __func__,
        p_newReward.rewardID.c_str(),
        p_newReward.walletName.c_str(), p_newReward.heightForPayout, static_cast<long long>(p_newReward.totalPayoutAmt),
        p_newReward.payoutSrc.c_str(), p_newReward.tgtAssetName.c_str(),
        p_newReward.exceptionAddresses.c_str());

    //  Retrieve height-based entry which contains list of rewards to pay at that height
    std::set<CRewardRequestDBEntry> entriesAtSpecifiedHeight;

    //  Result doesn't matter, as this might be the first entry added at this height
    Read(std::make_pair(SCHEDULEDREWARD_FLAG, p_newReward.heightForPayout), entriesAtSpecifiedHeight);

    //  Add this entry to the list
    entriesAtSpecifiedHeight.insert(p_newReward);

    //  Overwrite the entry in the database
    bool succeeded = Write(std::make_pair(SCHEDULEDREWARD_FLAG, p_newReward.heightForPayout), entriesAtSpecifiedHeight);

    LogPrintf("%s : Scheduling reward %s %s!\n",
        __func__,
        p_newReward.rewardID.c_str(),
        succeeded ? "succeeded" : "failed");

    return succeeded;
}

bool CRewardRequestDB::RetrieveRewardWithID(
    const std::string & p_rewardID, CRewardRequestDBEntry & p_reward)
{
    LogPrintf("%s : Looking for scheduled reward with ID '%s'\n",
        __func__, p_rewardID.c_str());

    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->SeekToFirst();

    // Load all pending rewards
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, int> key;

        if (pcursor->GetKey(key) && key.first == SCHEDULEDREWARD_FLAG) {
            std::set<CRewardRequestDBEntry> dbEntrySet;

            if (pcursor->GetValue(dbEntrySet)) {
                for (auto const & entry : dbEntrySet) {
                    //  Only retrieve the specified reward
                    if (p_rewardID == entry.rewardID) {
                        LogPrintf("%s : Found reward: rewardID='%s', wallet='%s', height=%d, amt=%lld, srcAsset='%s', tgtAsset='%s', exceptions='%s'\n",
                            __func__,
                            entry.rewardID.c_str(),
                            entry.walletName.c_str(), entry.heightForPayout, static_cast<long long>(entry.totalPayoutAmt),
                            entry.payoutSrc.c_str(), entry.tgtAssetName.c_str(),
                            entry.exceptionAddresses.c_str());

                        p_reward = entry;

                        return true;
                    }
                }
            } else {
                LogPrintf("%s: Failed to read reward\n", __func__);
            }
        }

        pcursor->Next();
    }

    return false;
}

bool CRewardRequestDB::RemoveReward(const std::string & p_rewardID)
{
    LogPrintf("%s : Attempting to remove reward: rewardID='%s'\n",
        __func__,
        p_rewardID.c_str());

    CRewardRequestDBEntry rewardToRemove;
    if (!RetrieveRewardWithID(p_rewardID, rewardToRemove)) {
        LogPrintf("%s : Failed to find reward: rewardID='%s'!\n",
            __func__,
            p_rewardID.c_str());

        return false;
    }
    //  Retrieve height-based entry which contains list of rewards to pay at that height
    std::set<CRewardRequestDBEntry> entriesAtSpecifiedHeight;

    //  If this fails, bail, since we can't be sure we won't blow away valid entries
    if (!Read(std::make_pair(SCHEDULEDREWARD_FLAG, rewardToRemove.heightForPayout), entriesAtSpecifiedHeight)) {
        LogPrintf("%s : Failed to read scheduled rewards at specified height!\n", __func__);

        return false;
    }

    //  If the list is empty, bail
    if (entriesAtSpecifiedHeight.size() == 0) {
        LogPrintf("%s : No scheduled reward exists at specified height!\n", __func__);

        return true;
    }

    //  Remove this entry from the list
    std::set<CRewardRequestDBEntry>::iterator entryIT;

    for (entryIT = entriesAtSpecifiedHeight.begin(); entryIT != entriesAtSpecifiedHeight.end(); ) {
        if (entryIT->tgtAssetName == rewardToRemove.tgtAssetName) {
            LogPrintf("%s : Removing reward: rewardID='%s', wallet='%s', height=%d, amt=%lld, srcAsset='%s', tgtAsset='%s', exceptions='%s'\n",
                __func__,
                rewardToRemove.rewardID.c_str(),
                rewardToRemove.walletName.c_str(), rewardToRemove.heightForPayout, static_cast<long long>(rewardToRemove.totalPayoutAmt),
                rewardToRemove.payoutSrc.c_str(), rewardToRemove.tgtAssetName.c_str(),
                rewardToRemove.exceptionAddresses.c_str());

            entryIT = entriesAtSpecifiedHeight.erase(entryIT);
        }
        else {
            ++entryIT;
        }
    }

    //  If the list still has entries, overwrite the record in the database
    if (entriesAtSpecifiedHeight.size() > 0) {
        bool succeeded = Write(std::make_pair(SCHEDULEDREWARD_FLAG, rewardToRemove.heightForPayout), entriesAtSpecifiedHeight);

        LogPrintf("%s : Removal of scheduled reward %s!\n",
            __func__,
            succeeded ? "succeeded" : "failed");

        return succeeded;
    }

    //  Otherwise, erase the entire entry since none are left.
    bool succeeded = Erase(std::make_pair(SCHEDULEDREWARD_FLAG, rewardToRemove.heightForPayout));

    LogPrintf("%s : Removal of last scheduled reward %s!\n",
        __func__,
        succeeded ? "succeeded" : "failed");

    return succeeded;
}

bool CRewardRequestDB::AreRewardsScheduledForHeight(const int & p_maxBlockHeight)
{
    LogPrintf("%s : Looking for scheduled rewards at height %d!\n",
        __func__, p_maxBlockHeight);

    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->SeekToFirst();

    // Find out if any pending rewards exist at or below the specified height
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, int> key;

        //  Only retrieve entries earlier than the provided block height
        if (pcursor->GetKey(key) && key.first == SCHEDULEDREWARD_FLAG && key.second <= p_maxBlockHeight) {
            return true;
        }

        pcursor->Next();
    }

    return false;
}

bool CRewardRequestDB::LoadPayableRewardsForAsset(
    const std::string & p_assetName, const int & p_blockHeight,
    std::set<CRewardRequestDBEntry> & p_dbEntries)
{
    LogPrintf("%s : Looking for scheduled rewards for asset '%s' at height %d!\n",
        __func__, p_assetName.c_str(), p_blockHeight);

    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->SeekToFirst();

    // Load all pending rewards
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, int> key;

        //  Only retrieve entries at the provided block height
        if (pcursor->GetKey(key) && key.first == SCHEDULEDREWARD_FLAG && key.second == p_blockHeight) {
            std::set<CRewardRequestDBEntry> dbEntrySet;

            if (pcursor->GetValue(dbEntrySet)) {
                for (auto const & entry : dbEntrySet) {
                    //  If an asset was specified, only add entries for it.
                    //  Otherwise, retrieve all entries.
                    if (p_assetName.length() == 0 || p_assetName == entry.tgtAssetName) {
                        p_dbEntries.insert(entry);
                    }
                }
            } else {
                LogPrintf("%s: Failed to read reward\n", __func__);
            }
        }

        pcursor->Next();
    }

    for (auto const & reward : p_dbEntries) {
        LogPrintf("%s : Found payable reward: wallet='%s', height=%d, amt=%lld, srcAsset='%s', tgtAsset='%s', exceptions='%s'\n",
            __func__,
            reward.walletName.c_str(), reward.heightForPayout, static_cast<long long>(reward.totalPayoutAmt),
            reward.payoutSrc.c_str(), reward.tgtAssetName.c_str(),
            reward.exceptionAddresses.c_str());
    }

    return true;
}
