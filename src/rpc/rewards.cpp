// Copyright (c) 2017-2019 The Raven Core developers
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
#include "assets/rewardsdb.h"

UniValue reward(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
                "reward \n"
                "\nSchedules a payout for the specified amount, using either RVN or the specified source asset name,\n"
                "\tto all owners of the specified asset, excluding the exception addresses.\n"

                "\nArguments:\n"
                "total_payout_amount: (number, required) The block height at which to schedule the payout\n"
                "payout_source: (string, required) Either RVN or the asset name to distribute as the reward\n"
                "target_asset_name:   (string, required) The asset name to whose owners the reward will be paid\n"
                "exception_addresses: (JSON string array, optional) A list of exception addresses that should not receive rewards\n"

                "\nResult:\n"

                "\nExamples:\n"
                + HelpExampleCli("reward", "100 \"RVN\" \"TRONCO\"")
                + HelpExampleRpc("reward", "1000 \"BLACKCO\" \"TRONCO\" \"['RBQ5A9wYKcebZtTSrJ5E4bKgPRbNmr8M2H','RCqsnXo2Uc1tfNxwnFzkTYXfjKP21VX5ZD']\"")
        );

    if (!fRewardsEnabled) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Rewards system is required to make a reward payout. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
        return ret;
    }

    //  Extract parameters
    int64_t total_payout_amount = request.params[0].get_int64();
    std::string payout_source = request.params[1].get_str();
    std::string target_asset_name = request.params[2].get_str();
    std::string exception_addresses = request.params[3].get_str();


    AssetType srcAssetType;
    AssetType tgtAssetType;

    if (!IsAssetNameValid(payout_source, srcAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid payout_source: Please use a valid payout_source"));

    if (srcAssetType == AssetType::UNIQUE || srcAssetType == AssetType::OWNER || srcAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    if (!IsAssetNameValid(target_asset_name, tgtAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid target_asset_name: Please use a valid target_asset_name"));

    if (tgtAssetType == AssetType::UNIQUE || tgtAssetType == AssetType::OWNER || tgtAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    const int64_t FUTURE_BLOCK_HEIGHT_OFFSET = 100; //  Select to hopefully be far enough forward to be safe from forks
    int64_t blockHeightForSnapshot = chainActive.Height() + FUTURE_BLOCK_HEIGHT_OFFSET;

    if (!pRewardsDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward database is not setup. Please restart wallet to try again"));

    if (pRewardsDb->WriteSnapshotCheck(target_asset_name, blockHeightForSnapshot))
        return "Reward Snapshot Check was successfully added to the database";

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to add Snapshot Check to database"));
}

static const CRPCCommand commands[] =
    {           //  category    name                          actor (function)             argNames
                //  ----------- ------------------------      -----------------------      ----------
            {   "rewards",      "reward",                     &reward,                     {"total_payout_amount", "payout_source", "target_asset_name", "exception_addresses"}},
    };

void RegisterRewardsRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
