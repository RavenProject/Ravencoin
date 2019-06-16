// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assetsnapshotdb.h"
#include "validation.h"

#include <boost/algorithm/string.hpp>

static const char SNAPSHOTCHECK_FLAG = 'C'; // Snapshot Check

//  Addresses are delimited by commas
static const std::string ADDRESS_COMMA_DELIMITER = ",";

CAssetSnapshotDBEntry::CAssetSnapshotDBEntry()
{
    SetNull();
}

CAssetSnapshotDBEntry::CAssetSnapshotDBEntry(
    const int p_snapshotHeight, const std::string & p_assetName,
    const CAmount & p_totalAmtOwned,
    const std::set<std::pair<std::string, CAmount>> & p_ownersAndAmounts
)
{
    SetNull();

    height = p_snapshotHeight;
    assetName = p_assetName;
    totalAmountOwned = p_totalAmtOwned;
    for (auto const & currPair : p_ownersAndAmounts) {
        ownersAndAmounts.insert(currPair);
    }
}

CAssetSnapshotDB::CAssetSnapshotDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "assetsnapshot", nCacheSize, fMemory, fWipe) {
}

bool CAssetSnapshotDB::AddAssetOwnershipSnapshot(
    const std::string & p_assetName, const int & p_height,
    const std::string & p_exceptionAddrList)
{
    CAssetSnapshotDBEntry snapshotEntry;

    //  Retrieve the asset snapshot at this height
    if (RetrieveOwnershipSnapshot(p_assetName, p_height, snapshotEntry)) {
        //  Snapshot already exists, so bail
        return true;
    }

    //  Retrieve ownership interest for the asset at this height
    if (passetsdb == nullptr) {
        LogPrintf("AddAssetOwnershipSnapshot: Invalid assets DB!\n");
        return false;
    }

    std::vector<std::string> exceptionAddressList;
    boost::split(exceptionAddressList, p_exceptionAddrList, boost::is_any_of(ADDRESS_COMMA_DELIMITER));

    CAmount totalAssetAmt = 0;
    if (!passetsdb->AssetTotalAmountNotExcluded(p_assetName, exceptionAddressList, totalAssetAmt)) {
        LogPrintf("AddAssetOwnershipSnapshot: Failed to retrieve total asset amount for '%s'\n", p_assetName.c_str());
        return false;
    }
    if (totalAssetAmt <= 0) {
        LogPrintf("AddAssetOwnershipSnapshot: No instances of asset '%s' are owned by non-exception addresses\n", p_assetName.c_str());
        return false;
    }

    std::set<std::pair<std::string, CAmount>> ownersAndAmounts;
    std::vector<std::pair<std::string, CAmount>> tempOwnersAndAmounts;
    int totalEntryCount;

    if (!passetsdb->AssetAddressDir(tempOwnersAndAmounts, totalEntryCount, true, p_assetName, INT_MAX, 0)) {
        LogPrintf("AddAssetOwnershipSnapshot: Failed to retrieve assets directory for '%s'\n", p_assetName.c_str());
        return false;
    }

    //  Retrieve all of the addresses/amounts in batches
    const int MAX_RETRIEVAL_COUNT = 100;
    bool errorsOccurred = false;

    for (int retrievalOffset = 0; retrievalOffset < totalEntryCount; retrievalOffset += MAX_RETRIEVAL_COUNT) {
        //  Retrieve the specified segment of addresses
        tempOwnersAndAmounts.clear();

        if (!passetsdb->AssetAddressDir(tempOwnersAndAmounts, totalEntryCount, false, p_assetName, MAX_RETRIEVAL_COUNT, retrievalOffset)) {
            LogPrintf("AddAssetOwnershipSnapshot: Failed to retrieve assets directory for '%s'\n", p_assetName.c_str());
            errorsOccurred = true;
            break;
        }

        //  Verify that some addresses were returned
        if (tempOwnersAndAmounts.size() == 0) {
            LogPrintf("AddAssetOwnershipSnapshot: No addresses were retrieved.\n");
            continue;
        }

        //  Move these into the main set
        for (auto const & currPair : tempOwnersAndAmounts) {
            ownersAndAmounts.insert(currPair);
        }
        tempOwnersAndAmounts.clear();
    }

    if (errorsOccurred) {
        LogPrintf("AddAssetOwnershipSnapshot: Errors occurred while acquiring ownership info for asset '%s'.\n", p_assetName.c_str());
        return false;
    }
    if (ownersAndAmounts.size() == 0) {
        LogPrintf("AddAssetOwnershipSnapshot: No owners exist for asset '%s'.\n", p_assetName.c_str());
        return false;
    }

    //  Add them to the existing list
    std::set<CAssetSnapshotDBEntry> snapshotEntries;

    if (!Read(std::make_pair(SNAPSHOTCHECK_FLAG, p_height), snapshotEntries)) {
        //  This is fine, snapshots won't always exist
    }

    snapshotEntries.insert(CAssetSnapshotDBEntry(p_height, p_assetName, totalAssetAmt, ownersAndAmounts));

    //  And save the list back to the DB
    if (Write(std::make_pair(SNAPSHOTCHECK_FLAG, p_height), snapshotEntries)) {
        LogPrintf("AddAssetOwnershipSnapshot: Successfully added snapshot for '%s' at height %d (totalAmt = %d, ownerCount = %d).\n",
            p_assetName.c_str(), p_height, totalAssetAmt, ownersAndAmounts.size());
        return true;
    }
    return false;
}

bool CAssetSnapshotDB::RetrieveOwnershipSnapshot(
    const std::string & p_assetName, const int & p_height,
    CAssetSnapshotDBEntry & p_snapshotEntry)
{
    std::set<CAssetSnapshotDBEntry> snapshotEntries;

    //  Load up the snapshot entries at this height
    if (!Read(std::make_pair(SNAPSHOTCHECK_FLAG, p_height), snapshotEntries)) {
        return false;
    }

    //  Find out if this specific asset ownership entry for this height exists
    for (auto const & entry : snapshotEntries) {
        if (p_assetName == entry.assetName) {
            p_snapshotEntry = entry;

            return true;
        }
    }

    return false;
}

bool CAssetSnapshotDB::RemoveOwnershipSnapshot(
        const std::string & p_assetName, const int & p_height)
{
    //  Find out if we're nuking all snapshots for the current asset
    if (p_height == 0) {
        LogPrintf("%s : Removing all snapshots for asset '%s'!\n",
            __func__, p_assetName.c_str());

        std::unique_ptr<CDBIterator> pcursor(NewIterator());

        pcursor->SeekToFirst();

        // Find out if any pending rewards exist at or below the specified height
        while (pcursor->Valid()) {
            boost::this_thread::interruption_point();
            std::pair<char, int> key;

            //  Retrieve entries at the current node and remove this asset, if present
            if (pcursor->GetKey(key) && key.first == SNAPSHOTCHECK_FLAG) {
                std::set<CAssetSnapshotDBEntry> snapshotEntries;
                if (pcursor->GetValue(snapshotEntries)) {
                    if (!RemoveAssetFromEntries(p_assetName, key.second, snapshotEntries)) {
                        LogPrintf("%s : Failed to remove entries for asset '%s' at height %d!\n",
                            __func__, p_assetName.c_str(), key.second);
                    }
                }
            }

            pcursor->Next();
        }

        //  Always return true, even if failures occur.
        //  User can cleanup their snapshot DB by nuking it if something goes wrong.
        return true;
    }

    LogPrintf("%s : Removing snapshots for asset '%s' at height %d!\n",
        __func__, p_assetName.c_str(), p_height);

    //  Or just those at a specific height
    std::set<CAssetSnapshotDBEntry> snapshotEntries;

    //  Load up the snapshot entries at this height
    if (!Read(std::make_pair(SNAPSHOTCHECK_FLAG, p_height), snapshotEntries)) {
        //  If it doesn't exist (what?!) that's fine.
        return true;
    }

    return RemoveAssetFromEntries(p_assetName, p_height, snapshotEntries);
}

bool CAssetSnapshotDB::RemoveAssetFromEntries(
    const std::string & p_assetName, int p_height,
    std::set<CAssetSnapshotDBEntry> & p_snapshotEntries)
{
    //  Find out if this specific asset ownership entry for this height exists
    std::set<CAssetSnapshotDBEntry>::iterator entryIT;

    for (entryIT = p_snapshotEntries.begin(); entryIT != p_snapshotEntries.end(); ) {
        if (entryIT->assetName == p_assetName) {
            entryIT = p_snapshotEntries.erase(entryIT);
        }
        else {
            ++entryIT;
        }
    }

    //  If it is non-empty, write it back out
    if (p_snapshotEntries.size() > 0) {
        return Write(std::make_pair(SNAPSHOTCHECK_FLAG, p_height), p_snapshotEntries);
    }

    //  Otherwise, erase it
    return Erase(std::make_pair(SNAPSHOTCHECK_FLAG, p_height), true);
}