// Copyright (c) 2017 The Raven Core developers
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

std::string MessageActivationWarning()
{
    return AreMessagingDeployed() ? "" : "\nTHIS COMMAND IS NOT YET ACTIVE!\nhttps://github.com/RavenProject/rips/blob/master/rip-0005.mediawiki\n";
}

//std::string AssetTypeToString(AssetType& assetType)
//{
//    switch (assetType)
//    {
//        case AssetType::ROOT:          return "ROOT";
//        case AssetType::SUB:           return "SUB";
//        case AssetType::UNIQUE:        return "UNIQUE";
//        case AssetType::OWNER:         return "OWNER";
//        case AssetType::MSGCHANNEL:    return "MSGCHANNEL";
//        case AssetType::VOTE:          return "VOTE";
//        case AssetType::REISSUE:       return "REISSUE";
//        case AssetType::INVALID:       return "INVALID";
//        default:            return "UNKNOWN";
//    }
//}

//UniValue UnitValueFromAmount(const CAmount& amount, const std::string asset_name)
//{
//    if (!passets)
//        throw JSONRPCError(RPC_INTERNAL_ERROR, "Asset cache isn't available.");
//
//    uint8_t units = OWNER_UNITS;
//    if (!IsAssetNameAnOwner(asset_name)) {
//        CNewAsset assetData;
//        if (!passets->GetAssetMetaDataIfExists(asset_name, assetData))
//            throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't load asset from cache: " + asset_name);
//
//        units = assetData.units;
//    }
//
//    return ValueFromAmount(amount, units);
//}

UniValue viewallmessages(const JSONRPCRequest& request) {
    if (request.fHelp || !AreMessagingDeployed() || request.params.size() != 0)
        throw std::runtime_error(
                "viewallmessages \n"
                + MessageActivationWarning() +
                "\nView all Messages that the wallet contains\n"

                "\nResult:\n"
                "\"message\"                     (string) The transaction id\n"

                "\nExamples:\n"
                + HelpExampleCli("viewallmessages", "")
        );

    if (!fMessaging) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Messaging is disabled. To enable messaging, run the wallet without -disablemessaging or remove disablemessaging from your raven.conf");
        return ret;
    }

    if (!pMessagesCache || !pmessagedb) {
        UniValue ret(UniValue::VSTR);
        ret.push_back("Messaging database and cache are having problems (a wallet restart might fix this issue)");
        return ret;
    }

    std::set<CMessage> setMessages;

    pmessagedb->LoadMessages(setMessages);

    LogPrintf("%s: Checking caches removeSize:%u, addSize:%u, orphanSize:%u\n", __func__, setDirtyMessagesRemove.size(), mapDirtyMessagesAdd.size(), mapDirtyMessagesOrphaned.size());


    for (auto pair : mapDirtyMessagesOrphaned) {
        CMessage message = pair.second;
        message.status = MessageStatus::ORPHAN;
        if (setMessages.count(message))
            setMessages.erase(message);
        setMessages.insert(message);
    }

    for (auto out : setDirtyMessagesRemove) {
        CMessage message;
        message.out = out;
        setMessages.erase(message);
    }

    for (auto pair : mapDirtyMessagesAdd) {
        setMessages.erase(pair.second);
        setMessages.insert(pair.second);
    }



    UniValue messages(UniValue::VARR);

    for (auto message : setMessages) {
        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("Asset Name", message.strName));
        obj.push_back(Pair("Message", EncodeIPFS(message.ipfsHash)));
        obj.push_back(Pair("Time", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", message.time)));
        obj.push_back(Pair("Block Height", message.nBlockHeight));
        obj.push_back(Pair("Status", MessageStatusToString(message.status)));
        if (message.nExpiredTime)
            obj.push_back(Pair("Expire Time", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", message.nExpiredTime)));

        messages.push_back(obj);
    }


    return messages;
}


static const CRPCCommand commands[] =
        { //  category    name                          actor (function)             argNames
                //  ----------- ------------------------      -----------------------      ----------
            { "messages",   "viewallmessages",                &viewallmessages,            {}}
        };

void RegisterMessageRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
