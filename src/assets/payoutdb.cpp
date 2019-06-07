// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "payoutdb.h"
#include "validation.h"

#include <boost/algorithm/string.hpp>

static const char PAYOUT_FLAG = 'P'; // Indicates a payout record

//  Addresses are delimited by commas
static const std::string ADDRESS_COMMA_DELIMITER = ",";

CPayoutDBEntry::CPayoutDBEntry()
{
    SetNull();
}

CPayoutDBEntry::CPayoutDBEntry(
    const std::string & p_assetName,
    const std::string & p_srcAssetName,
    const std::set<std::pair<std::string, CAmount>> & p_ownersAndPayouts
)
{
    SetNull();

    assetName = p_assetName;
    srcAssetName = p_srcAssetName;
    for (auto const & currPair : p_ownersAndPayouts) {
        ownersAndPayouts.insert(currPair);
    }
}

CPayoutDB::CPayoutDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "payout", nCacheSize, fMemory, fWipe) {
}

bool CPayoutDB::GeneratePayouts(
        const CRewardRequestDBEntry & p_rewardReq,
        const CAssetSnapshotDBEntry & p_assetSnapshot)
{
    if (passets == nullptr) {
        LogPrintf("GeneratePayouts: Invalid assets cache!\n");
        return false;
    }

    //  Get details on the specified source asset
    CNewAsset srcAsset;
    bool srcIsIndivisible = false;
    CAmount srcUnitMultiplier = COIN;  //  Default to multiplier for RVN

    if (p_rewardReq.payoutSrc != "RVN") {
        if (!passets->GetAssetMetaDataIfExists(p_rewardReq.payoutSrc, srcAsset)) {
            LogPrintf("GeneratePayouts: Failed to retrieve asset details for '%s'\n", p_rewardReq.payoutSrc.c_str());
            return false;
        }

        LogPrintf("GeneratePayouts: Source asset '%s' has units %d and multiplier %d\n",
            p_rewardReq.payoutSrc.c_str(), srcAsset.units, srcUnitMultiplier);

        //  If the token is indivisible, signal this to later code with a zero multiplier
        if (srcAsset.units == 0) {
            srcIsIndivisible = true;
            srcUnitMultiplier = 1;
        }
        else {
            srcUnitMultiplier = pow(10, srcAsset.units);
        }
    }
    else {
        LogPrintf("GeneratePayouts: Source is RVN with multiplier %d\n", srcUnitMultiplier);
    }

    //  Get details on the target asset
    CNewAsset tgtAsset;
    CAmount tgtUnitMultiplier = 0;
    if (!passets->GetAssetMetaDataIfExists(p_rewardReq.tgtAssetName, tgtAsset)) {
        LogPrintf("GeneratePayouts: Failed to retrieve asset details for '%s'\n", p_rewardReq.tgtAssetName.c_str());
        return false;
    }

    //  If the token is indivisible, signal this to later code with a zero multiplier
    if (tgtAsset.units == 0) {
        tgtUnitMultiplier = 1;
    }
    else {
        tgtUnitMultiplier = pow(10, tgtAsset.units);
    }

    LogPrintf("GeneratePayouts: Target asset '%s' has units %d and multiplier %d\n",
        p_rewardReq.tgtAssetName.c_str(), tgtAsset.units, tgtUnitMultiplier);

    //  If the asset is indivisible, and there are fewer rewards than individual ownerships,
    //      fail the distribution, since it is not possible to reward all ownerships equally.
    if (srcIsIndivisible && p_rewardReq.totalPayoutAmt < p_assetSnapshot.totalAmountOwned) {
        LogPrintf("GeneratePayouts: Source asset '%s' is indivisible, and not enough reward value was provided for all ownership of '%s'\n",
            p_rewardReq.payoutSrc.c_str(), p_rewardReq.tgtAssetName.c_str());
        return false;
    }

    //  Remove exception addresses from the list
    std::vector<std::string> exceptionAddressList;
    boost::split(exceptionAddressList, p_rewardReq.exceptionAddresses, boost::is_any_of(ADDRESS_COMMA_DELIMITER));

    std::vector<std::pair<std::string, CAmount>>::iterator entryIT;

    std::vector<std::pair<std::string, CAmount>> nonExceptionOwnersAndAmts;

    for (auto const & currPair : p_assetSnapshot.ownersAndAmounts) {
        nonExceptionOwnersAndAmts.emplace_back(currPair);
    }

    for (entryIT = nonExceptionOwnersAndAmts.begin(); entryIT != nonExceptionOwnersAndAmts.end(); ) {
        bool isExceptionAddress = false;

        for (auto const & exceptionAddr : exceptionAddressList) {
            if (entryIT->first == exceptionAddr) {
                isExceptionAddress = true;
                break;
            }
        }

        if (isExceptionAddress) {
            entryIT = nonExceptionOwnersAndAmts.erase(entryIT);
        }
        else {
            ++entryIT;
        }
    }

    //  Make sure we have some addresses to pay to
    if (nonExceptionOwnersAndAmts.size() == 0) {
        LogPrintf("GeneratePayouts: Ownership of  '%s' includes only exception addresses.\n",
            p_rewardReq.tgtAssetName.c_str());
        return false;
    }

    //  Loop through asset owners
    double floatingPaymentAmt = p_rewardReq.totalPayoutAmt;
    double floatingTotalAssetAmt = p_assetSnapshot.totalAmountOwned;

    std::set<std::pair<std::string, CAmount>> ownersAndPayouts;

    for (auto & ownerAndAmt : nonExceptionOwnersAndAmts) {
        //  Calculate the reward amount
        double floatingCurrAssetAmt = ownerAndAmt.second;
        double floatingRewardAmt = floatingPaymentAmt * floatingCurrAssetAmt / floatingTotalAssetAmt;
        CAmount rewardAmt = floatingRewardAmt;

        LogPrintf("GeneratePayouts: Found ownership address for '%s': '%s' owns %d.%08d => reward %d.%08d\n",
            p_rewardReq.tgtAssetName.c_str(), ownerAndAmt.first.c_str(),
            ownerAndAmt.second / tgtUnitMultiplier, ownerAndAmt.second % tgtUnitMultiplier,
            rewardAmt / srcUnitMultiplier, rewardAmt % srcUnitMultiplier);

        //  And save it into our list
        ownersAndPayouts.insert(std::make_pair(ownerAndAmt.first, rewardAmt));
    }

    //  Don't insert an empty payout record
    if (ownersAndPayouts.size() == 0) {
        LogPrintf("GeneratePayouts: No ownership payout records generated for for '%s'\n", p_rewardReq.tgtAssetName.c_str());
        return false;
    }

    //  Add the new entry to the existing list
    std::set<CPayoutDBEntry> payoutEntries;

    if (!Read(std::make_pair(PAYOUT_FLAG, p_rewardReq.tgtAssetName), payoutEntries)) {
        //  This is fine, list might not exist yet.
    }

    payoutEntries.insert(CPayoutDBEntry(p_rewardReq.tgtAssetName, p_rewardReq.payoutSrc, ownersAndPayouts));

    //  And save the list back to the DB
    return Write(std::make_pair(PAYOUT_FLAG, p_rewardReq.tgtAssetName), payoutEntries);
}

bool CPayoutDB::RetrievePayouts(
    const std::string & p_assetName,
    std::set<CPayoutDBEntry> & p_payoutEntries)
{
    //  Load up the payout entries for the specified asset
    return Read(std::make_pair(PAYOUT_FLAG, p_assetName), p_payoutEntries);
}