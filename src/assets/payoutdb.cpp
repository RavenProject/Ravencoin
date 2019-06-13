// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "payoutdb.h"
#include "validation.h"
#include "chainparams.h"

#include <boost/algorithm/string.hpp>

static const char PAYOUT_FLAG = 'P'; // Indicates a payout record

//  Addresses are delimited by commas
static const std::string ADDRESS_COMMA_DELIMITER = ",";

CPayment::CPayment()
{
    SetNull();
}

CPayment::CPayment(
    const std::string & p_address,
    const CAmount & p_payoutAmt,
    bool p_completed
)
{
    SetNull();

    address = p_address;
    payoutAmt = p_payoutAmt;
    completed = p_completed;
}

CPayoutDBEntry::CPayoutDBEntry()
{
    SetNull();
}

CPayoutDBEntry::CPayoutDBEntry(
    const std::string & p_rewardID,
    const std::string & p_assetName,
    const std::string & p_srcAssetName,
    const std::set<CPayment> & p_payments
)
{
    SetNull();

    rewardID = p_rewardID;
    assetName = p_assetName;
    srcAssetName = p_srcAssetName;
    for (auto const & currPayment : p_payments) {
        payments.insert(currPayment);
    }
}

CPayoutDB::CPayoutDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "payout", nCacheSize, fMemory, fWipe) {
}

bool CPayoutDB::GeneratePayouts(
        const CRewardRequestDBEntry & p_rewardReq,
        const CAssetSnapshotDBEntry & p_assetSnapshot,
        CPayoutDBEntry & p_payoutEntry)
{
    if (passets == nullptr) {
        LogPrintf("GeneratePayouts: Invalid assets cache!\n");
        return false;
    }
    if (pRewardRequestDb == nullptr) {
        LogPrintf("GeneratePayouts: Invalid reward request cache!\n");
        return false;
    }
    if (pAssetSnapshotDb == nullptr) {
        LogPrintf("GeneratePayouts: Invalid asset snapshot cache!\n");
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

    std::vector<CPayment>::iterator entryIT;

    std::vector<CPayment> nonExceptionPayments;

    for (auto const & currPair : p_assetSnapshot.ownersAndAmounts) {
        nonExceptionPayments.emplace_back(CPayment(currPair.first, currPair.second, false));
    }

    for (entryIT = nonExceptionPayments.begin(); entryIT != nonExceptionPayments.end(); ) {
        bool isExceptionAddress = false;

        for (auto const & exceptionAddr : exceptionAddressList) {
            if (entryIT->address == exceptionAddr) {
                isExceptionAddress = true;
                break;
            }
        }

        //  Ignore assets held by burn addresses
        if (Params().IsBurnAddress(entryIT->address)) {
            isExceptionAddress = true;
        }

        if (isExceptionAddress) {
            entryIT = nonExceptionPayments.erase(entryIT);
        }
        else {
            ++entryIT;
        }
    }

    //  Make sure we have some addresses to pay to
    if (nonExceptionPayments.size() == 0) {
        LogPrintf("GeneratePayouts: Ownership of '%s' includes only exception addresses.\n",
            p_rewardReq.tgtAssetName.c_str());
        return false;
    }

    //  Loop through asset owners
    double floatingPaymentAmt = p_rewardReq.totalPayoutAmt;
    double floatingTotalAssetAmt = p_assetSnapshot.totalAmountOwned;

    //  Update data in the provided payout entry
    p_payoutEntry.rewardID = p_rewardReq.rewardID;
    p_payoutEntry.assetName = p_rewardReq.tgtAssetName;
    p_payoutEntry.srcAssetName = p_rewardReq.payoutSrc;

    for (auto & payment : nonExceptionPayments) {
        //  Calculate the reward amount
        double floatingCurrAssetAmt = payment.payoutAmt;
        double floatingRewardAmt = floatingPaymentAmt * floatingCurrAssetAmt / floatingTotalAssetAmt;
        CAmount rewardAmt = floatingRewardAmt;

        LogPrintf("GeneratePayouts: Found ownership address for '%s': '%s' owns %d.%08d => reward %d.%08d\n",
            p_rewardReq.tgtAssetName.c_str(), payment.address.c_str(),
            payment.payoutAmt / tgtUnitMultiplier, payment.payoutAmt % tgtUnitMultiplier,
            rewardAmt / srcUnitMultiplier, rewardAmt % srcUnitMultiplier);

        //  And save it into our list
        p_payoutEntry.payments.insert(payment);
    }

    //  Don't insert an empty payout record
    if (p_payoutEntry.payments.size() == 0) {
        LogPrintf("GeneratePayouts: No ownership payout records generated for for '%s'\n", p_rewardReq.tgtAssetName.c_str());
        return false;
    }

    //  Overwrite the existing payout entry, if one exists
    if (!Write(std::make_pair(PAYOUT_FLAG, p_payoutEntry.rewardID), p_payoutEntry)) {
        LogPrintf("GeneratePayouts: Failed to write payout entry for reward '%s'\n", p_rewardReq.rewardID.c_str());
        return false;
    }

    //  After saving our payout entry, remove the reward request
    if (!pRewardRequestDb->RemoveReward(p_rewardReq.rewardID)) {
        //  Not critical if this fails
        LogPrintf("GeneratePayouts: Failed to remove reward request for reward '%s'\n", p_rewardReq.rewardID.c_str());
    }

    //  Find out if any reward requests are still pending for this asset at the current height
    if (!pRewardRequestDb->AreRewardsScheduledForHeight(p_rewardReq.heightForPayout)) {
        //  Delete the asset snapshot
        if (!pAssetSnapshotDb->RemoveOwnershipSnapshot(p_rewardReq.tgtAssetName, p_rewardReq.heightForPayout)) {
            //  Not critical if this fails
            LogPrintf("GeneratePayouts: Failed to remove asset snapshot for asset '%s' at height %d\n",
                p_rewardReq.tgtAssetName.c_str(), p_rewardReq.heightForPayout);
        }
    }

    //  Always return true if teh payout was written successfully
    return true;
}

bool CPayoutDB::RetrievePayoutEntry(
    const std::string & p_rewardID,
    CPayoutDBEntry & p_payoutEntry)
{
    //  Load up the payout entries for the specified reward
    return Read(std::make_pair(PAYOUT_FLAG, p_rewardID), p_payoutEntry);
}

bool CPayoutDB::UpdatePayoutEntry(
    const CPayoutDBEntry & p_payoutEntry)
{
    //  If all payments have been completed, delete the payout
    bool allPaymentsCompleted = true;

    for (auto const & payment : p_payoutEntry.payments) {
        if (!payment.completed) {
            allPaymentsCompleted = false;
            break;
        }
    }

    if (allPaymentsCompleted) {
        //  All payments completed for the payout, so remove the payout
        return Erase(std::make_pair(PAYOUT_FLAG, p_payoutEntry.rewardID));
    }

    //  Otherwise, simply update the payout
    return Write(std::make_pair(PAYOUT_FLAG, p_payoutEntry.rewardID), p_payoutEntry);
}
