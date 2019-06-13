// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include "assets/assetdb.h"
#include "assets/messages.h"
#include "assets/messagedb.h"
#include <map>
#include "tinyformat.h"

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/random_generator.hpp>

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
#include "assets/rewardrequestdb.h"
#include "assets/assetsnapshotdb.h"
#include "assets/payoutdb.h"

CAmount AmountFromValue(bool p_isRVN, const UniValue& p_value);

//  Transfer the specified amount of the asset from the source to the target
bool InitiateTransfer(
    CWallet * const p_walletPtr, const std::string & p_src,
    std::vector<CPayment> & p_payments,
    UniValue & p_batchResult);

UniValue reward(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 3)
        throw std::runtime_error(
                "reward total_payout_amount \"payout_source\" \"target_asset_name\" ( \"exception_addresses\" )\n"
                "\nSchedules a payout for the specified amount, using either RVN or the specified source asset name,\n"
                "\tto all owners of the specified asset, excluding the exception addresses.\n"

                "\nArguments:\n"
                "total_payout_amount: (number, required) The amount of the source asset to distribute amongst owners of the target asset\n"
                "payout_source: (string, required) Either RVN or the asset name to distribute as the reward\n"
                "target_asset_name:   (string, required) The asset name to whose owners the reward will be paid\n"
                "exception_addresses: (comma-delimited string, optional) A list of exception addresses that should not receive rewards\n"

                "\nResult:\n"

                "\nExamples:\n"
                + HelpExampleCli("reward", "100 \"RVN\" \"TRONCO\"")
                + HelpExampleRpc("reward", "1000 \"BLACKCO\" \"TRONCO\" \"RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H,RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required to schedule a reward. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
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
    std::string payout_source = request.params[1].get_str();

    CAmount total_payout_amount = AmountFromValue(
        (payout_source == "RVN"), request.params[0]);
    if (total_payout_amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount to reward");

    std::string target_asset_name = request.params[2].get_str();
    std::string exception_addresses;
    if (request.params.size() > 3) {
        exception_addresses = request.params[3].get_str();
    }

    AssetType srcAssetType;
    AssetType tgtAssetType;

    if (payout_source != "RVN") {
        if (!IsAssetNameValid(payout_source, srcAssetType))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid payout_source: Please use a valid payout_source"));

        if (srcAssetType == AssetType::UNIQUE || srcAssetType == AssetType::OWNER || srcAssetType == AssetType::MSGCHANNEL)
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));
    }

    if (!IsAssetNameValid(target_asset_name, tgtAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid target_asset_name: Please use a valid target_asset_name"));

    if (tgtAssetType == AssetType::UNIQUE || tgtAssetType == AssetType::OWNER || tgtAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    if (!pRewardRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward Request database is not setup. Please restart wallet to try again"));

    const int64_t FUTURE_BLOCK_HEIGHT_OFFSET = 1; //  Select to hopefully be far enough forward to be safe from forks

    //  Build our reward record for scheduling
    boost::uuids::random_generator uuidGenerator;
    boost::uuids::uuid rewardUUID = uuidGenerator();
    CRewardRequestDBEntry entryToAdd;
    entryToAdd.rewardID = to_string(rewardUUID);
    entryToAdd.walletName = walletPtr->GetName();
    entryToAdd.heightForPayout = chainActive.Height() + FUTURE_BLOCK_HEIGHT_OFFSET;
    entryToAdd.totalPayoutAmt = total_payout_amount;
    entryToAdd.tgtAssetName = target_asset_name;
    entryToAdd.payoutSrc = payout_source;
    entryToAdd.exceptionAddresses = exception_addresses;

    if (pRewardRequestDb->SchedulePendingReward(entryToAdd)) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("Reward ID", entryToAdd.rewardID));
        obj.push_back(Pair("Block Height", entryToAdd.heightForPayout));

        return obj;
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to add scheduled reward to database"));
}

UniValue payout(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "payout \"reward_id\"\n"
                "\nGenerates payment records for the specified reward ID.\n"

                "\nArguments:\n"
                "reward_id:   (string, required) The ID for the reward that will be paid\n"

                "\nResult:\n"

                "\nExamples:\n"
                + HelpExampleCli("payout", "\"de5c1822-6556-42da-b86f-deb8ccd78565\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required to payout a reward. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
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
    std::string rewardID = request.params[0].get_str();

    if (!passetsdb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Assets database is not setup. Please restart wallet to try again"));
    if (!pAssetSnapshotDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Asset Snapshot database is not setup. Please restart wallet to try again"));
    if (!pRewardRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward Request database is not setup. Please restart wallet to try again"));
    if (!pPayoutDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Payout database is not setup. Please restart wallet to try again"));

    //  Retrieve the specified reward
    CRewardRequestDBEntry rewardEntry;

    if (pRewardRequestDb->RetrieveRewardWithID(rewardID, rewardEntry)) {
        //  Retrieve the asset snapshot entry for the target asset at the specified height
        CAssetSnapshotDBEntry snapshotEntry;

        if (pAssetSnapshotDb->RetrieveOwnershipSnapshot(rewardEntry.tgtAssetName, rewardEntry.heightForPayout, snapshotEntry)) {
            //  Generate payment transactions and store in the payments DB
            CPayoutDBEntry payoutEntry;
            if (!pPayoutDb->GeneratePayouts(rewardEntry, snapshotEntry, payoutEntry)) {
                LogPrintf("Failed to generate payouts for reward '%s'!\n", rewardEntry.rewardID.c_str());                
            }
            else {
                UniValue obj(UniValue::VOBJ);

                obj.push_back(Pair("Reward ID", rewardEntry.rewardID));
                obj.push_back(Pair("Target Asset", rewardEntry.tgtAssetName));
                obj.push_back(Pair("Funding Asset", rewardEntry.payoutSrc));
                obj.push_back(Pair("Payout Block Height", rewardEntry.heightForPayout));

                UniValue entries(UniValue::VARR);
                for (auto const & payment : payoutEntry.payments) {
                    LogPrintf("Found '%s' payout to '%s' of %d\n",
                        rewardEntry.tgtAssetName.c_str(), payment.address.c_str(),
                        payment.payoutAmt);

                    UniValue entry(UniValue::VOBJ);

                    entry.push_back(Pair("Owner", payment.address));
                    entry.push_back(Pair("Payout", payment.payoutAmt));

                    entries.push_back(entry);
                }

                obj.push_back(Pair("Addresses and Amounts", entries));

                return obj;
            }
        }
        else {
            LogPrintf("Failed to retrieve ownership snapshot for '%s' at height %d!\n",
                rewardEntry.tgtAssetName.c_str(), rewardEntry.heightForPayout);                
        }
    }
    else {
        LogPrintf("Failed to retrieve specified reward '%s'!\n", rewardID.c_str());
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to payout specified rewards"));
}


UniValue execute(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "execute \"reward_id\"\n"
                "\nGenerates transactions for all payment records tied to the specified reward.\n"

                "\nArguments:\n"
                "reward_id:   (string, required) The ID for the reward for which transactions will be generated\n"

                "\nResult:\n"

                "\nExamples:\n"
                + HelpExampleCli("execute", "\"de5c1822-6556-42da-b86f-deb8ccd78565\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required to payout a reward. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
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
    std::string rewardID = request.params[0].get_str();

    if (!pPayoutDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Payout database is not setup. Please restart wallet to try again"));

    //  Retrieve all scheduled payouts for the target asset
    CPayoutDBEntry payoutEntry;
    if (!pPayoutDb->RetrievePayoutEntry(rewardID, payoutEntry)) {
        LogPrintf("Failed to retrieve payout entry for reward '%s'!\n",
            rewardID.c_str());                
    }
    else {
        UniValue responseObj(UniValue::VOBJ);

        responseObj.push_back(Pair("Reward ID", payoutEntry.rewardID));

        //
        //  Loop through the payout addresses and process them in batches
        //
        const int MAX_PAYMENTS_PER_TRANSACTION = 50;
        UniValue batchResults(UniValue::VARR);
        bool atLeastOneTxnSucceeded = false;
        std::vector<CPayment> paymentVector;
        std::set<CPayment> updatedPayments;

        for (auto & payment : payoutEntry.payments) {
            paymentVector.push_back(payment);

            //  Issue a transaction if we've hit the max payment count
            if (paymentVector.size() >= MAX_PAYMENTS_PER_TRANSACTION) {
                UniValue batchResult(UniValue::VOBJ);

                //  Build a transaction for the current batch
                if (InitiateTransfer(walletPtr, payoutEntry.srcAssetName, paymentVector, batchResult)) {
                    atLeastOneTxnSucceeded = true;
                }
                else {
                    LogPrintf("Transaction generation failed for '%s' using source '%s'!\n",
                        payoutEntry.assetName.c_str(), payoutEntry.srcAssetName.c_str());
                }

                batchResults.push_back(batchResult);

                //  Move the updated payments into a new set
                for (auto const & payment : paymentVector) {
                    updatedPayments.insert(payment);
                }

                //  Clear the vector after a batch is processed
                paymentVector.clear();
            }
        }

        //
        //  If any payments are left in the last batch, send them
        //
        if (paymentVector.size() > 0) {
            UniValue batchResult(UniValue::VOBJ);

            //  Build a transaction for the current batch
            if (InitiateTransfer(walletPtr, payoutEntry.srcAssetName, paymentVector, batchResult)) {
                atLeastOneTxnSucceeded = true;
            }
            else {
                LogPrintf("Transaction generation failed for '%s' using source '%s'!\n",
                    payoutEntry.assetName.c_str(), payoutEntry.srcAssetName.c_str());
            }

            batchResults.push_back(batchResult);

            //  Move the updated payments into a new set
            for (auto const & payment : paymentVector) {
                updatedPayments.insert(payment);
            }

            //  Clear the vector after a batch is processed
            paymentVector.clear();
        }

        //  Replace the existing set of payments with the updated one
        payoutEntry.payments.clear();
        for (auto const & payment : updatedPayments) {
            payoutEntry.payments.insert(payment);
        }
        updatedPayments.clear();

        responseObj.push_back(Pair("Batch Results", batchResults));

        //  Write the payment back to the database if anything succeded
        if (atLeastOneTxnSucceeded) {
            if (pPayoutDb->UpdatePayoutEntry(payoutEntry)) {
                responseObj.push_back(Pair("Payout DB Update", "succeeded"));
            }
            else {
                LogPrintf("Failed to update payout DB payment status for reward '%s'!\n",
                    payoutEntry.rewardID.c_str());
                responseObj.push_back(Pair("Payout DB Update", "failed"));
            }
        }

        return responseObj;
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to payout specified rewards"));
}

static const CRPCCommand commands[] =
    {           //  category    name                          actor (function)             argNames
                //  ----------- ------------------------      -----------------------      ----------
            {   "rewards",      "reward",                     &reward,                     {"total_payout_amount", "payout_source", "target_asset_name", "exception_addresses"}},
            {   "rewards",      "payout",                     &payout,                     {"reward_id"}},
            {   "rewards",      "execute",                    &execute,                    {"reward_id"}},
    };

void RegisterRewardsRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

CAmount AmountFromValue(bool p_isRVN, const UniValue& p_value)
{
    if (!p_value.isNum() && !p_value.isStr())
        throw JSONRPCError(RPC_TYPE_ERROR, "Amount is not a number or string");

    CAmount amount;
    if (!ParseFixedPoint(p_value.getValStr(), 8, &amount))
        throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Invalid amount (3): %s", p_value.getValStr()));

    if (p_isRVN && !MoneyRange(amount))
        throw JSONRPCError(RPC_TYPE_ERROR, strprintf("Amount out of range: %s", amount));

    return amount;
}

bool InitiateTransfer(
    CWallet * const p_walletPtr, const std::string & p_src,
    std::vector<CPayment> & p_payments,
    UniValue & p_batchResult)
{
    bool fcnRetVal = false;
    std::string transactionID;
    size_t expectedCount = 0;
    size_t actualCount = 0;

    LogPrintf("Initiating batch transfer...\n");

    // While condition is false to ensure a single pass through this logic
    do {
        //  Transfer the specified amount of the asset from the source to the target
        CCoinControl ctrl;
        CWalletTx transaction;

        //  Handle payouts using RVN differently from those using an asset
        if (p_src == "RVN") {
            // Check amount
            CAmount curBalance = p_walletPtr->GetBalance();

            if (p_walletPtr->GetBroadcastTransactions() && !g_connman) {
                LogPrintf("Error: Peer-to-peer functionality missing or disabled\n");
                break;
            }

            std::vector<CRecipient> vDestinations;
            CAmount totalPaymentAmt = 0;

            for (auto & payment : p_payments) {
                //  Have we already processed this payment?
                if (payment.completed) {
                    continue;
                }

                expectedCount++;

                // Parse Raven address
                CTxDestination dest = DecodeDestination(payment.address);
                if (!IsValidDestination(dest)) {
                    LogPrintf("Destination address '%s' is invalid.\n", payment.address.c_str());
                    payment.completed = true;
                    continue;
                }

                CScript scriptPubKey = GetScriptForDestination(dest);
                CRecipient recipient = {scriptPubKey, payment.payoutAmt, false};
                vDestinations.emplace_back(recipient);

                totalPaymentAmt += payment.payoutAmt;
                actualCount++;
            }

            //  Verify funds
            if (totalPaymentAmt > curBalance) {
                LogPrintf("Insufficient funds: total payment %lld > available balance %lld\n",
                    totalPaymentAmt, curBalance);
                break;
            }

            // Create and send the transaction
            CReserveKey reservekey(p_walletPtr);
            CAmount nFeeRequired;
            std::string strError;
            int nChangePosRet = -1;

            if (!p_walletPtr->CreateTransaction(vDestinations, transaction, reservekey, nFeeRequired, nChangePosRet, strError, ctrl)) {
                if (totalPaymentAmt + nFeeRequired > curBalance)
                    strError = strprintf("Error: This transaction requires a transaction fee of at least %s", FormatMoney(nFeeRequired));
                LogPrintf("%s\n", strError.c_str());
                break;
            }

            CValidationState state;
            if (!p_walletPtr->CommitTransaction(transaction, reservekey, g_connman.get(), state)) {
                LogPrintf("Error: The transaction was rejected! Reason given: %s\n", state.GetRejectReason());
                break;
            }

            transactionID = transaction.GetHash().GetHex();
        }
        else {
            std::pair<int, std::string> error;
            std::vector< std::pair<CAssetTransfer, std::string> > vDestinations;

            for (auto & payment : p_payments) {
                //  Have we already processed this payment?
                if (payment.completed) {
                    continue;
                }

                expectedCount++;

                vDestinations.emplace_back(std::make_pair(
                    CAssetTransfer(p_src, payment.payoutAmt, DecodeAssetData(""), 0), payment.address));

                actualCount++;
            }

            CReserveKey reservekey(p_walletPtr);
            CAmount nRequiredFee;

            // Create the Transaction
            if (!CreateTransferAssetTransaction(p_walletPtr, ctrl, vDestinations, "", error, transaction, reservekey, nRequiredFee)) {
                LogPrintf("Failed to create transfer asset transaction: %s\n", error.second.c_str());
                break;
            }

            // Send the Transaction to the network
            if (!SendAssetTransaction(p_walletPtr, transaction, reservekey, error, transactionID)) {
                LogPrintf("Failed to send asset transaction: %s\n", error.second.c_str());
                break;
            }
        }

        //  Indicate success
        fcnRetVal = true;
        p_batchResult.push_back(Pair("Transaction ID", transactionID));

        //  Post-process the payments in the batch to flag them as completed
        for (auto & payment : p_payments) {
            payment.completed = true;
        }
    } while (false);

    LogPrintf("Batch transfer processing %s.\n",
        fcnRetVal ? "succeeded" : "failed");
    p_batchResult.push_back(Pair("Result", fcnRetVal ? "succeeded" : "failed"));
    p_batchResult.push_back(Pair("Expected Count", expectedCount));
    p_batchResult.push_back(Pair("Actual Count", actualCount));

    return fcnRetVal;
}

