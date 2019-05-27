// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include "assets/assetdb.h"
#include "assets/messages.h"
#include <map>
#include "tinyformat.h"

#include <boost/algorithm/string.hpp>

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "httpserver.h"
#include "validation.h"
#include "net.h"
#include "policy/feerate.h"
#include "policy/fees.h"
#include "policy/policy.h"
#include "policy/rbf.h"
#include "rpc/mining.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet/coincontrol.h"
#include "wallet/feebumper.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "assets/snapshotrequestdb.h"
#include "assets/assetsnapshotdb.h"

//  Addresses are delimited by commas
static const std::string ADDRESS_COMMA_DELIMITER = ",";

//  Individual payment record
struct OwnerAndAmount
{
    std::string address;
    CAmount amount;

    OwnerAndAmount(
        const std::string & p_address,
        CAmount p_rewardAmt
    )
    {
        address = p_address;
        amount = p_rewardAmt;
    }

    bool operator<(const OwnerAndAmount &rhs) const
    {
        return address < rhs.address;
    }
};

//  Generate payment details
bool GeneratePayments(
    const std::string & p_ownershipAsset,
    const std::string & p_distributionAsset,
    const std::string & p_exceptionAddresses,
    CAmount p_totalPayoutValue,
    const CAssetSnapshotDBEntry & p_assetSnapshot,
    std::set<OwnerAndAmount> & p_paymentDetails);

//  Collect information about transactions that are pending commit
struct PendingTransaction
{
    std::string id;
    std::shared_ptr<CWalletTx> ptr;
    std::shared_ptr<CReserveKey> reserveKey;
    std::shared_ptr<UniValue> result;
    CAmount fee;
    CAmount totalAmt;
    std::vector<OwnerAndAmount> payments;

    PendingTransaction(
        const std::string & p_txnID,
        std::shared_ptr<CWalletTx> p_txnPtr,
        std::shared_ptr<CReserveKey> p_reserveKey,
        std::shared_ptr<UniValue> p_result,
        CAmount p_txnFee,
        CAmount p_txnAmount,
        std::vector<OwnerAndAmount> & p_payments
    )
    {
        id = p_txnID;
        ptr = p_txnPtr;
        reserveKey = p_reserveKey;
        result = p_result;
        fee = p_txnFee;
        totalAmt = p_txnAmount;
        payments = std::move(p_payments);
    }
};

//  Transfer the specified amount of the asset from the source to the target
bool GenerateTransaction(
    CWallet * const p_walletPtr, const std::string & p_src,
    std::vector<OwnerAndAmount> & p_payments,
    std::vector<PendingTransaction> & p_pendingTxns);

UniValue requestsnapshot(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
                "requestsnapshot \"asset_name\" block_height\n"
                "\nSchedules a snapshot of the specified asset at the specified block height.\n"

                "\nArguments:\n"
                "asset_name: (string, required) The asset name for which the snapshot will be taken\n"
                "block_height: (number, required) The block height at which the snapshot will be take\n"

                "\nResult:\n"
                "{\n"
                "  request_status: \"Added\",\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("requestsnapshot", "\"TRONCO\" 12345")
                + HelpExampleRpc("requestsnapshot", "\"PHATSTACKS\" 34987")
        );

    if (!fAssetIndex) {
        return "_This rpc call is not functional unless -assetindex is enabled. To enable, please run the wallet with -assetindex, this will require a reindex to occur";
    }

    //  Extract parameters
    std::string asset_name = request.params[0].get_str();
    int block_height = request.params[1].get_int();

    AssetType ownershipAssetType;

    if (!IsAssetNameValid(asset_name, ownershipAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: Please use a valid asset name"));

    if (ownershipAssetType == AssetType::UNIQUE || ownershipAssetType == AssetType::OWNER || ownershipAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    if (block_height <= chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid block_height: block height should be greater than current active chain height"));
    }

    if (!pSnapshotRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Snapshot Request database is not setup. Please restart wallet to try again"));

    //  Build our snapshot request record for scheduling
    if (pSnapshotRequestDb->ScheduleSnapshot(asset_name, block_height)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("request_status", "Added"));

        return obj;
    }

    throw JSONRPCError(RPC_MISC_ERROR, std::string("Failed to add requested snapshot to database"));
}

UniValue getsnapshotrequest(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
                "getsnapshotrequest \"asset_name\" block_height\n"
                "\nRetrieves the specified snapshot request details.\n"

                "\nArguments:\n"
                "asset_name: (string, required) The asset name for which the snapshot will be taken\n"
                "block_height: (number, required) The block height at which the snapshot will be take\n"

                "\nResult:\n"
                "{\n"
                "  asset_name: (string),\n"
                "  block_height: (number),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("getsnapshotrequest", "\"TRONCO\" 12345")
                + HelpExampleRpc("getsnapshotrequest", "\"PHATSTACKS\" 34987")
        );

    if (!fAssetIndex) {
        return "_This rpc call is not functional unless -assetindex is enabled. To enable, please run the wallet with -assetindex, this will require a reindex to occur";
    }

    //  Extract parameters
    std::string asset_name = request.params[0].get_str();
    int block_height = request.params[1].get_int();

    if (!pSnapshotRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Snapshot Request database is not setup. Please restart wallet to try again"));

    //  Retrieve the specified snapshot request
    CSnapshotRequestDBEntry snapshotRequest;

    if (pSnapshotRequestDb->RetrieveSnapshotRequest(asset_name, block_height, snapshotRequest)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("asset_name", snapshotRequest.assetName));
        obj.push_back(Pair("block_height", snapshotRequest.heightForSnapshot));

        return obj;
    }
    else {
       LogPrint(BCLog::REWARDS, "Failed to retrieve specified snapshot request for asset '%s' at height %d!\n",
            asset_name.c_str(), block_height);
    }

    throw JSONRPCError(RPC_MISC_ERROR, std::string("Failed to retrieve specified snapshot request"));
}

UniValue cancelsnapshotrequest(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
                "cancelsnapshotrequest \"asset_name\" block_height\n"
                "\nCancels the specified snapshot request.\n"

                "\nArguments:\n"
                "asset_name: (string, required) The asset name for which the snapshot will be taken\n"
                "block_height: (number, required) The block height at which the snapshot will be take\n"

                "\nResult:\n"
                "{\n"
                "  request_status: (string),\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("cancelsnapshotrequest", "\"TRONCO\" 12345")
                + HelpExampleRpc("cancelsnapshotrequest", "\"PHATSTACKS\" 34987")
        );

    if (!fAssetIndex) {
        return "_This rpc call is not functional unless -assetindex is enabled. To enable, please run the wallet with -assetindex, this will require a reindex to occur";
    }

    //  Extract parameters
    std::string asset_name = request.params[0].get_str();
    int block_height = request.params[1].get_int();

    if (!pSnapshotRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Snapshot Request database is not setup. Please restart wallet to try again"));

    //  Retrieve the specified reward
    if (pSnapshotRequestDb->RemoveSnapshotRequest(asset_name, block_height)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("request_status", "Removed"));

        return obj;
    }
    else {
        LogPrint(BCLog::REWARDS, "Failed to cancel specified snapshot request for asset '%s' at height %d!\n",
            asset_name.c_str(), block_height);
    }

    throw JSONRPCError(RPC_MISC_ERROR, std::string("Failed to remove specified snapshot request"));
}

UniValue distributereward(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 4)
        throw std::runtime_error(
                "distributereward \"asset_name\" snapshot_height \"distribution_asset_name\" gross_distribution_amount ( \"exception_addresses\" )\n"
                "\nSplits the specified amount of the distribution asset to all owners of asset_name that are not in the optional exclusion_addresses\n"

                "\nArguments:\n"
                "asset_name: (string, required) The reward will be distributed all owners of this asset\n"
                "snapshot_height: (number, required) The block height of the ownership snapshot\n"
                "distribution_asset_name: (string, required) The name of the asset that will be distributed, or RVN\n"
                "gross_distribution_amount: (number, required) The amount of the distribution asset that will be split amongst all owners\n"
                "exception_addresses: (string, optional) Ownership addresses that should be excluded\n"

                "\nResult:\n"
                "{\n"
                "  error_txn_gen_failed: (string),\n"
                "  error_nsf: (string),\n"
                "  error_rejects: (string),\n"
                "  error_db_update: (string),\n"
                "  batch_results: [\n"
                "    {\n"
                "      transaction_id: (string),\n"
                "      error_txn_rejected: (string),\n"
                "      total_amount: (number),\n"
                "      fee: (number),\n"
                "      expected_count: (number),\n"
                "      actual_count: (number),\n"
                "    }\n"
                "  ]\n"
                "}\n"

                "\nExamples:\n"
                + HelpExampleCli("requestsnapshot", "\"TRONCO\" 12345 \"RVN\" 1000")
                + HelpExampleCli("requestsnapshot", "\"PHATSTACKS\" 12345 \"DIVIDENDS\" 1000 \"mwN7xC3yomYdvJuVXkVC7ymY9wNBjWNduD,n4Rf18edydDaRBh7t6gHUbuByLbWEoWUTg\"")
                + HelpExampleRpc("requestsnapshot", "\"TRONCO\" 34987 \"DIVIDENDS\" 100000")
                + HelpExampleRpc("requestsnapshot", "\"PHATSTACKS\" 34987 \"RVN\" 100000 \"mwN7xC3yomYdvJuVXkVC7ymY9wNBjWNduD,n4Rf18edydDaRBh7t6gHUbuByLbWEoWUTg\"")
        );

    if (!fAssetIndex) {
        return "_This rpc call is not functional unless -assetindex is enabled. To enable, please run the wallet with -assetindex, this will require a reindex to occur";
    }

    //  Figure out which wallet to use
    CWallet * const walletPtr = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(walletPtr, request.fHelp)) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system requires a wallet.");
        return ret;
    }

    ObserveSafeMode();
    LOCK2(cs_main, walletPtr->cs_wallet);

    EnsureWalletIsUnlocked(walletPtr);

    //  Extract parameters
    std::string asset_name(request.params[0].get_str());
    int snapshot_height = request.params[1].get_int();
    std::string distribution_asset_name(request.params[2].get_str());
    CAmount distribution_amount = AmountFromValue(request.params[3], (distribution_asset_name == "RVN"));
    std::string exception_addresses;
    if (request.params.size() > 4) {
        exception_addresses = request.params[4].get_str();

        //LogPrint(BCLog::REWARDS, "Excluding \"%s\"\n", exception_addresses.c_str());
    }

    AssetType ownershipAssetType;
    AssetType distributionAssetType;

    if (!IsAssetNameValid(asset_name, ownershipAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: Please use a valid asset name"));

    if (ownershipAssetType == AssetType::UNIQUE || ownershipAssetType == AssetType::OWNER || ownershipAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    if (snapshot_height > chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid snapshot_height: block height should be less than or equal to the current active chain height"));
    }

    if (distribution_asset_name != "RVN") {
        if (!IsAssetNameValid(distribution_asset_name, distributionAssetType))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid distribution_asset_name: Please use a valid asset name"));

        if (distributionAssetType == AssetType::UNIQUE || distributionAssetType == AssetType::OWNER || distributionAssetType == AssetType::MSGCHANNEL)
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid distribution_asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));
    }


    if (!passetsdb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Assets database is not setup. Please restart wallet to try again"));
    if (!pAssetSnapshotDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Asset Snapshot database is not setup. Please restart wallet to try again"));

    //  Retrieve the asset snapshot entry for the target asset at the specified height
    CAssetSnapshotDBEntry snapshotEntry;

    if (pAssetSnapshotDb->RetrieveOwnershipSnapshot(asset_name, snapshot_height, snapshotEntry)) {
        //  Generate payment transactions and store in the payments DB
        std::set<OwnerAndAmount> paymentDetails;

        if (!GeneratePayments(
            asset_name, distribution_asset_name, exception_addresses,
            distribution_amount, snapshotEntry, paymentDetails)
        ) {
            LogPrint(BCLog::REWARDS, "Failed to generate payment details!\n");
        }
        else {
            //  We now have a list of payments to make, so make them.
            UniValue responseObj(UniValue::VOBJ);

            //
            //  Loop through the payout addresses and process them in batches
            //
            const int MAX_PAYMENTS_PER_TRANSACTION = 200;
            std::vector<OwnerAndAmount> paymentVector;
            std::vector<PendingTransaction> pendingTxns;
            bool someGenerationsFailed = false;

            for (auto & payment : paymentDetails) {
                paymentVector.push_back(payment);

                //  Issue a transaction if we've hit the max payment count
                if (paymentVector.size() >= MAX_PAYMENTS_PER_TRANSACTION) {
                    //  Build a transaction for the current batch
                    //  If this succeeds, the payment vector elements will have been moved into
                    //      a new PendingTransaction element in the pendingTxns vector.
                    if (!GenerateTransaction(walletPtr, distribution_asset_name, paymentVector, pendingTxns)) {
                        //  If anything fails, we abort before committing transactions
                        LogPrint(BCLog::REWARDS, "Transaction generation failed for '%s' using source '%s'!\n",
                            asset_name.c_str(), distribution_asset_name.c_str());

                        //  Explicitly clear teh vector, in case it wasn't done inside GenerateTransaction
                        paymentVector.clear();

                        someGenerationsFailed = true;
                    }
                }
            }

            //
            //  If any payments are left in the last batch, send them
            //
            if (paymentVector.size() > 0) {
                //  Build a transaction for the current batch
                if (!GenerateTransaction(walletPtr, distribution_asset_name, paymentVector, pendingTxns)) {
                    LogPrint(BCLog::REWARDS, "Transaction generation failed for '%s' using source '%s'!\n",
                        asset_name.c_str(), distribution_asset_name.c_str());

                    //  Explicitly clear teh vector, in case it wasn't done inside GenerateTransaction
                    paymentVector.clear();

                    someGenerationsFailed = true;
                }
            }

            if (someGenerationsFailed) {
                throw JSONRPCError(RPC_MISC_ERROR, std::string("Failed to generate some transactions"));
            }

            //  If we haven't generated any transactions, bail out.
            if (pendingTxns.size() == 0) {
                LogPrint(BCLog::REWARDS, "Failed to generate any transactions for asset '%s'!\n",
                    asset_name.c_str());

                throw JSONRPCError(RPC_MISC_ERROR, std::string("Failed to distribute reward"));
            }

            //  Walk through the entire set of transactions to find out if the fees can be covered
            CAmount totalFees = 0;
            CAmount totalAmount = 0;
            for (auto const & pendingTxn : pendingTxns) {
                totalFees += pendingTxn.fee;
                totalAmount += pendingTxn.totalAmt;
            }

            CAmount curBalance = walletPtr->GetBalance();
            if (curBalance < totalAmount + totalFees) {
                //  Not Sufficient Funds
                std::string strError = strprintf("Insufficient funds (%s) to cover reward (%s) as well as fees (%s)!",
                    FormatMoney(curBalance), FormatMoney(totalAmount), FormatMoney(totalFees));
                LogPrint(BCLog::REWARDS, "Error: %s\n", strError.c_str());

                responseObj.push_back(Pair("error_nsf", strError));
            }
            else {
                //
                //  Sufficient Funds... Proceed with transaction commits
                //
                bool errorsOccurred = false;
                UniValue batchResults(UniValue::VARR);

                //  Attempt to commit all generated transactions
                for (auto & pendingTxn : pendingTxns) {
                    CValidationState state;

                    if (!walletPtr->CommitTransaction(*pendingTxn.ptr.get(), *pendingTxn.reserveKey.get(), g_connman.get(), state)) {
                        LogPrint(BCLog::REWARDS, "Error: The transaction was rejected! Reason given: %s\n", state.GetRejectReason());

                        pendingTxn.result->push_back(Pair("error_txn_rejected", state.GetRejectReason()));

                        errorsOccurred = true;
                    }
                    else {
                        //  Remove all successfully processed payments
                        pendingTxn.payments.clear();
                    }

                    batchResults.push_back(*pendingTxn.result.get());

                    pendingTxn.payments.clear();
                }

                if (errorsOccurred) {
                    responseObj.push_back(Pair("error_rejects", "One or more transactions were rejected"));
                }

                responseObj.push_back(Pair("batch_results", batchResults));
            }

            return responseObj;
        }
    }
    else {
        LogPrint(BCLog::REWARDS, "Failed to retrieve ownership snapshot for '%s' at height %d!\n",
            asset_name.c_str(), snapshot_height);                
    }

    throw JSONRPCError(RPC_MISC_ERROR, std::string("Failed to distribute reward"));
}

bool GeneratePayments(
    const std::string & p_ownershipAsset,
    const std::string & p_distributionAsset,
    const std::string & p_exceptionAddresses,
    CAmount p_totalPayoutAmount,
    const CAssetSnapshotDBEntry & p_assetSnapshot,
    std::set<OwnerAndAmount> & p_paymentDetails)
{
    p_paymentDetails.clear();

    if (passets == nullptr) {
        LogPrint(BCLog::REWARDS, "GeneratePayments: Invalid assets cache!\n");
        return false;
    }
    if (pSnapshotRequestDb == nullptr) {
        LogPrint(BCLog::REWARDS, "GeneratePayments: Invalid Snapshot Request cache!\n");
        return false;
    }
    if (pAssetSnapshotDb == nullptr) {
        LogPrint(BCLog::REWARDS, "GeneratePayments: Invalid asset snapshot cache!\n");
        return false;
    }

    //  Get details on the specified source asset
    CNewAsset distributionAsset;
    bool srcIsIndivisible = false;
    CAmount srcUnitDivisor = COIN;  //  Default to divisor for RVN
    const int8_t COIN_DIGITS_PAST_DECIMAL = 8;

    //  This value is in indivisible units of the source asset
    CAmount modifiedPaymentInAssetUnits = p_totalPayoutAmount;

    if (p_distributionAsset != "RVN") {
        if (!passets->GetAssetMetaDataIfExists(p_distributionAsset, distributionAsset)) {
            LogPrint(BCLog::REWARDS, "GeneratePayments: Failed to retrieve asset details for '%s'\n", p_distributionAsset.c_str());
            return false;
        }

        //  If the token is indivisible, signal this to later code with a zero divisor
        if (distributionAsset.units == 0) {
            srcIsIndivisible = true;
        }

        srcUnitDivisor = static_cast<CAmount>(pow(10, distributionAsset.units));

        CAmount srcDivisor = pow(10, COIN_DIGITS_PAST_DECIMAL - distributionAsset.units);
        modifiedPaymentInAssetUnits /= srcDivisor;

        LogPrint(BCLog::REWARDS, "GeneratePayments: Distribution asset '%s' has units %d and divisor %d\n",
            p_distributionAsset.c_str(), distributionAsset.units, srcUnitDivisor);
    }
    else {
        LogPrint(BCLog::REWARDS, "GeneratePayments: Distribution is RVN with divisor %d\n", srcUnitDivisor);
    }

    LogPrint(BCLog::REWARDS, "GeneratePayments: Scaled payment amount in %s is %d\n",
        p_distributionAsset.c_str(), modifiedPaymentInAssetUnits);

    //  Get details on the ownership asset
    CNewAsset ownershipAsset;
    CAmount tgtUnitDivisor = 0;
    if (!passets->GetAssetMetaDataIfExists(p_ownershipAsset, ownershipAsset)) {
        LogPrint(BCLog::REWARDS, "GeneratePayments: Failed to retrieve asset details for '%s'\n", p_ownershipAsset.c_str());
        return false;
    }

    //  Save the ownership asset's divisor
    tgtUnitDivisor = static_cast<CAmount>(pow(10, COIN_DIGITS_PAST_DECIMAL - ownershipAsset.units));

    LogPrint(BCLog::REWARDS, "GeneratePayments: Ownership asset '%s' has units %d and divisor %d\n",
        p_ownershipAsset.c_str(), ownershipAsset.units, tgtUnitDivisor);

    //  Remove exception addresses & amounts from the list
    std::set<std::string> exceptionAddressSet;
    boost::split(exceptionAddressSet, p_exceptionAddresses, boost::is_any_of(ADDRESS_COMMA_DELIMITER));

    std::set<OwnerAndAmount> nonExceptionOwnerships;
    CAmount totalAmtOwned = 0;

    for (auto const & currPair : p_assetSnapshot.ownersAndAmounts) {
        //  Ignore exception and burn addresses
        if (
            exceptionAddressSet.find(currPair.first) == exceptionAddressSet.end()
            && !GetParams().IsBurnAddress(currPair.first)
        ) {
            //  Address is valid so add it to the payment list
            nonExceptionOwnerships.insert(OwnerAndAmount(currPair.first, currPair.second / tgtUnitDivisor));
            totalAmtOwned += currPair.second / tgtUnitDivisor;
        }
    }

    //  Make sure we have some addresses to pay to
    if (nonExceptionOwnerships.size() == 0) {
        LogPrint(BCLog::REWARDS, "GeneratePayments: Ownership of '%s' includes only exception/burn addresses.\n",
            p_ownershipAsset.c_str());
        return false;
    }

    LogPrint(BCLog::REWARDS, "GeneratePayments: Total amount owned %d\n",
        totalAmtOwned);

    LogPrint(BCLog::REWARDS, "GeneratePayments: Total payout amount %d\n",
        modifiedPaymentInAssetUnits);

    //  If the asset is indivisible, and there are fewer rewards than individual ownerships,
    //      fail the distribution, since it is not possible to reward all ownerships equally.
    if (srcIsIndivisible
        && (
            modifiedPaymentInAssetUnits < totalAmtOwned   //  Fail, not enough
            || modifiedPaymentInAssetUnits % totalAmtOwned != 0   //  Fail, can't distribute evenly
        )
    ) {
        LogPrint(BCLog::REWARDS, "GeneratePayments: Distribution asset '%s' is indivisible, and not enough reward value was provided for all ownership of '%s'\n",
            p_distributionAsset.c_str(), p_ownershipAsset.c_str());
        return false;
    }

    //  Loop through asset owners
    for (auto & ownership : nonExceptionOwnerships) {
        //  Calculate the reward amount
        //
        //  NOTE - This has to assume that the payment amount in asset units is *AT LEAST*
        //      as large as the total amount of the asset owned, so that the division doesn't
        //      end up with fractions.
        //      To this end, the payment is divided by the total amount owned
        //          *BEFORE* multiplying by the current owner's owned amount.
        CAmount rewardAmt = (modifiedPaymentInAssetUnits / totalAmtOwned) * ownership.amount;

        LogPrint(BCLog::REWARDS, "GeneratePayments: Found ownership address for '%s': '%s' owns %d => reward %d\n",
            p_ownershipAsset.c_str(), ownership.address.c_str(),
            ownership.amount, rewardAmt);

        //  And save it into our list
        p_paymentDetails.insert(OwnerAndAmount(ownership.address, rewardAmt));
    }

    return true;
}

bool GenerateTransaction(
    CWallet * const p_walletPtr, const std::string & p_src,
    std::vector<OwnerAndAmount> & p_pendingPayments,
    std::vector<PendingTransaction> & p_pendingTxns)
{
    bool fcnRetVal = false;
    size_t expectedCount = 0;
    size_t actualCount = 0;

    LogPrint(BCLog::REWARDS, "Generating transactions for payments...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Transfer the specified amount of the asset from the source to the target
        CCoinControl ctrl;
        std::shared_ptr<CWalletTx> txnPtr = std::make_shared<CWalletTx>();
        std::shared_ptr<CReserveKey> reserveKeyPtr = std::make_shared<CReserveKey>(p_walletPtr);
        std::shared_ptr<UniValue> batchResult = std::make_shared<UniValue>(UniValue::VOBJ);
        CAmount nFeeRequired = 0;
        CAmount totalPaymentAmt = 0;

        //  Handle payouts using RVN differently from those using an asset
        if (p_src == "RVN") {
            // Check amount
            CAmount curBalance = p_walletPtr->GetBalance();

            if (p_walletPtr->GetBroadcastTransactions() && !g_connman) {
                LogPrint(BCLog::REWARDS, "Error: Peer-to-peer functionality missing or disabled\n");
                break;
            }

            std::vector<CRecipient> vDestinations;

            //  This should (due to external logic) only include pending payments
            for (auto & payment : p_pendingPayments) {
                expectedCount++;

                // Parse Raven address (already validated during ownership snapshot creation)
                CTxDestination dest = DecodeDestination(payment.address);
                CScript scriptPubKey = GetScriptForDestination(dest);
                CRecipient recipient = {scriptPubKey, payment.amount, false};
                vDestinations.emplace_back(recipient);

                totalPaymentAmt += payment.amount;
                actualCount++;
            }

            //  Verify funds
            if (totalPaymentAmt > curBalance) {
                LogPrint(BCLog::REWARDS, "Insufficient funds: total payment %lld > available balance %lld\n",
                    totalPaymentAmt, curBalance);
                break;
            }

            // Create and send the transaction
            std::string strError;
            int nChangePosRet = -1;

            if (!p_walletPtr->CreateTransaction(vDestinations, *txnPtr.get(), *reserveKeyPtr.get(), nFeeRequired, nChangePosRet, strError, ctrl)) {
                if (totalPaymentAmt + nFeeRequired > curBalance)
                    strError = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
                LogPrint(BCLog::REWARDS, "%s\n", strError.c_str());
                break;
            }
        }
        else {
            std::pair<int, std::string> error;
            std::vector< std::pair<CAssetTransfer, std::string> > vDestinations;

            //  This should (due to external logic) only include pending payments
            for (auto & payment : p_pendingPayments) {
                expectedCount++;

                vDestinations.emplace_back(std::make_pair(
                    CAssetTransfer(p_src, payment.amount, DecodeAssetData(""), 0), payment.address));

                actualCount++;
            }

            // Create the Transaction (this also verifies dest address)
            if (!CreateTransferAssetTransaction(p_walletPtr, ctrl, vDestinations, "", error, *txnPtr.get(), *reserveKeyPtr.get(), nFeeRequired)) {
                LogPrint(BCLog::REWARDS, "Failed to create transfer asset transaction: %s\n", error.second.c_str());
                break;
            }
        }

        //  Indicate success
        fcnRetVal = true;
        std::string txnID = txnPtr->GetHash().GetHex();

        batchResult->push_back(Pair("transaction_id", txnID));
        batchResult->push_back(Pair("total_amount", totalPaymentAmt));
        batchResult->push_back(Pair("fee", nFeeRequired));
        batchResult->push_back(Pair("expected_count", expectedCount));
        batchResult->push_back(Pair("actual_count", actualCount));

        //  This call results in the movement of all p_payments records into the PendingTransaction object
        p_pendingTxns.push_back(
            PendingTransaction(txnID, txnPtr, reserveKeyPtr, batchResult, nFeeRequired, totalPaymentAmt, p_pendingPayments));
    } while (false);

    LogPrint(BCLog::REWARDS, "Transaction generation %s.\n",
        fcnRetVal ? "succeeded" : "failed");

    return fcnRetVal;
}


static const CRPCCommand commands[] =
    {           //  category    name                          actor (function)             argNames
                //  ----------- ------------------------      -----------------------      ----------
            {   "rewards",      "requestsnapshot",            &requestsnapshot,            {"asset_name", "block_height"}},
            {   "rewards",      "getsnapshotrequest",         &getsnapshotrequest,         {"asset_name", "block_height"}},
            {   "rewards",      "cancelsnapshotrequest",      &cancelsnapshotrequest,      {"asset_name", "block_height"}},
            {   "rewards",      "distributereward",           &distributereward,           {"asset_name", "snapshot_height", "distribution_asset_name", "gross_distribution_amount", "exception_addresses"}},
    };

void RegisterRewardsRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
