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

    // Remove a snapshot request
    bool RemoveSnapshotRequest(const std::string & p_assetName, int p_heightForSnapshot);

    //  Retrieve all snapshot requests at the provided block height,
    //      limited to the specified asset name (if provided)
    bool RetrieveSnapshotRequestsForHeight(
        const std::string & p_assetName, int p_blockHeight,
        std::set<std::string> & p_assetsToSnapshot
    );
};


#endif //SNAPSHOTREQUESTDB_H
