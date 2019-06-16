// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ASSETSNAPSHOTDB_H
#define ASSETSNAPSHOTDB_H

#include <set>

#include <dbwrapper.h>
#include "amount.h"

class CAssetSnapshotDBEntry
{
public:
    int height;
    std::string assetName;
    CAmount totalAmountOwned;
    std::set<std::pair<std::string, CAmount>> ownersAndAmounts;

    CAssetSnapshotDBEntry();
    CAssetSnapshotDBEntry(
        const int p_snapshotHeight, const std::string & p_assetName,
        const CAmount & p_totalAmtOwned,
        const std::set<std::pair<std::string, CAmount>> & p_ownersAndAmounts
    );

    void SetNull()
    {
        height = 0;
        assetName = "";
        totalAmountOwned = 0;
        ownersAndAmounts.clear();
    }

    bool operator<(const CAssetSnapshotDBEntry &rhs) const
    {
        std::string myHeightAndAsset = std::to_string(height) + assetName;
        std::string theirHeightAndAsset = std::to_string(rhs.height) + rhs.assetName;
        return myHeightAndAsset < theirHeightAndAsset;
    }

    // Serialization methods
    ADD_SERIALIZE_METHODS;

    template<typename Stream, typename Operation>
    inline void SerializationOp(Stream &s, Operation ser_action)
    {
        READWRITE(height);
        READWRITE(assetName);
        READWRITE(totalAmountOwned);
        READWRITE(ownersAndAmounts);
    }
};

class CAssetSnapshotDB  : public CDBWrapper {
public:
    explicit CAssetSnapshotDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    CAssetSnapshotDB(const CAssetSnapshotDB&) = delete;
    CAssetSnapshotDB& operator=(const CAssetSnapshotDB&) = delete;

    //  Add an entry to the snapshot at the specified height
    bool AddAssetOwnershipSnapshot(
        const std::string & p_assetName, const int & p_height,
        const std::string & p_exceptionAddrList);

    //  Read all of the entries at a specified height
    bool RetrieveOwnershipSnapshot(
        const std::string & p_assetName, const int & p_height,
        CAssetSnapshotDBEntry & p_snapshotEntry);

    //  Remove the asset snapshot at the specified height
    bool RemoveOwnershipSnapshot(
        const std::string & p_assetName, const int & p_height);

    bool Flush();

private:
    //  Removes asset entries from the provided snapshot entries
    //      and writes the remainder back to the DB.
    bool RemoveAssetFromEntries(
        const std::string & p_assetName, int p_height,
        std::set<CAssetSnapshotDBEntry> & p_snapshotEntries);
};


#endif //ASSETSNAPSHOTDB_H
