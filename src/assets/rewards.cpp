// Copyright (c) 2017-2020 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utilstrencodings.h>
#include <hash.h>
#include <validation.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <chainparams.h>
#include <univalue/include/univalue.h>
#include <core_io.h>
#include <net.h>
#include <base58.h>
#include <consensus/validation.h>
#include <wallet/coincontrol.h>
#include <utilmoneystr.h>
#include "assets/rewards.h"
#include "assetsnapshotdb.h"
#include "wallet/wallet.h"

std::map<uint256, CRewardSnapshot> mapRewardSnapshots;

uint256 CRewardSnapshot::GetHash() const
{
    return SerializeHash(*this, SER_GETHASH);
}

bool AddDistributeRewardSnapshot(CRewardSnapshot& p_rewardSnapshot)
{
    auto hash = p_rewardSnapshot.GetHash();
    CRewardSnapshot temp;
    if (pDistributeSnapshotDb->RetrieveDistributeSnapshotRequest(hash, temp)) {
        return false;
    }

    if (pDistributeSnapshotDb->AddDistributeSnapshot(hash, p_rewardSnapshot)) {
        mapRewardSnapshots[hash] = p_rewardSnapshot;
    }

    return true;
}

bool GenerateDistributionList(const CRewardSnapshot& p_rewardSnapshot, std::vector<OwnerAndAmount>& vecDistributionList)
{
    vecDistributionList.clear();

    if (passets == nullptr) {
        LogPrint(BCLog::REWARDS, "%s: Invalid assets cache!\n", __func__);
        return false;
    }
    if (pSnapshotRequestDb == nullptr) {
        LogPrint(BCLog::REWARDS, "%s: Invalid Snapshot Request cache!\n", __func__);
        return false;
    }
    if (pAssetSnapshotDb == nullptr) {
        LogPrint(BCLog::REWARDS, "%s: Invalid asset snapshot cache!\n", __func__);
        return false;
    }

    //  Get details on the specified source asset
    CNewAsset distributionAsset;
    UNUSED_VAR bool srcIsIndivisible = false;
    CAmount srcUnitDivisor = COIN;  //  Default to divisor for RVN
    const int8_t COIN_DIGITS_PAST_DECIMAL = 8;

    //  This value is in indivisible units of the source asset
    CAmount modifiedPaymentInAssetUnits = p_rewardSnapshot.nDistributionAmount;

    if (p_rewardSnapshot.strDistributionAsset != "RVN") {
        if (!passets->GetAssetMetaDataIfExists(p_rewardSnapshot.strDistributionAsset, distributionAsset)) {
            LogPrint(BCLog::REWARDS, "%s: Failed to retrieve asset details for '%s'\n", __func__, p_rewardSnapshot.strDistributionAsset.c_str());
            return false;
        }

        //  If the token is indivisible, signal this to later code with a zero divisor
        if (distributionAsset.units == 0) {
            srcIsIndivisible = true;
        }

        srcUnitDivisor = static_cast<CAmount>(pow(10, distributionAsset.units));

        CAmount srcDivisor = pow(10, COIN_DIGITS_PAST_DECIMAL - distributionAsset.units);
        modifiedPaymentInAssetUnits /= srcDivisor;

        LogPrint(BCLog::REWARDS, "%s: Distribution asset '%s' has units %d and divisor %d\n", __func__,
                 p_rewardSnapshot.strDistributionAsset.c_str(), distributionAsset.units, srcUnitDivisor);
    }
    else {
        LogPrint(BCLog::REWARDS, "%s: Distribution is RVN with divisor %d\n", __func__, srcUnitDivisor);
    }

    LogPrint(BCLog::REWARDS, "%s: Scaled payment amount in %s is %d\n", __func__,
             p_rewardSnapshot.strDistributionAsset.c_str(), modifiedPaymentInAssetUnits);

    //  Get details on the ownership asset
    CNewAsset ownershipAsset;
    CAmount tgtUnitDivisor = 0;
    if (!passets->GetAssetMetaDataIfExists(p_rewardSnapshot.strOwnershipAsset, ownershipAsset)) {
        LogPrint(BCLog::REWARDS, "%s: Failed to retrieve asset details for '%s'\n", __func__, p_rewardSnapshot.strOwnershipAsset.c_str());
        return false;
    }

    //  Save the ownership asset's divisor
    tgtUnitDivisor = static_cast<CAmount>(pow(10, COIN_DIGITS_PAST_DECIMAL - ownershipAsset.units));

    LogPrint(BCLog::REWARDS, "%s: Ownership asset '%s' has units %d and divisor %d\n", __func__,
             p_rewardSnapshot.strOwnershipAsset.c_str(), ownershipAsset.units, tgtUnitDivisor);

    //  Remove exception addresses & amounts from the list
    std::set<std::string> exceptionAddressSet;
    boost::split(exceptionAddressSet, p_rewardSnapshot.strExceptionAddresses, boost::is_any_of(ADDRESS_COMMA_DELIMITER));

    std::set<OwnerAndAmount> nonExceptionOwnerships;
    CAmount totalAmtOwned = 0;

    CAssetSnapshotDBEntry snapshotEntry;
    if (!pAssetSnapshotDb->RetrieveOwnershipSnapshot(p_rewardSnapshot.strOwnershipAsset, p_rewardSnapshot.nHeight, snapshotEntry)) {
        LogPrint(BCLog::REWARDS, "%s: Failed to retrieve ownership snapshot list!\n", __func__);
        return false;
    }

    for (auto const & currPair : snapshotEntry.ownersAndAmounts) {
        //  Ignore exception and burn addresses
        if (
                exceptionAddressSet.find(currPair.first) == exceptionAddressSet.end()
                && !GetParams().IsBurnAddress(currPair.first)
                ) {
            //  Address is valid so add it to the payment list
            nonExceptionOwnerships.insert(OwnerAndAmount(currPair.first, currPair.second));
            totalAmtOwned += currPair.second;
        }
    }

    //  Make sure we have some addresses to pay to
    if (nonExceptionOwnerships.size() == 0) {
        LogPrint(BCLog::REWARDS, "%s: Ownership of '%s' includes only exception/burn addresses.\n", __func__,
                 p_rewardSnapshot.strOwnershipAsset.c_str());
        return false;
    }

    LogPrint(BCLog::REWARDS, "%s: Total amount owned %d\n", __func__,
             totalAmtOwned);

    LogPrint(BCLog::REWARDS, "%s: Total payout amount %d\n", __func__,
             modifiedPaymentInAssetUnits);

    CAmount totalSentAsRewards = 0;
    //  Loop through asset owners
    for (auto & ownership : nonExceptionOwnerships) {
        // Get percentage of total ownership
        long double percent = (long double)ownership.amount / (long double)totalAmtOwned;
        // Caculate the reward with potentional unit inaccurancies e.g with units 4, 90054100 satoshis = 0.90054100
        CAmount rewardAmt = percent * modifiedPaymentInAssetUnits * static_cast<CAmount>(pow(10, COIN_DIGITS_PAST_DECIMAL - distributionAsset.units));
        // Remove all none accurate units e.g with units 4 90054100 => 9005
        rewardAmt /= static_cast<CAmount>(pow(10, COIN_DIGITS_PAST_DECIMAL - distributionAsset.units));
        // Replace all none accurate units back with zeros e.g with units 4 9005 => 90050000 satoshis = 0.90050000
        rewardAmt *= static_cast<CAmount>(pow(10, COIN_DIGITS_PAST_DECIMAL - distributionAsset.units));

        totalSentAsRewards += rewardAmt;

        LogPrint(BCLog::REWARDS, "%s: Found ownership address for '%s': '%s' owns %d => reward %d\n", __func__,
                 p_rewardSnapshot.strOwnershipAsset.c_str(), ownership.address.c_str(),
                 ownership.amount, rewardAmt);

        //  Save it into our list if the reward payment is above zero
        if (rewardAmt > 0)
            vecDistributionList.push_back(OwnerAndAmount(ownership.address, rewardAmt));
    }

    CAmount change = totalAmtOwned - totalSentAsRewards;
    if (change > 0) {
        LogPrint(BCLog::REWARDS, "%s: Found change amount of %u\n", __func__, change);
    }

    return true;
}

#ifdef ENABLE_WALLET

void DistributeRewardSnapshot(CWallet * p_wallet, const CRewardSnapshot& p_rewardSnapshot)
{
    if (p_wallet->IsLocked()) {
        LogPrint(BCLog::REWARDS, "Skipping distribution: Wallet is locked!\n");
        return;
    }

    if (IsInitialBlockDownload()) {
        LogPrint(BCLog::REWARDS, "Skipping distribution: Syncing Chain!\n");
        return;
    }

    //  Retrieve the asset snapshot entry for the target asset at the specified height
    CAssetSnapshotDBEntry snapshotEntry;

    if (!pAssetSnapshotDb->RetrieveOwnershipSnapshot(p_rewardSnapshot.strOwnershipAsset, p_rewardSnapshot.nHeight, snapshotEntry)) {
        LogPrint(BCLog::REWARDS, "Failed to retrieve ownership snapshot!\n");
        return;
    }

    //  Generate payment transactions and store in the payments DB
    std::vector<OwnerAndAmount> paymentDetails;
    if (!GenerateDistributionList(p_rewardSnapshot, paymentDetails)) {
        LogPrint(BCLog::REWARDS, "Failed to generate payment details!\n");
        return;
    }

    int nNumberOfTransactions = ((int)paymentDetails.size() / MAX_PAYMENTS_PER_TRANSACTION) + 1;
    CRewardSnapshot copyRewardSnapshot = p_rewardSnapshot;
    for (int i = 0; i < nNumberOfTransactions; i++) {
        uint256 txid;
        if (pDistributeSnapshotDb->GetDistributeTransaction(p_rewardSnapshot.GetHash(), i, txid)) {
            CTransactionRef txRef;
            uint256 hashBlock;
            auto walletTx = p_wallet->GetWalletTx(txid);
            if (walletTx) {
                int depth = walletTx->GetDepthInMainChain();
                if (depth < 0) {
                    LogPrint(BCLog::REWARDS, "Failed distribution: Tx conflict with another tx: %s: number of block back %d!\n", txid.GetHex(), depth);
                    return;
                } else if (depth == 0) {
                    LogPrint(BCLog::REWARDS, "Tx is in the mempool! %s\n", txid.GetHex());
                    return;
                } else if (depth > 0) {
                    LogPrint(BCLog::REWARDS, "Tx is in a block %s!\n", txid.GetHex());
                    continue;
                }
            } else {
                LogPrint(BCLog::REWARDS, "Failed to get wallet Tx: %s\n", txid.GetHex());
            }
        } else {
            LogPrint(BCLog::REWARDS, "Didn't find transaction in database creating new transaction: %s %s %d %d\n", p_rewardSnapshot.strOwnershipAsset, p_rewardSnapshot.strDistributionAsset, p_rewardSnapshot.nDistributionAmount, i);
            // Create a new transaction and database it
            int start = i * MAX_PAYMENTS_PER_TRANSACTION;
            uint256 retTxid;
            std::string change = "";
            if (!BuildTransaction(p_wallet, p_rewardSnapshot, paymentDetails, start, change, retTxid)) {
                LogPrint(BCLog::REWARDS, "Failed to build Tx: distribute: %s, amount: %d\n", p_rewardSnapshot.strDistributionAsset, p_rewardSnapshot.nDistributionAmount);
                return;
            }
            pDistributeSnapshotDb->AddDistributeTransaction(p_rewardSnapshot.GetHash(), i, retTxid);
        }
    }
}

bool BuildTransaction(
        CWallet * const p_walletPtr, const CRewardSnapshot& p_rewardSnapshot,
        const std::vector<OwnerAndAmount> & p_pendingPayments,const int& start,
        std::string& change_address, uint256& retTxid)
{
    int expectedCount = 0;
    int actualCount = 0;
    CValidationState state;

    int stop = start + MAX_PAYMENTS_PER_TRANSACTION;
    CRewardSnapshot copyRewardSnapshot = p_rewardSnapshot;
    auto rewardSnapshotHash = p_rewardSnapshot.GetHash();

    LogPrint(BCLog::REWARDS, "Generating transactions for payments...\n");

    //  Transfer the specified amount of the asset from the source to the target
    CCoinControl ctrl;
    ctrl.destChange = DecodeDestination(change_address);
    ctrl.assetDestChange = DecodeDestination(change_address);
    std::shared_ptr<CWalletTx> txnPtr = std::make_shared<CWalletTx>();
    std::shared_ptr<CReserveKey> reserveKeyPtr = std::make_shared<CReserveKey>(p_walletPtr);
    std::shared_ptr<UniValue> batchResult = std::make_shared<UniValue>(UniValue::VOBJ);
    CAmount nFeeRequired = 0;
    CAmount totalPaymentAmt = 0;


    //  Handle payouts using RVN differently from those using an asset
    if (p_rewardSnapshot.strDistributionAsset == "RVN") {
        // Check amount
        CAmount curBalance = p_walletPtr->GetBalance();

        if (p_walletPtr->GetBroadcastTransactions() && !g_connman) {
            mapRewardSnapshots[rewardSnapshotHash].nStatus = CRewardSnapshot::NETWORK_ERROR;
            pDistributeSnapshotDb->OverrideDistributeSnapshot(rewardSnapshotHash, mapRewardSnapshots.at(rewardSnapshotHash));
            LogPrint(BCLog::REWARDS, "Error: Peer-to-peer functionality missing or disabled\n");
            return false;
        }

        std::vector<CRecipient> vDestinations;

        //  This should (due to external logic) only include pending payments
        for (int i = start; i < (int)p_pendingPayments.size() && i < stop; i++) {
            expectedCount++;

            // Parse Raven address (already validated during ownership snapshot creation)
            CTxDestination dest = DecodeDestination(p_pendingPayments[i].address);
            CScript scriptPubKey = GetScriptForDestination(dest);
            CRecipient recipient = {scriptPubKey, p_pendingPayments[i].amount, false};
            vDestinations.emplace_back(recipient);

            totalPaymentAmt += p_pendingPayments[i].amount;
            actualCount++;
        }

        //  Verify funds
        if (totalPaymentAmt > curBalance) {
            mapRewardSnapshots[rewardSnapshotHash].nStatus = CRewardSnapshot::LOW_FUNDS;
            pDistributeSnapshotDb->OverrideDistributeSnapshot(rewardSnapshotHash, mapRewardSnapshots[rewardSnapshotHash]);
            LogPrint(BCLog::REWARDS, "Insufficient funds: total payment %lld > available balance %lld\n",
                     totalPaymentAmt, curBalance);
            return false;
        }

        // Create and send the transaction
        std::string strError;
        int nChangePosRet = -1;

        if (!p_walletPtr->CreateTransaction(vDestinations, *txnPtr.get(), *reserveKeyPtr.get(), nFeeRequired, nChangePosRet, strError, ctrl)) {
            if (totalPaymentAmt + nFeeRequired > curBalance) {
                mapRewardSnapshots[rewardSnapshotHash].nStatus = CRewardSnapshot::NOT_ENOUGH_FEE;
                pDistributeSnapshotDb->OverrideDistributeSnapshot(rewardSnapshotHash, mapRewardSnapshots.at(rewardSnapshotHash));
                strError = strprintf("Error: This transaction requires a transaction fee of at least %s",
                                     FormatMoney(nFeeRequired));
            } else {
                mapRewardSnapshots[rewardSnapshotHash].nStatus = CRewardSnapshot::FAILED_CREATE_TRANSACTION;
                pDistributeSnapshotDb->OverrideDistributeSnapshot(rewardSnapshotHash, mapRewardSnapshots.at(rewardSnapshotHash));
            }

            LogPrint(BCLog::REWARDS, "%s\n", strError.c_str());
            return false;
        }

        if (!p_walletPtr->CommitTransaction(*txnPtr.get(), *reserveKeyPtr.get(), g_connman.get(), state)) {
            mapRewardSnapshots[rewardSnapshotHash].nStatus = CRewardSnapshot::FAILED_COMMIT_TRANSACTION;
            pDistributeSnapshotDb->OverrideDistributeSnapshot(rewardSnapshotHash, mapRewardSnapshots.at(rewardSnapshotHash));
            LogPrint(BCLog::REWARDS, "%s\n", state.GetRejectReason());
            return false;
        }
    }
    else {
        std::pair<int, std::string> error;
        std::vector< std::pair<CAssetTransfer, std::string> > vDestinations;
        CAmount nTotalAssetAmount = 0;

        // Get the total amount of distribution assets this wallet has
        CAmount totalAssetBalance = 0;
        GetMyAssetBalance(p_rewardSnapshot.strDistributionAsset, totalAssetBalance, 0);

        //  This should (due to external logic) only include pending payments
        for (int i = start; i < (int)p_pendingPayments.size() && i < stop; i++) {
            expectedCount++;

            vDestinations.emplace_back(std::make_pair(
                    CAssetTransfer(p_rewardSnapshot.strDistributionAsset, p_pendingPayments[i].amount, DecodeAssetData(""), 0), p_pendingPayments[i].address));

            nTotalAssetAmount += p_pendingPayments[i].amount;
            actualCount++;
        }

        if (nTotalAssetAmount > totalAssetBalance) {
            mapRewardSnapshots[rewardSnapshotHash].nStatus = CRewardSnapshot::LOW_REWARDS;
            pDistributeSnapshotDb->OverrideDistributeSnapshot(rewardSnapshotHash, mapRewardSnapshots[rewardSnapshotHash]);
            LogPrint(BCLog::REWARDS, "Insufficient asset funds: total payment %lld > available balance %lld\n",
                     nTotalAssetAmount, totalAssetBalance);
            return false;
        }

        // Create the Transaction (this also verifies dest address)
        if (!CreateTransferAssetTransaction(p_walletPtr, ctrl, vDestinations, "", error, *txnPtr.get(), *reserveKeyPtr.get(), nFeeRequired)) {
            mapRewardSnapshots[rewardSnapshotHash].nStatus = CRewardSnapshot::FAILED_CREATE_TRANSACTION;
            pDistributeSnapshotDb->OverrideDistributeSnapshot(rewardSnapshotHash, mapRewardSnapshots.at(rewardSnapshotHash));
            LogPrint(BCLog::REWARDS, "Failed to create transfer asset transaction: %s\n", error.second.c_str());
            return false;
        }

        if (!p_walletPtr->CommitTransaction(*txnPtr.get(), *reserveKeyPtr.get(), g_connman.get(), state)) {
            mapRewardSnapshots[rewardSnapshotHash].nStatus = CRewardSnapshot::FAILED_COMMIT_TRANSACTION;
            pDistributeSnapshotDb->OverrideDistributeSnapshot(rewardSnapshotHash, mapRewardSnapshots.at(rewardSnapshotHash));
            LogPrint(BCLog::REWARDS, "%s\n", state.GetRejectReason());
            return false;
        }
    }

    retTxid = txnPtr->GetHash();
    LogPrint(BCLog::REWARDS, "Transaction generation succeeded : %s\n", retTxid.GetHex());

    return true;
}

void CheckRewardDistributions(CWallet * p_wallet)
{
    for (auto item : mapRewardSnapshots) {
        DistributeRewardSnapshot(p_wallet, item.second);
    }
}

#endif //ENABLE_WALLET



