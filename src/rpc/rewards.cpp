// Copyright (c) 2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include "assets/assetdb.h"
#include "assets/messages.h"
#include "assets/messagedb.h"
#include <map>
#include "tinyformat.h"

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
    const std::set<std::pair<std::string, CAmount>> & p_addrAmtPairs,
    std::string & p_resultantTxID);

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
    CRewardRequestDBEntry entryToAdd;
    entryToAdd.walletName = walletPtr->GetName();
    entryToAdd.heightForPayout = chainActive.Height() + FUTURE_BLOCK_HEIGHT_OFFSET;
    entryToAdd.totalPayoutAmt = total_payout_amount;
    entryToAdd.tgtAssetName = target_asset_name;
    entryToAdd.payoutSrc = payout_source;
    entryToAdd.exceptionAddresses = exception_addresses;

    if (pRewardRequestDb->SchedulePendingReward(entryToAdd))
        return "Reward was successfully scheduled in the database";

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to add scheduled reward to database"));
}

UniValue payout(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
                "payout \"target_asset_name\" block_height\n"
                "\nGenerates payment records for all rewards scheduled for the target asset\n"
                "\tat the specified height.\n"

                "\nArguments:\n"
                "target_asset_name:   (string, required) The asset name to whose owners the reward will be paid\n"
                "block_height: (number, required) The block height at which to schedule the payout\n"

                "\nResult:\n"

                "\nExamples:\n"
                + HelpExampleCli("payout", "\"TRONCO\" 12345")
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
    std::string target_asset_name = request.params[0].get_str();
    int block_height = request.params[1].get_int();

    AssetType tgtAssetType;

    if (!IsAssetNameValid(target_asset_name, tgtAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid target_asset_name: Please use a valid target_asset_name"));

    if (tgtAssetType == AssetType::UNIQUE || tgtAssetType == AssetType::OWNER || tgtAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    if (!passetsdb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Assets database is not setup. Please restart wallet to try again"));
    if (!pAssetSnapshotDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Asset Snapshot database is not setup. Please restart wallet to try again"));
    if (!pRewardRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward Request database is not setup. Please restart wallet to try again"));
    if (!pPayoutDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Payout database is not setup. Please restart wallet to try again"));

    //  Retrieve all scheduled rewards for the target asset at the specified height
    std::set<CRewardRequestDBEntry> dbEntriesToProcess;

    if (pRewardRequestDb->LoadPayableRewardsForAsset(target_asset_name, block_height, dbEntriesToProcess)) {
        //  Loop through them
        for (auto const & rewardEntry : dbEntriesToProcess) {
            //  Retrieve the asset snapshot entry for the target asset at the specified height
            CAssetSnapshotDBEntry snapshotEntry;

            if (pAssetSnapshotDb->RetrieveOwnershipSnapshot(rewardEntry.tgtAssetName, block_height, snapshotEntry)) {
                //  Generate payment transactions and store in the payments DB
                if (!pPayoutDb->GeneratePayouts(rewardEntry, snapshotEntry)) {
                    LogPrintf("Failed to payouts for '%s'!\n", rewardEntry.tgtAssetName.c_str());                
                }
                else {
                    //
                    //  Debug code to dump the payout info
                    //
                    std::set<CPayoutDBEntry> payoutEntries;
                    if (!pPayoutDb->RetrievePayouts(rewardEntry.tgtAssetName, payoutEntries)) {
                        LogPrintf("Failed to retrieve payouts for '%s'!\n",
                            rewardEntry.tgtAssetName.c_str());                
                    }
                    else {
                        for (auto const & payout : payoutEntries) {
                            for (auto const & ownerAndPayout : payout.ownersAndPayouts) {
                                LogPrintf("Found '%s' payout to '%s' of %d\n",
                                    rewardEntry.tgtAssetName.c_str(), ownerAndPayout.first.c_str(),
                                    ownerAndPayout.second);
                            }
                        }
                    }
                    //
                    //  Debug code to dump the payout info
                    //

                    return "Payouts were successfully generated in the database";
                }
            }
            else {
                LogPrintf("Failed to retrieve ownership snapshot for '%s' at height %d!\n",
                    rewardEntry.tgtAssetName.c_str(), block_height);                
            }
        }
    }
    else {
        LogPrintf("Failed to payout reward requests at height %d!\n", block_height);
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to payout specified rewards"));
}


UniValue execute(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 1)
        throw std::runtime_error(
                "execute \"target_asset_name\"\n"
                "\nGenerates transactions for all payment records tied to the target asset.\n"

                "\nArguments:\n"
                "target_asset_name:   (string, required) The asset name to whose owners the reward will be paid\n"

                "\nResult:\n"

                "\nExamples:\n"
                + HelpExampleCli("execute", "\"TRONCO\"")
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
    std::string target_asset_name = request.params[0].get_str();

    AssetType tgtAssetType;

    if (!IsAssetNameValid(target_asset_name, tgtAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid target_asset_name: Please use a valid target_asset_name"));

    if (tgtAssetType == AssetType::UNIQUE || tgtAssetType == AssetType::OWNER || tgtAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    if (!pPayoutDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Payout database is not setup. Please restart wallet to try again"));

    //  Retrieve all scheduled payouts for the target asset
    std::set<CPayoutDBEntry> payoutEntries;
    if (!pPayoutDb->RetrievePayouts(target_asset_name, payoutEntries)) {
        LogPrintf("Failed to retrieve payouts for '%s'!\n",
            target_asset_name.c_str());                
    }
    else {
        //  Process all payouts registered for the specified asset
        for (auto const & payout : payoutEntries) {
            for (auto const & ownerAndPayout : payout.ownersAndPayouts) {
                LogPrintf("Found '%s' payout to '%s' of %d\n",
                    payout.assetName.c_str(), ownerAndPayout.first.c_str(),
                    ownerAndPayout.second);
            }

            //  Loop through each payable account for the asset, sending it the appropriate portion of the total payout amount
            std::string transactionID;
            if (!InitiateTransfer(walletPtr, payout.srcAssetName, payout.ownersAndPayouts, transactionID)) {
                LogPrintf("Transaction generation failed for '%s' using source '%s'!\n",
                    payout.assetName.c_str(), payout.srcAssetName.c_str());
            }
        }

        return "Transactions were successfully executed in the database";
    }

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to payout specified rewards"));
}

static const CRPCCommand commands[] =
    {           //  category    name                          actor (function)             argNames
                //  ----------- ------------------------      -----------------------      ----------
            {   "rewards",      "reward",                     &reward,                     {"total_payout_amount", "payout_source", "target_asset_name", "exception_addresses"}},
            {   "rewards",      "payout",                     &payout,                     {"target_asset_name", "block_height"}},
            {   "rewards",      "execute",                     &execute,                     {"target_asset_name"}},
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
    const std::set<std::pair<std::string, CAmount>> & p_addrAmtPairs,
    std::string & p_resultantTxID)
{
    bool fcnRetVal = false;

    LogPrintf("Initiating transfer...\n");

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
            bool errorsOccurred = false;
            CAmount totalPaymentAmt = 0;

            for (auto const & addrAmtPair : p_addrAmtPairs) {
                // Parse Raven address
                CTxDestination dest = DecodeDestination(addrAmtPair.first);
                if (!IsValidDestination(dest)) {
                    LogPrintf("Destination address '%s' is invalid.\n", addrAmtPair.first.c_str());
                    errorsOccurred = true;
                    continue;
                }

                CScript scriptPubKey = GetScriptForDestination(dest);
                CRecipient recipient = {scriptPubKey, addrAmtPair.second, false};
                vDestinations.emplace_back(recipient);

                totalPaymentAmt += addrAmtPair.second;
            }

            if (errorsOccurred) {
                LogPrintf("Breaking out due to invalid destination address(es)\n");
                break;
            }

            //  Verify funds
            if (totalPaymentAmt > curBalance) {
                LogPrintf("Insufficient funds\n");
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
        }
        else {
            std::pair<int, std::string> error;
            std::vector< std::pair<CAssetTransfer, std::string> > vDestinations;

            for (auto const & addrAmtPair : p_addrAmtPairs) {
                LogPrintf("Sending asset '%s' to address '%s' as reward %d\n",
                    p_src.c_str(), addrAmtPair.first.c_str(),
                    addrAmtPair.second);

                vDestinations.emplace_back(std::make_pair(CAssetTransfer(p_src, addrAmtPair.second, DecodeAssetData(""), 0), addrAmtPair.first));
            }

            CReserveKey reservekey(p_walletPtr);
            CAmount nRequiredFee;

            // Create the Transaction
            if (!CreateTransferAssetTransaction(p_walletPtr, ctrl, vDestinations, "", error, transaction, reservekey, nRequiredFee)) {
                LogPrintf("Failed to create transfer asset transaction: %s\n", error.second.c_str());
                break;
            }

            // Send the Transaction to the network
            if (!SendAssetTransaction(p_walletPtr, transaction, reservekey, error, p_resultantTxID)) {
                LogPrintf("Failed to send asset transaction: %s\n", error.second.c_str());
                break;
            }
        }

        //  Indicate success
        fcnRetVal = true;
    } while (false);

    LogPrintf("Transfer processing %s.\n",
        fcnRetVal ? "succeeded" : "failed");

    return fcnRetVal;
}

