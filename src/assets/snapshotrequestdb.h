// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SNAPSHOTREQUESTDB_H
#define SNAPSHOTREQUESTDB_H

#include <dbwrapper.h>

#include <string>
#include <vector>
#include <set>

#include "amount.h"
#include "assets/rewards.h"

class CSnapshotRequestDBEntry
{
public:
    std::string assetName;
    int heightForSnapshot;

    //  Used as the DB key for the snapshot request
    std::string heightAndName;

    CSnapshotRequestDBEntry();
    CSnapshotRequestDBEntry(
        const std::string & p_assetName, int p_heightForSnapshot
    );

    void SetNull()
    {
        assetName = "";
        heightForSnapshot = 0;

        heightAndName = "";
    }

    bool operator<(const CSnapshotRequestDBEntry &rhs) const
    {
        return heightAndName < rhs.heightAndName;
    }

    // Serialization methods
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(assetName);
        READWRITE(heightForSnapshot);
        READWRITE(heightAndName);
    }
};

class CSnapshotRequestDB  : public CDBWrapper
{
public:
    explicit CSnapshotRequestDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CSnapshotRequestDB(const CSnapshotRequestDB&) = delete;
    CSnapshotRequestDB& operator=(const CSnapshotRequestDB&) = delete;

    // Schedule a asset snapshot
    bool ScheduleSnapshot(
        const std::string & p_assetName, int p_heightForSnapshot
    );

    //  Find a snapshot request using its ID
    bool RetrieveSnapshotRequest(
        const std::string & p_assetName, int p_heightForSnapshot,
        CSnapshotRequestDBEntry & p_snapshotRequest
    );

    bool ContainsSnapshotRequest(const std::string & p_assetName, int p_heightForSnapshot);

    // Remove a snapshot request
    bool RemoveSnapshotRequest(const std::string & p_assetName, int p_heightForSnapshot);

    //  Retrieve all snapshot requests at the provided block height
    //      (if the block height is zero, retrieve requests at ALL HEIGHTS),
    //      limited to the specified asset name (if provided)
    bool RetrieveSnapshotRequestsForHeight(
        const std::string & p_assetName, int p_blockHeight,
        std::set<CSnapshotRequestDBEntry> & p_assetsToSnapshot
    );
};

class CDistributeSnapshotRequestDB  : public CDBWrapper
{
public:
    explicit CDistributeSnapshotRequestDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CDistributeSnapshotRequestDB(const CDistributeSnapshotRequestDB&) = delete;
    CDistributeSnapshotRequestDB& operator=(const CDistributeSnapshotRequestDB&) = delete;

    // Schedule a asset snapshot
    bool AddDistributeSnapshot(const uint256& hash, const CRewardSnapshot& p_rewardSnapshot);
    bool OverrideDistributeSnapshot(const uint256& hash, const CRewardSnapshot& p_rewardSnapshot);

    //  Find a snapshot request using its ID
    bool RetrieveDistributeSnapshotRequest(const uint256& hash, CRewardSnapshot& p_rewardSnapshot);

    // Remove a snapshot request
    bool RemoveDistributeSnapshotRequest(const uint256& hash);

    bool AddDistributeTransaction(const uint256& hash, const int& nBatchNumber, const uint256& txid);
    bool GetDistributeTransaction(const uint256& hash, const int& nBatchNumber, uint256& txid);

    void LoadAllDistributeSnapshot(std::map<uint256, CRewardSnapshot>& mapRewardSnapshots);


};



#endif //SNAPSHOTREQUESTDB_H
