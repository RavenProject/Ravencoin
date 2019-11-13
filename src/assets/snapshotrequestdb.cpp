// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <set>
#include <boost/thread.hpp>

#include "snapshotrequestdb.h"

static const char SNAPSHOTREQUEST_FLAG = 'S';

static const char DISTRIBUTEREQUEST_FLAG = 'D';
static const char DISTRIBUTETRANSACTION_FLAG = 'T';

CSnapshotRequestDBEntry::CSnapshotRequestDBEntry()
{
    SetNull();
}

CSnapshotRequestDBEntry::CSnapshotRequestDBEntry(
    const std::string & p_assetName, int p_heightForSnapshot
)
{
    SetNull();

    assetName = p_assetName;
    heightForSnapshot = p_heightForSnapshot;

    heightAndName = std::to_string(heightForSnapshot) + assetName;
}

CSnapshotRequestDB::CSnapshotRequestDB(
    size_t nCacheSize, bool fMemory, bool fWipe)
    : CDBWrapper(GetDataDir() / "rewards" / "snapshotrequest", nCacheSize, fMemory, fWipe) {
}

bool CSnapshotRequestDB::ScheduleSnapshot(
    const std::string & p_assetName, int p_heightForSnapshot
)
{
    LogPrint(BCLog::REWARDS, "%s : Requesting snapshot: assetName='%s', height=%d\n",
        __func__,
        p_assetName.c_str(), p_heightForSnapshot);

    CSnapshotRequestDBEntry snapshotRequest(p_assetName, p_heightForSnapshot);

    //  Add the entry to the database
    bool succeeded = Write(std::make_pair(SNAPSHOTREQUEST_FLAG, snapshotRequest.heightAndName), snapshotRequest);

    LogPrint(BCLog::REWARDS, "%s : Snapshot request for '%s' at height %d %s!\n",
        __func__,
        p_assetName.c_str(), p_heightForSnapshot,
        succeeded ? "succeeded" : "failed");

    return succeeded;
}

bool CSnapshotRequestDB::RetrieveSnapshotRequest(
    const std::string & p_assetName, int p_heightForSnapshot,
    CSnapshotRequestDBEntry & p_snapshotRequest
)
{
    //  Load up the snapshot entries at this height
    std::string heightAndName = std::to_string(p_heightForSnapshot) + p_assetName;

    LogPrint(BCLog::REWARDS, "%s : Looking for snapshot request '%s'\n",
        __func__, heightAndName.c_str());

    bool succeeded = Read(std::make_pair(SNAPSHOTREQUEST_FLAG, heightAndName), p_snapshotRequest);

    LogPrint(BCLog::REWARDS, "%s : Retrieval of snapshot request for '%s' %s!\n",
        __func__,
        heightAndName.c_str(),
        succeeded ? "succeeded" : "failed");

    return succeeded;
}

bool CSnapshotRequestDB::ContainsSnapshotRequest(const std::string & p_assetName, int p_heightForSnapshot)
{
    std::string heightAndName = std::to_string(p_heightForSnapshot) + p_assetName;

    CSnapshotRequestDBEntry snapshotRequest;
    return Read(std::make_pair(SNAPSHOTREQUEST_FLAG, heightAndName), snapshotRequest);
}

bool CSnapshotRequestDB::RemoveSnapshotRequest(
    const std::string & p_assetName, int p_heightForSnapshot
)
{
    //  Load up the snapshot entries at this height
    std::string heightAndName = std::to_string(p_heightForSnapshot) + p_assetName;

    LogPrint(BCLog::REWARDS, "%s : Attempting to remove snapshot request '%s'\n",
        __func__,
        heightAndName.c_str());

    //  Otherwise, erase the entire entry since none are left.
    bool succeeded = Erase(std::make_pair(SNAPSHOTREQUEST_FLAG, heightAndName), true);

    LogPrint(BCLog::REWARDS, "%s : Removal of snapshot request for '%s' %s!\n",
        __func__,
        heightAndName.c_str(),
        succeeded ? "succeeded" : "failed");

    return succeeded;
}

bool CSnapshotRequestDB::RetrieveSnapshotRequestsForHeight(
    const std::string & p_assetName, int p_blockHeight,
    std::set<CSnapshotRequestDBEntry> & p_assetsToSnapshot)
{
    bool assetNameProvided = p_assetName.length() > 0;
    if (assetNameProvided) {
        LogPrint(BCLog::REWARDS, "%s : Looking for snapshot requests for asset '%s' at height %d!\n",
            __func__,
            p_assetName.c_str(), p_blockHeight);
    }
    else {
        LogPrint(BCLog::REWARDS, "%s : Looking for all snapshot requests at height %d!\n",
            __func__,
            p_blockHeight);
    }

    p_assetsToSnapshot.clear();

    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->SeekToFirst();

    // Load all pending rewards
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, int> key;

        //  Only retrieve entries at the provided block height
        if (pcursor->GetKey(key) && key.first == SNAPSHOTREQUEST_FLAG) {
            CSnapshotRequestDBEntry reqDbEntry;

            if (pcursor->GetValue(reqDbEntry)) {
                if (p_blockHeight == 0 || reqDbEntry.heightForSnapshot == p_blockHeight) {
                    //  If an asset was specified, only add entries for it.
                    //  Otherwise, retrieve all entries.
                    if (!assetNameProvided || p_assetName == reqDbEntry.assetName) {
                        p_assetsToSnapshot.insert(reqDbEntry);
                    }
                }
            } else {
                LogPrint(BCLog::REWARDS, "%s: Failed to read snapshot request\n", __func__);
            }
        }

        pcursor->Next();
    }

    return true;
}

CDistributeSnapshotRequestDB::CDistributeSnapshotRequestDB(
        size_t nCacheSize, bool fMemory, bool fWipe)
        : CDBWrapper(GetDataDir() / "rewards" / "distributerequests", nCacheSize, fMemory, fWipe) {
}

// Schedule a distribution to occur
bool CDistributeSnapshotRequestDB::AddDistributeSnapshot(const uint256& hash, const CRewardSnapshot& p_rewardSnapshot)
{
    return Write(std::make_pair(DISTRIBUTEREQUEST_FLAG, hash), p_rewardSnapshot);
}

bool CDistributeSnapshotRequestDB::OverrideDistributeSnapshot(const uint256& hash, const CRewardSnapshot& p_rewardSnapshot)
{
    return  Write(std::make_pair(DISTRIBUTEREQUEST_FLAG, hash), p_rewardSnapshot);
}

bool OverrideDistributeSnapshot(const uint256& hash, const CRewardSnapshot& p_rewardSnapshot);

// Add a new distribution transaction
bool CDistributeSnapshotRequestDB::AddDistributeTransaction(const uint256& hash, const int& nBatchNumber, const uint256& txid)
{
    return Write(std::make_pair(DISTRIBUTETRANSACTION_FLAG, std::make_pair(hash, nBatchNumber)), txid);
}

// Find a distribution transaction from the database
bool CDistributeSnapshotRequestDB::GetDistributeTransaction(const uint256& hash, const int& nBatchNumber, uint256& txid)
{
    return Read(std::make_pair(DISTRIBUTETRANSACTION_FLAG, std::make_pair(hash, nBatchNumber)), txid);
}

//  Find a distribute snapshot request
bool CDistributeSnapshotRequestDB::RetrieveDistributeSnapshotRequest(const uint256& hash, CRewardSnapshot& p_rewardSnapshot)
{
    return Read(std::make_pair(DISTRIBUTEREQUEST_FLAG, hash), p_rewardSnapshot);
}

// Remove a distribute snapshot request
bool CDistributeSnapshotRequestDB::RemoveDistributeSnapshotRequest(const uint256& hash)
{
    return Erase(std::make_pair(DISTRIBUTEREQUEST_FLAG, hash));
}

// Load all distribute snapshot from database
void CDistributeSnapshotRequestDB::LoadAllDistributeSnapshot(std::map<uint256, CRewardSnapshot>& mapRewardSnapshots)
{
    std::unique_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(std::make_pair(DISTRIBUTEREQUEST_FLAG, uint256()));

    // Load all pending rewards
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;

        //  Only retrieve entries at the provided block height
        if (pcursor->GetKey(key) && key.first == DISTRIBUTEREQUEST_FLAG) {
            CRewardSnapshot distributeDbEntry;

            if (pcursor->GetValue(distributeDbEntry)) {
                mapRewardSnapshots[key.second] = distributeDbEntry;
            } else {
                LogPrint(BCLog::REWARDS, "%s: Failed to read snapshot distribution for key: %s\n", __func__, key.second.GetHex());
            }
        }

        pcursor->Next();
    }

    for (auto const & item : mapRewardSnapshots) {
        LogPrint(BCLog::REWARDS, "%s : Found snapshot distribution request for Owner: %s,  Distribution: %s, Exception: %s, Height: %d, Status: %d\n",
                 __func__,
                 item.second.strOwnershipAsset,
                 item.second.strDistributionAsset,
                 item.second.strExceptionAddresses,
                 item.second.nHeight,
                 item.second.nStatus);
    }
}
