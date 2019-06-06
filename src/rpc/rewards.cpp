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
#include "assets/rewardsdb.h"

CAmount AmountFromValue(bool p_isRVN, const UniValue& p_value);

UniValue reward(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() < 3)
        throw std::runtime_error(
                "reward total_payout_amount \"payout_source\" \"target_asset_name\" ( \"exception_addresses\" )\n"
                "\nSchedules a payout for the specified amount, using either RVN or the specified source asset name,\n"
                "\tto all owners of the specified asset, excluding the exception addresses.\n"

                "\nArguments:\n"
                "total_payout_amount: (number, required) The block height at which to schedule the payout\n"
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
        ret.push_back("Rewards system is required to make a reward payout. To enable rewards, run the wallet with -rewards or add rewards from your raven.conf and perform a -reindex");
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
        (payout_source.compare("RVN") == 0), request.params[0]);
    if (total_payout_amount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount to reward");

    std::string target_asset_name = request.params[2].get_str();
    std::string exception_addresses;
    if (request.params.size() > 3) {
        exception_addresses = request.params[3].get_str();
    }

    AssetType srcAssetType;
    AssetType tgtAssetType;

    if (payout_source.compare("RVN") != 0) {
        if (!IsAssetNameValid(payout_source, srcAssetType))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid payout_source: Please use a valid payout_source"));

        if (srcAssetType == AssetType::UNIQUE || srcAssetType == AssetType::OWNER || srcAssetType == AssetType::MSGCHANNEL)
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));
    }

    if (!IsAssetNameValid(target_asset_name, tgtAssetType))
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid target_asset_name: Please use a valid target_asset_name"));

    if (tgtAssetType == AssetType::UNIQUE || tgtAssetType == AssetType::OWNER || tgtAssetType == AssetType::MSGCHANNEL)
        throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid asset_name: OWNER, UNQIUE, MSGCHANNEL assets are not allowed for this call"));

    if (!pRewardsDb)
        throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Reward database is not setup. Please restart wallet to try again"));

    const int64_t FUTURE_BLOCK_HEIGHT_OFFSET = 1; //  Select to hopefully be far enough forward to be safe from forks

    //  Build our reward record for scheduling
    CRewardsDBEntry entryToAdd;
    entryToAdd.walletName = walletPtr->GetName();
    entryToAdd.heightForPayout = chainActive.Height() + FUTURE_BLOCK_HEIGHT_OFFSET;
    entryToAdd.totalPayoutAmt = total_payout_amount;
    entryToAdd.tgtAssetName = target_asset_name;
    entryToAdd.payoutSrc = payout_source;
    entryToAdd.exceptionAddresses = exception_addresses;

    if (pRewardsDb->SchedulePendingReward(entryToAdd))
        return "Reward was successfully scheduled in the database";

    throw JSONRPCError(RPC_DATABASE_ERROR, std::string("Failed to add scheduled reward to database"));
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
