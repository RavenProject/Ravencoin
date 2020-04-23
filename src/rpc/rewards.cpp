// Copyright (c) 2019-2020 The Raven Core developers
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

#ifdef ENABLE_WALLET

////  Collect information about transactions that are pending commit
//struct PendingTransaction
//{
//    std::string id;
//    std::shared_ptr<CWalletTx> ptr;
//    std::shared_ptr<CReserveKey> reserveKey;
//    std::shared_ptr<UniValue> result;
//    CAmount fee;
//    CAmount totalAmt;
//    std::vector<OwnerAndAmount> payments;
//
//    PendingTransaction(
//        const std::string & p_txnID,
//        std::shared_ptr<CWalletTx> p_txnPtr,
//        std::shared_ptr<CReserveKey> p_reserveKey,
//        std::shared_ptr<UniValue> p_result,
//        CAmount p_txnFee,
//        CAmount p_txnAmount,
//        std::vector<OwnerAndAmount> & p_payments
//    )
//    {
//        id = p_txnID;
//        ptr = p_txnPtr;
//        reserveKey = p_reserveKey;
//        result = p_result;
//        fee = p_txnFee;
//        totalAmt = p_txnAmount;
//        payments = std::move(p_payments);
//    }
//};

UniValue requestsnapshot(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
                "requestsnapshot \"asset_name\" block_height\n"
                "\nSchedules a snapshot of the specified asset at the specified block height.\n"

                "\nArguments:\n"
                "1. \"asset_name\"              (string, required) The asset name for which the snapshot will be taken\n"
                "2. \"block_height\"            (number, required) The block height at which the snapshot will be take\n"

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

    auto currentActiveAssetCache = GetCurrentAssetCache();
    if (!currentActiveAssetCache)
        return "_Couldn't get current asset cache.";
    CNewAsset asset;
    if (!currentActiveAssetCache->GetAssetMetaDataIfExists(asset_name, asset))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: asset does not exist."));

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
                "1. \"asset_name\"              (string, required) The asset name for which the snapshot will be taken\n"
                "2. \"block_height\"            (number, required) The block height at which the snapshot will be take\n"

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

UniValue listsnapshotrequests(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
                "listsnapshotrequests [\"asset_name\" [block_height]]\n"
                "\nList snapshot request details.\n"

                "\nArguments:\n"
                "asset_name: (string, optional) List only requests for a specific asset (default is \"\" for ALL)\n"
                "block_height: (number, optional) List only requests for a particular block height (default is 0 for ALL)\n"

                "\nResult:\n"
                "[\n"
                "  {\n"
                "    asset_name: (string),\n"
                "    block_height: (number)\n"
                "  }\n"
                "]\n"

                "\nExamples:\n"
                + HelpExampleCli("listsnapshotrequests", "")
                + HelpExampleRpc("listsnapshotrequests", "\"TRONCO\" 345333")
        );

    if (!fAssetIndex)
        return "_This rpc call is not functional unless -assetindex is enabled. To enable, please run the wallet with -assetindex, this will require a reindex to occur";

    //  Extract parameters
    std::string asset_name = "";
    int block_height = 0;
    if (request.params.size() > 0)
        asset_name = request.params[0].get_str();
    if (request.params.size() > 1)
        block_height = request.params[1].get_int();

    if (!pSnapshotRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Snapshot Request database is not setup. Please restart wallet to try again"));

    UniValue result(UniValue::VARR);
    std::set<CSnapshotRequestDBEntry> entries;
    if (pSnapshotRequestDb->RetrieveSnapshotRequestsForHeight(asset_name, block_height, entries)) {
        for (auto const &entry : entries) {
            UniValue item(UniValue::VOBJ);
            item.push_back(Pair("asset_name", entry.assetName));
            item.push_back(Pair("block_height", entry.heightForSnapshot));
            result.push_back(item);
        }
        return result;
    }
    else {
        LogPrint(BCLog::REWARDS, "Failed to cancel specified snapshot request for asset '%s' at height %d!\n",
                 asset_name.c_str(), block_height);
    }

    return NullUniValue;
}

UniValue cancelsnapshotrequest(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 2)
        throw std::runtime_error(
                "cancelsnapshotrequest \"asset_name\" block_height\n"
                "\nCancels the specified snapshot request.\n"

                "\nArguments:\n"
                "1. \"asset_name\"              (string, required) The asset name for which the snapshot will be taken\n"
                "2. \"block_height\"            (number, required) The block height at which the snapshot will be take\n"

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
                "distributereward \"asset_name\" snapshot_height \"distribution_asset_name\" gross_distribution_amount ( \"exception_addresses\" ) (\"change_address\") (\"dry_run\")\n"
                "\nSplits the specified amount of the distribution asset to all owners of asset_name that are not in the optional exclusion_addresses\n"

                "\nArguments:\n"
                "1. \"asset_name\"                 (string, required) The reward will be distributed all owners of this asset\n"
                "2. \"snapshot_height\"            (number, required) The block height of the ownership snapshot\n"
                "3. \"distribution_asset_name\"    (string, required) The name of the asset that will be distributed, or RVN\n"
                "4. \"gross_distribution_amount\"  (number, required) The amount of the distribution asset that will be split amongst all owners\n"
                "5. \"exception_addresses\"        (string, optional) Ownership addresses that should be excluded\n"
                "6. \"change_address\"             (string, optional) If the rewards can't be fully distributed. The change will be sent to this address\n"

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
                + HelpExampleCli("distributereward", "\"TRONCO\" 12345 \"RVN\" 1000")
                + HelpExampleCli("distributereward", "\"PHATSTACKS\" 12345 \"DIVIDENDS\" 1000 \"mwN7xC3yomYdvJuVXkVC7ymY9wNBjWNduD,n4Rf18edydDaRBh7t6gHUbuByLbWEoWUTg\"")
                + HelpExampleRpc("distributereward", "\"TRONCO\" 34987 \"DIVIDENDS\" 100000")
                + HelpExampleRpc("distributereward", "\"PHATSTACKS\" 34987 \"RVN\" 100000 \"mwN7xC3yomYdvJuVXkVC7ymY9wNBjWNduD,n4Rf18edydDaRBh7t6gHUbuByLbWEoWUTg\"")
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

    std::string change_address = "";
    if (request.params.size() > 5) {
        change_address = request.params[5].get_str();
        if (!change_address.empty() && !IsValidDestinationString(change_address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid change address: Use a valid RVN address"));
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

        std::pair<int, std::string> errorPair;
        if (!VerifyWalletHasAsset(distribution_asset_name + OWNER_TAG, errorPair))
            throw JSONRPCError(RPC_INVALID_REQUEST, std::string("Wallet doesn't have the ownership token(!) for the distribution asset"));
    }

    if (chainActive.Height() - snapshot_height < gArgs.GetArg("-minrewardheight", MINIMUM_REWARDS_PAYOUT_HEIGHT)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, std::string(
                "For security of the rewards payout, it is recommended to wait until chain is 60 blocks ahead of the snapshot height. You can modify this by using the -minrewardsheight."));
    }

    if (!passets)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Asset cache not setup. Please restart wallet to try again"));

    CNewAsset assetMetaData;
    if (!passets->GetAssetMetaDataIfExists(asset_name, assetMetaData))
        throw JSONRPCError(RPC_INVALID_REQUEST, std::string("The asset hasn't been created: ") + asset_name);

    if (!passetsdb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Assets database is not setup. Please restart wallet to try again"));
    if (!pAssetSnapshotDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Asset Snapshot database is not setup. Please restart wallet to try again"));

    if (!pSnapshotRequestDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Snapshot Request database is not setup. Please restart wallet to try again"));

    if (!pSnapshotRequestDb->ContainsSnapshotRequest(asset_name, snapshot_height))
        throw JSONRPCError(RPC_INVALID_REQUEST, std::string("Snapshot request not found"));

    CRewardSnapshot distribRewardSnapshotData(asset_name, distribution_asset_name, exception_addresses, distribution_amount, snapshot_height);
    if (!AddDistributeRewardSnapshot(distribRewardSnapshotData))
        throw JSONRPCError(RPC_INVALID_REQUEST, std::string("Distribution of reward has already be created. You must remove the distribution before creating another one"));

    // Trigger the distribution
    DistributeRewardSnapshot(walletPtr, distribRewardSnapshotData);

    return "Created reward distribution";
}

UniValue getdistributestatus(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 4)
        throw std::runtime_error(
                "getdistributestatus \"asset_name\" snapshot_height \"distribution_asset_name\" gross_distribution_amount ( \"exception_addresses\" )\n"
                "\nGive information about the status of the distribution\n"

                "\nArguments:\n"
                "1. \"asset_name\"                 (string, required) The reward will be distributed all owners of this asset\n"
                "2. \"snapshot_height\"            (number, required) The block height of the ownership snapshot\n"
                "3. \"distribution_asset_name\"    (string, required) The name of the asset that will be distributed, or RVN\n"
                "4. \"gross_distribution_amount\"  (number, required) The amount of the distribution asset that will be split amongst all owners\n"
                "5. \"exception_addresses\"        (string, optional) Ownership addresses that should be excluded\n"

                "\nExamples:\n"
                + HelpExampleCli("getdistributestatus", "\"TRONCO\" 12345 \"RVN\" 1000")
                + HelpExampleCli("getdistributestatus", "\"PHATSTACKS\" 12345 \"DIVIDENDS\" 1000 \"mwN7xC3yomYdvJuVXkVC7ymY9wNBjWNduD,n4Rf18edydDaRBh7t6gHUbuByLbWEoWUTg\"")
                + HelpExampleRpc("getdistributestatus", "\"TRONCO\" 34987 \"DIVIDENDS\" 100000")
                + HelpExampleRpc("getdistributestatus", "\"PHATSTACKS\" 34987 \"RVN\" 100000 \"mwN7xC3yomYdvJuVXkVC7ymY9wNBjWNduD,n4Rf18edydDaRBh7t6gHUbuByLbWEoWUTg\"")
        );

    if (!fAssetIndex) {
        return "_This rpc call is not functional unless -assetindex is enabled. To enable, please run the wallet with -assetindex, this will require a reindex to occur";
    }

    ObserveSafeMode();

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

    if (!pDistributeSnapshotDb)
        throw JSONRPCError(RPC_INVALID_REQUEST, std::string("Snapshot request database is not setup.  Please restart wallet to try again"));

    CRewardSnapshot distribRewardSnapshotData(asset_name, distribution_asset_name, exception_addresses, distribution_amount, snapshot_height);
    auto hash = distribRewardSnapshotData.GetHash();

    CRewardSnapshot temp;
    if (!pDistributeSnapshotDb->RetrieveDistributeSnapshotRequest(hash, temp)) {
        return "Distribution not found";
    }

    UniValue responseObj(UniValue::VOBJ);

    responseObj.push_back(std::make_pair("Asset Name", temp.strOwnershipAsset));
    responseObj.push_back(std::make_pair("Height", std::to_string(temp.nHeight)));
    responseObj.push_back(std::make_pair("Distribution Name", temp.strDistributionAsset));
    responseObj.push_back(std::make_pair("Distribution Amount", ValueFromAmount(temp.nDistributionAmount)));
    responseObj.push_back(std::make_pair("Status", temp.nStatus));

    return responseObj;
}
#endif



static const CRPCCommand commands[] =
    {           //  category    name                          actor (function)             argNames
                //  ----------- ------------------------      -----------------------      ----------
#ifdef ENABLE_WALLET
            {   "rewards",      "requestsnapshot",            &requestsnapshot,            {"asset_name", "block_height"}},
            {   "rewards",      "getsnapshotrequest",         &getsnapshotrequest,         {"asset_name", "block_height"}},
            {   "rewards",      "listsnapshotrequests",         &listsnapshotrequests,         {"asset_name", "block_height"}},
            {   "rewards",      "cancelsnapshotrequest",      &cancelsnapshotrequest,      {"asset_name", "block_height"}},
            {   "rewards",      "distributereward",           &distributereward,           {"asset_name", "snapshot_height", "distribution_asset_name", "gross_distribution_amount", "exception_addresses", "change_address"}},
            {   "rewards",      "getdistributestatus",        &getdistributestatus,            {"asset_name", "block_height", "distribution_asset_name", "gross_distribution_amount", "exception_addresses"}}
    #endif
    };

void RegisterRewardsRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}


