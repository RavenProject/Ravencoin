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