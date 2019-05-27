// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rewardsdb.h"

static const char SCHEDULEDREWARD_FLAG = 'S';

CRewardsDB::CRewardsDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "Rewards", nCacheSize, fMemory, fWipe) {
}

bool CRewardsDB::SchedulePendingReward(const CRewardsDBEntry & p_newReward)
{
    //  Retrieve height-based entry which contains list of rewards to pay at that height
    std::set<CRewardsDBEntry> entriesAtSpecifiedHeight;

    //  Result doesn't matter, as this might be the first entry added at this height
    Read(std::make_pair(SCHEDULEDREWARD_FLAG, p_newReward.heightForPayout), entriesAtSpecifiedHeight);

    //  Add this entry to the list
    entriesAtSpecifiedHeight.insert(p_newReward);

    //  Overwrite the entry in the database
    return Write(std::make_pair(SCHEDULEDREWARD_FLAG, p_newReward.heightForPayout), entriesAtSpecifiedHeight);
}

bool CRewardsDB::RemoveCompletedReward(const CRewardsDBEntry & p_completedReward)
{
    //  Retrieve height-based entry which contains list of rewards to pay at that height
    std::set<CRewardsDBEntry> entriesAtSpecifiedHeight;

    //  If this fails, bail, since we can't be sure we won't blow away valid entries
    if (!Read(std::make_pair(SCHEDULEDREWARD_FLAG, p_completedReward.heightForPayout), entriesAtSpecifiedHeight)) {
        return false;
    }

    //  If the list is empty, bail
    if (entriesAtSpecifiedHeight.size() == 0) {
        return true;
    }

    //  Remove this entry from the list
    std::set<CRewardsDBEntry>::iterator entryIT;

    for (entryIT = entriesAtSpecifiedHeight.begin(); entryIT != entriesAtSpecifiedHeight.end(); ) {
        if (entryIT->tgtAssetName.compare(p_completedReward.tgtAssetName) == 0) {
            entryIT = entriesAtSpecifiedHeight.erase(entryIT);
        }
        else {
            ++entryIT;
        }
    }

    //  If the list still has entries, overwrite the record in the database
    if (entriesAtSpecifiedHeight.size() > 0) {
        return Write(std::make_pair(SCHEDULEDREWARD_FLAG, p_completedReward.heightForPayout), entriesAtSpecifiedHeight);
    }

    //  Otherwise, erase the entire entry since none are left.
    return Erase(std::make_pair(SCHEDULEDREWARD_FLAG, p_completedReward.heightForPayout));
}

bool CRewardsDB::LoadPayableRewards(
    std::set<CRewardsDBEntry> & p_dbEntries, const int & p_minBlockHeight)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->SeekToFirst();

    // Load all pending rewards
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, int> key;

        //  Only retrieve entries earlier than the provided block height
        if (pcursor->GetKey(key) && key.first == SCHEDULEDREWARD_FLAG && key.second <= p_minBlockHeight) {
            CRewardsDBEntry dbEntry;

            if (pcursor->GetValue(dbEntry)) {
                p_dbEntries.insert(dbEntry);
            } else {
                LogPrintf("%s: failed to read reward\n", __func__);
            }

            pcursor->Next();
        } else {
            break;
        }
    }

    return true;
}
