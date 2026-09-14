// Copyright (c) 2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assets/assets.h"
#include "assets/assettypes.h"

#include "amount.h"
#include "base58.h"
#include "chain.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "policy/policy.h"
#include "rpc/safemode.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/standard.h"
#include "script/sign.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validation.h"
#include "net.h"

#ifdef ENABLE_WALLET
#include "wallet/coincontrol.h"
#include "wallet/fees.h"
#include "wallet/wallet.h"
#include "wallet/rpcwallet.h"
#endif

#include <set>

#include <univalue.h>

std::string AssetAuthActivationWarning()
{
    return AreAssetAuthDeployed() ? "" : "\nTHIS COMMAND IS NOT YET ACTIVE! P2AH (pay-to-asset-hash) has not been activated on this network.\n";
}

/**
 * Used by createassetauthaddress / addassetauthaddress:
 * Parses and validates (nrequired, [owner asset names]) into a canonical preimage
 */
static CAssetAuthPreimage _createassetauth_preimage(const UniValue& params)
{
    int nRequired = params[0].get_int();
    const UniValue& names = params[1].get_array();

    if (nRequired < 1)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "a P2AH address must require at least one owner asset to authorize spends");
    if ((int)names.size() < nRequired)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("not enough owner assets supplied (got %u assets, but need at least %d to authorize)", names.size(), nRequired));
    if (names.size() > MAX_ASSET_AUTH_NAMES)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("number of owner assets in a P2AH address can't be larger than %d", MAX_ASSET_AUTH_NAMES));

    std::vector<std::string> vNames;
    for (unsigned int i = 0; i < names.size(); i++) {
        std::string name = names[i].get_str();
        if (!IsAssetNameAnOwner(name))
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                strprintf("%s is not a valid owner asset name (owner asset names end with '%s')", name, OWNER_TAG));
        vNames.push_back(name);
    }

    // Canonicalize: sort ascending and reject duplicates so a given set of names
    // always produces the same preimage and address
    std::sort(vNames.begin(), vNames.end());
    for (size_t i = 1; i < vNames.size(); i++) {
        if (vNames[i] == vNames[i - 1])
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("duplicate owner asset name: %s", vNames[i]));
    }

    CAssetAuthPreimage preimage((uint8_t)nRequired, vNames);

    std::string strError;
    if (!preimage.IsValid(strError))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strError);

    return preimage;
}

static std::vector<unsigned char> SerializePreimage(const CAssetAuthPreimage& preimage)
{
    CDataStream ssPreimage(SER_NETWORK, PROTOCOL_VERSION);
    ssPreimage << preimage;
    return std::vector<unsigned char>(ssPreimage.begin(), ssPreimage.end());
}

static UniValue PreimageToUniValue(const CAssetAuthPreimage& preimage)
{
    UniValue result(UniValue::VOBJ);
    CAssetAuthID id(preimage.GetHash());
    std::vector<unsigned char> vchPreimage = SerializePreimage(preimage);

    result.push_back(Pair("address", EncodeDestination(id)));
    result.push_back(Pair("hash", id.GetHex()));
    result.push_back(Pair("preimage", HexStr(vchPreimage)));
    result.push_back(Pair("nrequired", preimage.nRequired));
    result.push_back(Pair("total", (int)preimage.vOwnerAssetNames.size()));

    UniValue assets(UniValue::VARR);
    for (const std::string& name : preimage.vOwnerAssetNames)
        assets.push_back(name);
    result.push_back(Pair("owner_assets", assets));

    return result;
}

UniValue createassetauthaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "createassetauthaddress nrequired [\"owner_asset\",...]\n"
            + AssetAuthActivationWarning() +
            "\nCreates a pay-to-asset-hash (P2AH) address that requires nrequired of the given owner assets\n"
            "to move through any transaction that spends from it. Does not modify the wallet.\n"
            "\nKEEP THE RETURNED PREIMAGE: it is required to spend from the address.\n"

            "\nArguments:\n"
            "1. nrequired        (numeric, required) The number of owner assets that must move in the spending transaction\n"
            "2. \"owner_assets\"   (array, required) A json array of owner asset names (each must end with '!')\n"
            "     [\n"
            "       \"asset_name!\"   (string) owner asset name\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"address\":\"address\",        (string) The P2AH address\n"
            "  \"hash\":\"hex\",               (string) The hash160 of the preimage\n"
            "  \"preimage\":\"hex\",           (string) The serialized preimage. KEEP THIS - it is required to spend\n"
            "  \"nrequired\": n,             (numeric) Number of owner assets that must move to authorize a spend\n"
            "  \"total\": n,                 (numeric) Total number of owner assets committed to\n"
            "  \"owner_assets\": [...]       (array) The canonical (sorted) owner asset names\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("createassetauthaddress", "1 \"[\\\"MYASSET!\\\"]\"")
            + HelpExampleCli("createassetauthaddress", "2 \"[\\\"ALPHA!\\\",\\\"BETA!\\\",\\\"GAMMA!\\\"]\"")
            + HelpExampleRpc("createassetauthaddress", "2, \"[\\\"ALPHA!\\\",\\\"BETA!\\\",\\\"GAMMA!\\\"]\"")
        );

    CAssetAuthPreimage preimage = _createassetauth_preimage(request.params);
    return PreimageToUniValue(preimage);
}

UniValue getassetauthinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "getassetauthinfo \"address_or_hex\"\n"
            + AssetAuthActivationWarning() +
            "\nDecodes a P2AH address or a hex-encoded P2AH preimage.\n"
            "\nIf an address is given, the preimage is looked up in the wallet (if available).\n"
            "If a hex preimage is given, it is decoded directly.\n"

            "\nArguments:\n"
            "1. \"address_or_hex\"   (string, required) A P2AH address or hex-encoded preimage\n"

            "\nResult (preimage known):\n"
            "{\n"
            "  \"address\":\"address\",        (string) The P2AH address\n"
            "  \"hash\":\"hex\",               (string) The hash160 of the preimage\n"
            "  \"known\": true,              (boolean) Whether the preimage is known\n"
            "  \"preimage\":\"hex\",           (string) The serialized preimage\n"
            "  \"nrequired\": n,             (numeric) Number of owner assets that must move to authorize a spend\n"
            "  \"total\": n,                 (numeric) Total number of owner assets committed to\n"
            "  \"owner_assets\": [...]       (array) The owner asset names\n"
            "}\n"
            "\nResult (preimage unknown):\n"
            "{\n"
            "  \"address\":\"address\",        (string) The P2AH address\n"
            "  \"hash\":\"hex\",               (string) The committed hash\n"
            "  \"known\": false              (boolean) The preimage is not known to this node\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getassetauthinfo", "\"address\"")
            + HelpExampleRpc("getassetauthinfo", "\"hexpreimage\"")
        );

    std::string param = request.params[0].get_str();

    // Case 1: hex preimage
    if (IsHex(param)) {
        std::vector<unsigned char> vchPreimage = ParseHex(param);
        std::string strFramingError;
        if (!AssetAuthPreimageFramingValid(vchPreimage, strFramingError))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Failed to decode hex as a P2AH preimage: %s", strFramingError));
        CDataStream ssPreimage(vchPreimage, SER_NETWORK, PROTOCOL_VERSION);
        CAssetAuthPreimage preimage;
        try {
            ssPreimage >> preimage;
        } catch (const std::exception&) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Failed to decode hex as a P2AH preimage");
        }

        std::string strError;
        if (!preimage.IsValid(strError))
            throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Decoded preimage is not valid: %s", strError));

        UniValue result = PreimageToUniValue(preimage);
        result.push_back(Pair("known", true));
        return result;
    }

    // Case 2: P2AH address
    CTxDestination dest = DecodeDestination(param);
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid address or hex preimage: ") + param);

    const CAssetAuthID* assetAuthID = boost::get<CAssetAuthID>(&dest);
    if (!assetAuthID)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Not a P2AH address: ") + param);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("address", param));
    result.push_back(Pair("hash", assetAuthID->GetHex()));

#ifdef ENABLE_WALLET
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (pwallet) {
        std::vector<unsigned char> vchPreimage;
        if (pwallet->GetAssetAuthPreimage(*assetAuthID, vchPreimage)) {
            CAssetAuthPreimage preimage;
            std::string strStrictError;
            if (AssetAuthPreimageStrictFromRaw(vchPreimage, preimage, strStrictError)) {
                UniValue full = PreimageToUniValue(preimage);
                full.push_back(Pair("known", true));
                return full;
            }
            // fall through to unknown on strict-validation failure
        }
    }
#endif

    result.push_back(Pair("known", false));
    return result;
}

UniValue verifyassetauth(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "verifyassetauth \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\"},...] )\n"
            + AssetAuthActivationWarning() +
            "\nVerifies the P2AH (pay-to-asset-hash) authorization of a raw transaction.\n"
            "\nFor each P2AH input, reports whether the revealed preimage matches the committed hash and\n"
            "whether enough of the committed owner assets move through the transaction to authorize the spend.\n"
            "\nThis checks the authorization graph only. It does not verify signatures, fees or value\n"
            "conservation, and a \"valid\" result does not mean the transaction would be accepted into the\n"
            "mempool. Inputs provided through prevtxs override the chain and mempool view for those\n"
            "prevouts, so the result is only as accurate as the supplied context.\n"
            "\nThe transaction's inputs are looked up in the UTXO set and mempool. Inputs that are not found\n"
            "there can be provided through the prevtxs parameter.\n"

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The hex string of the raw transaction\n"
            "2. \"prevtxs\"       (array, optional) An array of previous dependent transaction outputs\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",            (string, required) The transaction id\n"
            "         \"vout\":n,               (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",  (string, required) The output script\n"
            "         \"amount\": value         (numeric, optional) The amount spent\n"
            "       }\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"valid\": true|false,          (boolean) Whether every P2AH input in the transaction is authorized\n"
            "  \"active\": true|false,         (boolean) Whether the P2AH deployment is active\n"
            "  \"inputs\": [                   (array) Details for each P2AH input\n"
            "    {\n"
            "      \"vin\": n,                 (numeric) The input index\n"
            "      \"txid\": \"id\",             (string) The previous transaction id\n"
            "      \"vout\": n,                (numeric) The previous output index\n"
            "      \"nrequired\": n,           (numeric) Owner assets required to move\n"
            "      \"total\": n,               (numeric) Total owner assets committed to\n"
            "      \"owner_assets\": [...],    (array) The committed owner asset names\n"
            "      \"moved\": [...],           (array) The committed owner assets that move in this transaction\n"
            "      \"authorized\": true|false  (boolean) Whether this input is authorized\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("verifyassetauth", "\"hexstring\"")
            + HelpExampleRpc("verifyassetauth", "\"hexstring\"")
        );

    ObserveSafeMode();

    CMutableTransaction mtx;
    if (!DecodeHexTx(mtx, request.params[0].get_str(), true))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    CTransaction tx(mtx);

    // Build a view of the inputs from the UTXO set, mempool, and any provided prevtxs
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK2(cs_main, mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const CTxIn& txin : tx.vin) {
            view.AccessCoin(txin.prevout); // Load entries from viewChain into view; can fail.
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    // Overlay user-provided prevouts
    if (request.params.size() > 1 && !request.params[1].isNull()) {
        UniValue prevTxs = request.params[1].get_array();
        for (unsigned int idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();
            RPCTypeCheckObj(prevOut,
                {
                    {"txid", UniValueType(UniValue::VSTR)},
                    {"vout", UniValueType(UniValue::VNUM)},
                    {"scriptPubKey", UniValueType(UniValue::VSTR)},
                });

            uint256 txid = ParseHashO(prevOut, "txid");
            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            COutPoint out(txid, nOut);
            std::vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            Coin newcoin;
            newcoin.out.scriptPubKey = scriptPubKey;
            newcoin.out.nValue = 0;
            if (prevOut.exists("amount")) {
                newcoin.out.nValue = AmountFromValue(find_value(prevOut, "amount"));
            }
            newcoin.nHeight = 1;
            view.AddCoin(out, std::move(newcoin), true);
        }
    }

    // Make sure all inputs are available; report which are missing
    UniValue inputs(UniValue::VARR);
    bool fAllInputsAvailable = true;
    for (size_t i = 0; i < tx.vin.size(); i++) {
        const Coin& coin = view.AccessCoin(tx.vin[i].prevout);
        if (coin.IsSpent()) {
            fAllInputsAvailable = false;
            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("vin", (int)i));
            input.push_back(Pair("txid", tx.vin[i].prevout.hash.GetHex()));
            input.push_back(Pair("vout", (int)tx.vin[i].prevout.n));
            input.push_back(Pair("error", "input not found in UTXO set, mempool, or prevtxs"));
            inputs.push_back(input);
        }
    }

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("active", AreAssetAuthDeployed()));

    if (!fAllInputsAvailable) {
        result.push_back(Pair("valid", false));
        result.push_back(Pair("inputs", inputs));
        return result;
    }

    // Run the same authorization check that consensus runs
    std::string strError;
    std::vector<CAssetAuthInputInfo> vInfo;
    bool fValid = CheckTxAssetAuthInputs(tx, view, strError, &vInfo);

    for (const auto& info : vInfo) {
        UniValue input(UniValue::VOBJ);
        input.push_back(Pair("vin", (int)info.nIndex));
        input.push_back(Pair("txid", tx.vin[info.nIndex].prevout.hash.GetHex()));
        input.push_back(Pair("vout", (int)tx.vin[info.nIndex].prevout.n));
        input.push_back(Pair("nrequired", info.preimage.nRequired));
        input.push_back(Pair("total", (int)info.preimage.vOwnerAssetNames.size()));

        UniValue assets(UniValue::VARR);
        for (const std::string& name : info.preimage.vOwnerAssetNames)
            assets.push_back(name);
        input.push_back(Pair("owner_assets", assets));

        UniValue moved(UniValue::VARR);
        for (const std::string& name : info.vAuthorizingAssets)
            moved.push_back(name);
        input.push_back(Pair("moved", moved));

        input.push_back(Pair("authorized", info.fAuthorized));
        inputs.push_back(input);
    }

    result.push_back(Pair("valid", fValid));
    if (!fValid && !strError.empty())
        result.push_back(Pair("error", strError));
    result.push_back(Pair("inputs", inputs));
    return result;
}

#ifdef ENABLE_WALLET

UniValue addassetauthaddress(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3)
        throw std::runtime_error(
            "addassetauthaddress nrequired [\"owner_asset\",...] ( \"account\" )\n"
            + AssetAuthActivationWarning() +
            "\nCreates a pay-to-asset-hash (P2AH) address, stores the preimage in the wallet, and starts\n"
            "watching the address so its UTXOs are tracked. Returns the same information as createassetauthaddress.\n"

            "\nArguments:\n"
            "1. nrequired        (numeric, required) The number of owner assets that must move in the spending transaction\n"
            "2. \"owner_assets\"   (array, required) A json array of owner asset names (each must end with '!')\n"
            "     [\n"
            "       \"asset_name!\"   (string) owner asset name\n"
            "       ,...\n"
            "     ]\n"
            "3. \"account\"        (string, optional) DEPRECATED. An account to assign the address to\n"

            "\nResult:\n"
            "{\n"
            "  \"address\":\"address\",        (string) The P2AH address\n"
            "  \"hash\":\"hex\",               (string) The hash160 of the preimage\n"
            "  \"preimage\":\"hex\",           (string) The serialized preimage (also stored in the wallet)\n"
            "  \"nrequired\": n,             (numeric) Number of owner assets that must move to authorize a spend\n"
            "  \"total\": n,                 (numeric) Total number of owner assets committed to\n"
            "  \"owner_assets\": [...]       (array) The canonical (sorted) owner asset names\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("addassetauthaddress", "1 \"[\\\"MYASSET!\\\"]\"")
            + HelpExampleRpc("addassetauthaddress", "2, \"[\\\"ALPHA!\\\",\\\"BETA!\\\",\\\"GAMMA!\\\"]\"")
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    std::string strAccount;
    if (request.params.size() > 2 && !request.params[2].isNull()) {
        strAccount = request.params[2].get_str();
        if (strAccount == "*")
            throw JSONRPCError(RPC_WALLET_INVALID_ACCOUNT_NAME, "Invalid account name");
    }

    CAssetAuthPreimage preimage = _createassetauth_preimage(request.params);
    std::vector<unsigned char> vchPreimage = SerializePreimage(preimage);
    CAssetAuthID id(preimage.GetHash());

    // Store the preimage so the wallet can spend from this address later
    if (!pwallet->AddAssetAuthPreimage(vchPreimage))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to store P2AH preimage in wallet");

    // Watch the base script so the wallet records UTXOs sent to this address
    CScript script = GetScriptForDestination(id);
    if (!pwallet->HaveWatchOnly(script)) {
        if (!pwallet->AddWatchOnly(script, 0))
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to add P2AH address to wallet watch list");
    }

    pwallet->SetAddressBook(id, strAccount, "send");

    return PreimageToUniValue(preimage);
}

UniValue listassetauthutxos(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "listassetauthutxos \"address\"\n"
            + AssetAuthActivationWarning() +
            "\nLists the UTXOs held at a P2AH address that this wallet is watching.\n"
            "The address must have been added with addassetauthaddress.\n"

            "\nArguments:\n"
            "1. \"address\"   (string, required) The P2AH address\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\": \"id\",          (string) The transaction id\n"
            "    \"vout\": n,             (numeric) The output index\n"
            "    \"amount\": x.xxx,       (numeric) The RVN amount\n"
            "    \"confirmations\": n,    (numeric) The number of confirmations\n"
            "    \"asset\": {             (object, optional) Asset held at this output, if any\n"
            "      \"name\": \"name\",      (string) The asset name\n"
            "      \"amount\": x.xxx      (numeric) The asset amount\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n"
            + HelpExampleCli("listassetauthutxos", "\"address\"")
            + HelpExampleRpc("listassetauthutxos", "\"address\"")
        );

    ObserveSafeMode();
    LOCK2(cs_main, pwallet->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    const CAssetAuthID* assetAuthID = boost::get<CAssetAuthID>(&dest);
    if (!assetAuthID)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Not a P2AH address");

    UniValue results(UniValue::VARR);

    for (const auto& entry : pwallet->mapWallet) {
        const CWalletTx& wtx = entry.second;
        if (wtx.IsCoinBase() && wtx.GetBlocksToMaturity() > 0)
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < 0)
            continue;

        for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
            const CTxOut& txout = wtx.tx->vout[i];

            CTxDestination outDest;
            if (!ExtractDestination(txout.scriptPubKey, outDest))
                continue;

            const CAssetAuthID* outID = boost::get<CAssetAuthID>(&outDest);
            if (!outID || *outID != *assetAuthID)
                continue;

            if (pwallet->IsSpent(entry.first, i))
                continue;

            UniValue utxo(UniValue::VOBJ);
            utxo.push_back(Pair("txid", entry.first.GetHex()));
            utxo.push_back(Pair("vout", (int)i));
            utxo.push_back(Pair("amount", ValueFromAmount(txout.nValue)));
            utxo.push_back(Pair("confirmations", nDepth));

            // Report any asset held at this output
            if (txout.scriptPubKey.IsAssetScript()) {
                std::string strName;
                CAmount nAmount;
                if (GetAssetInfoFromScript(txout.scriptPubKey, strName, nAmount)) {
                    UniValue asset(UniValue::VOBJ);
                    asset.push_back(Pair("name", strName));
                    asset.push_back(Pair("amount", ValueFromAmount(nAmount)));
                    utxo.push_back(Pair("asset", asset));
                }
            }

            results.push_back(utxo);
        }
    }

    return results;
}

#ifdef ENABLE_WALLET

/** UTXO held at a watched P2AH address */
struct P2AHUtxo {
    COutPoint outpoint;
    CTxOut txout;
    std::string assetName; // empty if RVN-only
    CAmount assetAmount;
};

/** Key-held owner asset used as an authorization root */
struct AuthKeyInput {
    std::string assetName;
    COutput output;
};

/** Watched P2AH input spent to authorize a chained spend; owner token returns to this P2AH */
struct AuthP2AHInput {
    COutPoint outpoint;
    CAssetAuthPreimage preimage;
    CTxDestination p2ahDest;
    std::string assetReturned; // owner asset held at this P2AH that must move back
};

/** Maximum chained-P2AH resolution depth and total candidate attempts.
 *  Both bounds only guard pathological wallet states; realistic chains are tiny. */
static const unsigned int MAX_ASSET_AUTH_RESOLVE_DEPTH = 256;
static const unsigned long MAX_ASSET_AUTH_RESOLVE_STEPS = 8192;

/** Internal state for the bounded-backtracking resolver */
struct ResolveCtx {
    CWallet* pwallet;
    const std::map<std::string, std::vector<COutput> >& mapAssetCoins;
    std::set<COutPoint> usedP2AH;
    std::set<std::pair<uint256, unsigned int> > usedKey;
    // Names already secured by selected key inputs. Consensus counts names in
    // its authorizer set, so one moved owner token satisfies EVERY pending that
    // names it; the resolver models that with this name-level set instead of
    // blocking on per-outpoint consumption.
    std::set<std::string> availableRootNames;
    unsigned long steps;
};

/** One candidate P2AH holding of an owner asset */
struct P2AHOwnerCandidate {
    COutPoint outpoint;
    CAssetAuthPreimage preimage;   // preimage required to spend that holding
    CTxDestination p2ahDest;
};

/** Enumerate every unused watched-P2AH holding of `name` (wallet must know each preimage) */
static std::vector<P2AHOwnerCandidate> CollectP2AHOwnerCandidates(CWallet* pwallet, const std::string& ownerName,
    const std::set<COutPoint>& usedOutpoints)
{
    std::vector<P2AHOwnerCandidate> out;
    for (const auto& entry : pwallet->mapWallet) {
        const CWalletTx& wtx = entry.second;
        if (wtx.IsCoinBase() && wtx.GetBlocksToMaturity() > 0)
            continue;
        if (wtx.GetDepthInMainChain() < 0)
            continue;

        for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
            if (pwallet->IsSpent(entry.first, i))
                continue;

            const CTxOut& txout = wtx.tx->vout[i];
            if (!txout.scriptPubKey.IsAssetScript())
                continue;

            std::string strName;
            CAmount nAmount;
            if (!GetAssetInfoFromScript(txout.scriptPubKey, strName, nAmount))
                continue;
            if (!IsAssetNameAnOwner(strName) || strName != ownerName)
                continue;

            CTxDestination outDest;
            if (!ExtractDestination(txout.scriptPubKey, outDest))
                continue;
            const CAssetAuthID* outID = boost::get<CAssetAuthID>(&outDest);
            if (!outID)
                continue;

            std::vector<unsigned char> vchPreimage;
            if (!pwallet->GetAssetAuthPreimage(*outID, vchPreimage))
                continue;

            CAssetAuthPreimage candidate;
            std::string strStrictError;
            if (!AssetAuthPreimageStrictFromRaw(vchPreimage, candidate, strStrictError))
                continue;

            COutPoint outpoint(entry.first, i);
            if (usedOutpoints.count(outpoint))
                continue;

            out.push_back(P2AHOwnerCandidate{outpoint, candidate, outDest});
        }
    }
    return out;
}

// Forward declaration
static bool ResolveRequirements(ResolveCtx& ctx, const std::vector<std::string>& vNames,
    uint8_t nRequired, std::vector<AuthKeyInput>& keyInputs,
    std::vector<AuthP2AHInput>& p2ahInputs, unsigned int nDepth);

/**
 * Satisfy a single owner-asset name. All-or-nothing: on failure the resolver
 * state is left exactly as it was, so the caller can safely skip to the next
 * name (any-m-of-n semantics).
 *
 * Candidate order preserves the author's design preference: chained P2AH
 * holdings first, key-held roots second. Every alternative is attempted with
 * rollback until one commits or the list is exhausted.
 */
static bool TrySatisfyName(ResolveCtx& ctx, const std::string& ownerName,
    std::vector<AuthKeyInput>& keyInputs, std::vector<AuthP2AHInput>& p2ahInputs,
    unsigned int nDepth)
{
    if (++ctx.steps > MAX_ASSET_AUTH_RESOLVE_STEPS)
        return false;

    // A root already secured by another selection satisfies this name without
    // consuming anything new (consensus set semantics).
    if (ctx.availableRootNames.count(ownerName))
        return true;

    // ---- Chained P2AH candidates (author's preferred path) ----
    for (const P2AHOwnerCandidate& cand : CollectP2AHOwnerCandidates(ctx.pwallet, ownerName, ctx.usedP2AH)) {
        if (++ctx.steps > MAX_ASSET_AUTH_RESOLVE_STEPS)
            return false;

        // snapshot for rollback
        std::set<COutPoint> snapUsedP2AH = ctx.usedP2AH;
        std::set<std::pair<uint256, unsigned int> > snapUsedKey = ctx.usedKey;
        std::set<std::string> snapRoots = ctx.availableRootNames;
        size_t snapKeySize = keyInputs.size();
        size_t snapP2AHSize = p2ahInputs.size();

        ctx.usedP2AH.insert(cand.outpoint);

        std::vector<AuthKeyInput> subKeys;
        std::vector<AuthP2AHInput> subP2ah;
        if (ResolveRequirements(ctx, cand.preimage.vOwnerAssetNames, cand.preimage.nRequired,
                                subKeys, subP2ah, nDepth + 1)) {
            keyInputs.insert(keyInputs.end(), subKeys.begin(), subKeys.end());
            p2ahInputs.insert(p2ahInputs.end(), subP2ah.begin(), subP2ah.end());
            AuthP2AHInput link;
            link.outpoint = cand.outpoint;
            link.preimage = cand.preimage;
            link.p2ahDest = cand.p2ahDest;
            link.assetReturned = ownerName;
            p2ahInputs.push_back(link);
            // The moved token joins the authorizer set for every other
            // requirement, mirroring the consensus fixpoint.
            ctx.availableRootNames.insert(ownerName);
            return true;
        }

        // rollback and try the next candidate
        ctx.usedP2AH = snapUsedP2AH;
        ctx.usedKey = snapUsedKey;
        ctx.availableRootNames = snapRoots;
        keyInputs.erase(keyInputs.begin() + static_cast<long>(snapKeySize), keyInputs.end());
        p2ahInputs.erase(p2ahInputs.begin() + static_cast<long>(snapP2AHSize), p2ahInputs.end());
    }

    // ---- Key-held roots ----
    auto it = ctx.mapAssetCoins.find(ownerName);
    if (it != ctx.mapAssetCoins.end()) {
        for (const COutput& out : it->second) {
            if (++ctx.steps > MAX_ASSET_AUTH_RESOLVE_STEPS)
                return false;
            // Tokens held at P2AH outputs are handled by the chained branch.
            if (out.tx->tx->vout[out.i].scriptPubKey.IsAssetAuthScript())
                continue;
            // Only signature-spendable coins can serve as roots.
            if (!out.fSpendable)
                continue;
            auto id = std::make_pair(out.tx->GetHash(), out.i);
            if (!ctx.usedKey.insert(id).second)
                continue;
            keyInputs.push_back({ownerName, out});
            ctx.availableRootNames.insert(ownerName);
            return true;
        }
    }
    return false;
}

/**
 * Any-m-of-n requirement solver: tries every name independently and succeeds
 * when at least `nRequired` of them are satisfiable, matching the consensus
 * fixpoint (which counts satisfied names, not a prefix).
 */
static bool ResolveRequirements(ResolveCtx& ctx, const std::vector<std::string>& vNames,
    uint8_t nRequired, std::vector<AuthKeyInput>& keyInputs,
    std::vector<AuthP2AHInput>& p2ahInputs, unsigned int nDepth)
{
    if (nDepth > MAX_ASSET_AUTH_RESOLVE_DEPTH)
        return false;

    uint8_t nFound = 0;
    for (const std::string& ownerName : vNames) {
        if (nFound >= nRequired)
            break;
        if (TrySatisfyName(ctx, ownerName, keyInputs, p2ahInputs, nDepth))
            nFound++;
        // A failed name leaves the state untouched; simply continue with the
        // next name (this is what makes any-m-of-n complete).
    }
    return nFound >= nRequired;
}

/**
 * Select inputs that satisfy a P2AH preimage's authorization requirement.
 * Bounded backtracking over chained P2AH holdings and key-held roots with
 * consensus-parity any-m-of-n counting, shared-root reuse, and full rollback.
 * On success the consumed sets are exported via usedP2AH/usedKey so callers can
 * exclude these outpoints from target selection.
 */
static bool ResolveAssetAuth(CWallet* pwallet,
    const std::map<std::string, std::vector<COutput> >& mapAssetCoins,
    const CAssetAuthPreimage& preimage, std::vector<AuthKeyInput>& keyInputs,
    std::vector<AuthP2AHInput>& p2ahInputs, std::set<COutPoint>& usedP2AH,
    std::set<std::pair<uint256, unsigned int> >& usedKey)
{
    ResolveCtx ctx{pwallet, mapAssetCoins, {}, {}, {}, 0};

    if (!ResolveRequirements(ctx, preimage.vOwnerAssetNames, preimage.nRequired,
                             keyInputs, p2ahInputs, 0)) {
        // Roll back everything so a failed resolution has no side effects.
        keyInputs.clear();
        p2ahInputs.clear();
        return false;
    }

    usedP2AH = ctx.usedP2AH;
    usedKey = ctx.usedKey;
    return true;
}

#endif // ENABLE_WALLET

UniValue spendassetauth(const JSONRPCRequest& request)
{
    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!EnsureWalletIsAvailable(pwallet, request.fHelp)) {
        return NullUniValue;
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 4)
        throw std::runtime_error(
            "spendassetauth \"from_address\" outputs ( \"preimage\" \"change_address\" )\n"
            + AssetAuthActivationWarning() +
            "\nSpends UTXOs held at a P2AH (pay-to-asset-hash) address.\n"
            "\nThe wallet automatically selects the owner asset UTXO(s) needed to authorize the spend.\n"
            "Key-held authorization roots are moved to fresh addresses. When an owner token is held at a\n"
            "parent P2AH address (chained authorization), that parent P2AH input is spent and the token is\n"
            "returned to the same parent P2AH address. The wallet must hold at least nrequired of the owner\n"
            "assets (or be able to reach them through a watched P2AH chain).\n"

            "\nArguments:\n"
            "1. \"from_address\"     (string, required) The P2AH address to spend from\n"
            "2. \"outputs\"          (object, required) The outputs to create\n"
            "    {\n"
            "      \"address\": x.xxx,                      (numeric) RVN amount to send to the address\n"
            "      \"address\": {\"transfer\":{\"NAME\":qty}}   (object) asset amount to send to the address\n"
            "      ,...\n"
            "    }\n"
            "3. \"preimage\"         (string, optional) The hex preimage. Required if not stored in the wallet.\n"
            "                             A supplied preimage is stored in the wallet so future spends can use it\n"
            "4. \"change_address\"   (string, optional) Address for RVN/asset change. Defaults to the P2AH address itself\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\": \"id\",                      (string) The transaction id\n"
            "  \"owner_assets_moved\": [...],       (array) The owner assets used to authorize the spend\n"
            "  \"owner_asset_destinations\": [...], (array) Where each moved owner asset was sent (fresh key or parent P2AH)\n"
            "  \"fee\": x.xxx                       (numeric) The transaction fee\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("spendassetauth", "\"p2ah_address\" \"{\\\"destination_address\\\": 5.0}\"")
            + HelpExampleCli("spendassetauth", "\"p2ah_address\" \"{\\\"destination_address\\\": {\\\"transfer\\\": {\\\"SOMEASSET\\\": 100}}}\"")
            + HelpExampleRpc("spendassetauth", "\"p2ah_address\", {\"destination_address\": 5.0}")
        );

    ObserveSafeMode();
    if (!AreAssetAuthDeployed())
        throw JSONRPCError(RPC_INVALID_REQUEST,
            "P2AH (pay-to-asset-hash) is not active on this network; spends of P2AH outputs are not permitted yet");
    LOCK2(cs_main, pwallet->cs_wallet);
    EnsureWalletIsUnlocked(pwallet);

    // ---- Parse the P2AH address ----
    std::string strFromAddress = request.params[0].get_str();
    CTxDestination fromDest = DecodeDestination(strFromAddress);
    const CAssetAuthID* assetAuthID = boost::get<CAssetAuthID>(&fromDest);
    if (!assetAuthID)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Not a P2AH address: ") + strFromAddress);

    // ---- Resolve the preimage ----
    std::vector<unsigned char> vchPreimage;
    if (request.params.size() > 2 && !request.params[2].isNull() && !request.params[2].get_str().empty()) {
        vchPreimage = ParseHexV(request.params[2], "preimage");
        if (Hash160(vchPreimage) != *assetAuthID)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Provided preimage does not hash to the P2AH address");
    } else if (!pwallet->GetAssetAuthPreimage(*assetAuthID, vchPreimage)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Preimage not found in wallet. Provide the preimage parameter or use addassetauthaddress first");
    }

    CAssetAuthPreimage preimage;
    std::string strStrictError;
    if (!AssetAuthPreimageStrictFromRaw(vchPreimage, preimage, strStrictError))
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Failed to decode P2AH preimage: %s", strStrictError));

    // Make sure the preimage is in the wallet keystore so ProduceSignature can
    // find it when signing. Persist only when missing: the walletdb write uses
    // DB_NOOVERWRITE, so rewriting an identical preimage would spuriously fail.
    if (!pwallet->HaveAssetAuthPreimage(*assetAuthID)) {
        if (!pwallet->AddAssetAuthPreimage(vchPreimage))
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to persist the P2AH preimage to the wallet");
    }

    // ---- Parse change address ----
    CTxDestination changeDest = fromDest; // default: change goes back to the P2AH address
    if (request.params.size() > 3 && !request.params[3].isNull() && !request.params[3].get_str().empty()) {
        changeDest = DecodeDestination(request.params[3].get_str());
        if (!IsValidDestination(changeDest))
            throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid change address: ") + request.params[3].get_str());
    }

    // ---- Parse outputs ----
    UniValue outputs = request.params[1].get_obj();
    std::vector<CTxOut> vDestOuts;
    CAmount nTotalRvnOut = 0;
    std::map<std::string, CAmount> mapAssetsOut; // asset name -> total amount requested

    for (const std::string& name_ : outputs.getKeys()) {
        CTxDestination destination = DecodeDestination(name_);
        if (!IsValidDestination(destination))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Raven address: ") + name_);

        CScript scriptPubKey = GetScriptForDestination(destination);
        const UniValue& value = outputs[name_];

        if (value.isNum() || value.isStr()) {
            // Plain RVN output
            CAmount nAmount = AmountFromValue(value);
            vDestOuts.push_back(CTxOut(nAmount, scriptPubKey));
            nTotalRvnOut += nAmount;
        } else if (value.isObject()) {
            // Asset transfer output: {"transfer": {"NAME": qty}}
            UniValue obj = value.get_obj();
            if (!obj.exists("transfer"))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Output objects must contain a \"transfer\" key");

            UniValue transferObj = obj["transfer"].get_obj();
            for (const std::string& assetName : transferObj.getKeys()) {
                CAmount nAssetAmount = AmountFromValue(transferObj[assetName]);
                if (nAssetAmount <= 0)
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Asset amount must be positive");

                CScript assetScript = scriptPubKey;
                CAssetTransfer assetTransfer(assetName, nAssetAmount);
                assetTransfer.ConstructTransaction(assetScript);
                vDestOuts.push_back(CTxOut(0, assetScript));

                mapAssetsOut[assetName] += nAssetAmount;
            }
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Output values must be an amount or a transfer object");
        }
    }

    if (vDestOuts.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No outputs specified");

    // ---- Resolve authorization inputs (key roots and/or parent P2AH chain) ----
    std::map<std::string, std::vector<COutput> > mapAssetCoins;
    pwallet->AvailableAssets(mapAssetCoins, true, nullptr);

    std::vector<AuthKeyInput> vKeyAuthInputs;
    std::vector<AuthP2AHInput> vP2AHAuthInputs;
    std::set<COutPoint> usedP2AHAuth;
    std::set<std::pair<uint256, unsigned int> > usedKeyAuth;
    if (!ResolveAssetAuth(pwallet, mapAssetCoins, preimage, vKeyAuthInputs, vP2AHAuthInputs,
                          usedP2AHAuth, usedKeyAuth)) {
        std::string strNeed;
        for (const auto& name : preimage.vOwnerAssetNames)
            strNeed += (strNeed.empty() ? "" : ", ") + name;
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Wallet cannot authorize this spend. Need %d of [%s] from keys or a watched P2AH chain",
                preimage.nRequired, strNeed));
    }

    // ---- Collect P2AH UTXOs at the from address ----
    std::vector<P2AHUtxo> vP2AHRvn;
    std::vector<P2AHUtxo> vP2AHAssets;

    for (const auto& entry : pwallet->mapWallet) {
        const CWalletTx& wtx = entry.second;
        if (wtx.IsCoinBase() && wtx.GetBlocksToMaturity() > 0)
            continue;
        if (wtx.GetDepthInMainChain() < 0)
            continue;

        for (unsigned int i = 0; i < wtx.tx->vout.size(); i++) {
            const CTxOut& txout = wtx.tx->vout[i];

            CTxDestination outDest;
            if (!ExtractDestination(txout.scriptPubKey, outDest))
                continue;
            const CAssetAuthID* outID = boost::get<CAssetAuthID>(&outDest);
            if (!outID || *outID != *assetAuthID)
                continue;
            if (pwallet->IsSpent(entry.first, i))
                continue;
            // Never double-select an outpoint already consumed as an
            // authorization link; spending it twice would be invalid and
            // counting it twice would corrupt the asset/RVN accounting.
            if (usedP2AHAuth.count(COutPoint(entry.first, i)))
                continue;

            P2AHUtxo utxo;
            utxo.outpoint = COutPoint(entry.first, i);
            utxo.txout = txout;
            utxo.assetAmount = 0;

            if (txout.scriptPubKey.IsAssetScript()) {
                std::string strName;
                CAmount nAmount;
                if (GetAssetInfoFromScript(txout.scriptPubKey, strName, nAmount)) {
                    utxo.assetName = strName;
                    utxo.assetAmount = nAmount;
                    vP2AHAssets.push_back(utxo);
                    continue;
                }
            }
            vP2AHRvn.push_back(utxo);
        }
    }

    if (vP2AHRvn.empty() && vP2AHAssets.empty())
        throw JSONRPCError(RPC_WALLET_ERROR, "No spendable UTXOs found at the P2AH address (is the address being watched? use addassetauthaddress)");

    // ---- Select UTXOs at the from address (inputs added after authorization inputs) ----
    std::vector<P2AHUtxo> vTargetP2AHInputs;

    for (const auto& assetOut : mapAssetsOut) {
        CAmount nNeeded = assetOut.second;
        CAmount nGathered = 0;
        for (const auto& utxo : vP2AHAssets) {
            if (utxo.assetName != assetOut.first)
                continue;
            if (nGathered >= nNeeded)
                break;
            vTargetP2AHInputs.push_back(utxo);
            nGathered += utxo.assetAmount;
        }
        if (nGathered < nNeeded)
            throw JSONRPCError(RPC_WALLET_ERROR,
                strprintf("Not enough of asset %s at the P2AH address (need %s, have %s)",
                    assetOut.first, FormatMoney(nNeeded), FormatMoney(nGathered)));
    }

    std::sort(vP2AHRvn.begin(), vP2AHRvn.end(),
        [](const P2AHUtxo& a, const P2AHUtxo& b) { return a.txout.nValue > b.txout.nValue; });

    CAmount nRvnFromTarget = 0;
    for (const auto& utxo : vP2AHAssets)
        nRvnFromTarget += utxo.txout.nValue;

    const int nP2AHInputsEstimate = 1 + (int)vP2AHAuthInputs.size() + (int)vTargetP2AHInputs.size();
    CAmount nFeeEstimate = 10000 * (1 + (int)vDestOuts.size() + nP2AHInputsEstimate + (int)vKeyAuthInputs.size());

    size_t nRvnUtxoIdx = 0;
    while (nRvnFromTarget < nTotalRvnOut + nFeeEstimate && nRvnUtxoIdx < vP2AHRvn.size()) {
        nRvnFromTarget += vP2AHRvn[nRvnUtxoIdx].txout.nValue;
        vTargetP2AHInputs.push_back(vP2AHRvn[nRvnUtxoIdx]);
        nRvnUtxoIdx++;
    }

    // ---- Build the transaction ----
    CMutableTransaction mtx;
    CAmount nRvnIn = 0;
    std::map<std::string, CAmount> mapAssetsIn;
    std::set<std::string> setAuthAssetsMoved;

    for (const auto& keyIn : vKeyAuthInputs) {
        mtx.vin.push_back(CTxIn(COutPoint(keyIn.output.tx->GetHash(), keyIn.output.i)));
        mapAssetsIn[keyIn.assetName] += OWNER_ASSET_AMOUNT;
        setAuthAssetsMoved.insert(keyIn.assetName);
    }

    for (const auto& p2ahIn : vP2AHAuthInputs) {
        mtx.vin.push_back(CTxIn(p2ahIn.outpoint));
        if (!p2ahIn.assetReturned.empty()) {
            mapAssetsIn[p2ahIn.assetReturned] += OWNER_ASSET_AMOUNT;
            setAuthAssetsMoved.insert(p2ahIn.assetReturned);
        }
    }

    for (const auto& utxo : vTargetP2AHInputs) {
        mtx.vin.push_back(CTxIn(utxo.outpoint));
        nRvnIn += utxo.txout.nValue;
        if (!utxo.assetName.empty())
            mapAssetsIn[utxo.assetName] += utxo.assetAmount;
    }

    if (nRvnIn < nTotalRvnOut + nFeeEstimate) {
        std::vector<COutput> vAvailableCoins;
        pwallet->AvailableCoins(vAvailableCoins, true, nullptr);
        for (const COutput& out : vAvailableCoins) {
            if (nRvnIn >= nTotalRvnOut + nFeeEstimate)
                break;
            if (!out.fSpendable)
                continue;
            if (out.tx->tx->vout[out.i].scriptPubKey.IsAssetScript())
                continue;
            mtx.vin.push_back(CTxIn(COutPoint(out.tx->GetHash(), out.i)));
            nRvnIn += out.tx->tx->vout[out.i].nValue;
        }

        if (nRvnIn < nTotalRvnOut + nFeeEstimate)
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds to cover outputs and fee");
    }

    // ---- Build outputs ----
    // 1. Requested destination outputs
    for (const auto& out : vDestOuts)
        mtx.vout.push_back(out);

    // 2. Authorization roots move to fresh keys; chained tokens return to their parent P2AH
    UniValue ownerDestinations(UniValue::VARR);
    UniValue ownerAssetsMoved(UniValue::VARR);
    for (const auto& keyIn : vKeyAuthInputs) {
        CPubKey newKey;
        if (!pwallet->GetKeyFromPool(newKey))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Keypool ran out, please call keypoolrefill first");

        CScript ownerScript = GetScriptForDestination(newKey.GetID());
        CAssetTransfer ownerTransfer(keyIn.assetName, OWNER_ASSET_AMOUNT);
        ownerTransfer.ConstructTransaction(ownerScript);
        mtx.vout.push_back(CTxOut(0, ownerScript));

        ownerAssetsMoved.push_back(keyIn.assetName);
        ownerDestinations.push_back(EncodeDestination(newKey.GetID()));
    }

    for (const auto& p2ahIn : vP2AHAuthInputs) {
        if (p2ahIn.assetReturned.empty())
            continue;

        CScript ownerScript = GetScriptForDestination(p2ahIn.p2ahDest);
        CAssetTransfer ownerTransfer(p2ahIn.assetReturned, OWNER_ASSET_AMOUNT);
        ownerTransfer.ConstructTransaction(ownerScript);
        mtx.vout.push_back(CTxOut(0, ownerScript));

        ownerAssetsMoved.push_back(p2ahIn.assetReturned);
        ownerDestinations.push_back(EncodeDestination(p2ahIn.p2ahDest));
    }

    // 3. Asset change (back to the P2AH address or the change address)
    for (const auto& assetIn : mapAssetsIn) {
        if (setAuthAssetsMoved.count(assetIn.first))
            continue;

        CAmount nChange = assetIn.second - (mapAssetsOut.count(assetIn.first) ? mapAssetsOut.at(assetIn.first) : 0);
        if (nChange > 0) {
            CScript changeScript = GetScriptForDestination(changeDest);
            CAssetTransfer changeTransfer(assetIn.first, nChange);
            changeTransfer.ConstructTransaction(changeScript);
            mtx.vout.push_back(CTxOut(0, changeScript));
        }
    }

    // 4. RVN change placeholder (value set after fee calculation)
    CScript rvnChangeScript = GetScriptForDestination(changeDest);
    int nChangeOutputIndex = -1;
    if (nRvnIn > nTotalRvnOut) {
        mtx.vout.push_back(CTxOut(0, rvnChangeScript));
        nChangeOutputIndex = (int)mtx.vout.size() - 1;
    }

    // ---- Sign and size the transaction (two passes for fee accuracy) ----
    auto signTransaction = [&](CMutableTransaction& tx) -> bool {
        const CTransaction txConst(tx);
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            CTxIn& txin = tx.vin[i];

            // Find the prevout
            CTxOut prevOut;
            const auto mi = pwallet->mapWallet.find(txin.prevout.hash);
            if (mi != pwallet->mapWallet.end() && txin.prevout.n < mi->second.tx->vout.size()) {
                prevOut = mi->second.tx->vout[txin.prevout.n];
            } else {
                return false;
            }

            SignatureData sigdata;
            if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &tx, i, prevOut.nValue, SIGHASH_ALL),
                                  prevOut.scriptPubKey, sigdata))
                return false;
            UpdateTransaction(tx, i, sigdata);
        }
        return true;
    };

    // First pass: sign with placeholder change to get an accurate size
    CMutableTransaction mtxForSize = mtx;
    if (nChangeOutputIndex >= 0)
        mtxForSize.vout[nChangeOutputIndex].nValue = nRvnIn - nTotalRvnOut;
    if (!signTransaction(mtxForSize))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction (missing keys or preimage?)");

    // Compute the fee from the actual signed size, with a safety margin: ECDSA signature
    // sizes can vary by a byte per input between the sizing pass and the final signing pass
    size_t nTxBytes = GetVirtualTransactionSize(CTransaction(mtxForSize)) + mtx.vin.size() * 2;
    CAmount nFee = GetMinimumFee(nTxBytes, CCoinControl(), ::mempool, ::feeEstimator, nullptr);

    if (nRvnIn < nTotalRvnOut + nFee)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient funds to cover fee of %s", FormatMoney(nFee)));

    // Set the real change value (or drop the change output if it would be dust)
    CAmount nChange = nRvnIn - nTotalRvnOut - nFee;
    if (nChangeOutputIndex >= 0) {
        if (nChange > 546) { // dust threshold
            mtx.vout[nChangeOutputIndex].nValue = nChange;
        } else {
            mtx.vout.erase(mtx.vout.begin() + nChangeOutputIndex);
            nFee += nChange;
        }
    }

    // Final signing pass on the real transaction
    if (!signTransaction(mtx))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction (missing keys or preimage?)");

    // ---- Broadcast ----
    CWalletTx wtxNew;
    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.BindWallet(pwallet);
    wtxNew.fFromMe = true;
    wtxNew.SetTx(MakeTransactionRef(std::move(mtx)));

    CReserveKey reservekey(pwallet);
    CValidationState state;
    if (!pwallet->CommitTransaction(wtxNew, reservekey, g_connman.get(), state))
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Transaction was rejected: %s", state.GetRejectReason()));

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("txid", wtxNew.GetHash().GetHex()));
    result.push_back(Pair("owner_assets_moved", ownerAssetsMoved));
    result.push_back(Pair("owner_asset_destinations", ownerDestinations));
    result.push_back(Pair("fee", ValueFromAmount(nFee)));
    return result;
}

#endif // ENABLE_WALLET

static const CRPCCommand commands[] =
{ //  category      name                       actor (function)           argNames
  //  ------------- -------------------------  -------------------------  ----------
    { "assetauth",  "createassetauthaddress",  &createassetauthaddress,   {"nrequired", "owner_assets"} },
    { "assetauth",  "getassetauthinfo",        &getassetauthinfo,         {"address_or_hex"} },
    { "assetauth",  "verifyassetauth",         &verifyassetauth,          {"hexstring", "prevtxs"} },
#ifdef ENABLE_WALLET
    { "assetauth",  "addassetauthaddress",     &addassetauthaddress,      {"nrequired", "owner_assets", "account"} },
    { "assetauth",  "listassetauthutxos",      &listassetauthutxos,       {"address"} },
    { "assetauth",  "spendassetauth",          &spendassetauth,           {"from_address", "outputs", "preimage", "change_address"} },
#endif
};

void RegisterAssetAuthRPCCommands(CRPCTable &t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
