// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>

#include <test/test_raven.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>

#include <amount.h>
#include <script/standard.h>
#include <base58.h>
#include <consensus/validation.h>
#include <consensus/tx_verify.h>
#include <hash.h>
#include <key.h>
#include <keystore.h>
#include <policy/policy.h>
#include <script/interpreter.h>
#include <script/sign.h>
#include <txmempool.h>
#include <validation.h>
#include <wallet/wallet.h>
#ifdef ENABLE_WALLET
#include <wallet/db.h>
#endif

namespace {

CTxOut MakeAssetTransferOutput(const std::string& assetName, CAmount amount)
{
    CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    CAssetTransfer(assetName, amount).ConstructTransaction(scriptPubKey);
    return CTxOut(0, scriptPubKey);
}

void AddAssetCoin(CCoinsViewCache& coins, const COutPoint& outpoint, const std::string& assetName, CAmount amount)
{
    coins.AddCoin(outpoint, Coin(MakeAssetTransferOutput(assetName, amount), 10, false), true);
}

CScript MakeTaggedAssetTransferScript(const std::string& assetName, CAmount amount,
                                      const std::vector<unsigned char>& program)
{
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << assetName;
    payload << amount;
    payload << static_cast<unsigned char>(0x50);
    payload << std::string(program.begin(), program.end());

    std::vector<unsigned char> assetData{RVN_R, RVN_V, RVN_N, RVN_T};
    assetData.insert(assetData.end(), payload.begin(), payload.end());
    CScript script = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    script << OP_RVN_ASSET << assetData << OP_DROP;
    return script;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(asset_tx_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(check_tx_inputs_uses_candidate_enforced_values_state)
    {
        SelectParams(CBaseChainParams::MAIN);

        CCoinsView base;
        CCoinsViewCache coins(&base);
        const COutPoint input(uint256S("05"), 0);
        coins.AddCoin(input, Coin(CTxOut(COIN, CScript() << OP_TRUE), 10, false), false);

        CMutableTransaction mutableTx;
        mutableTx.vin.emplace_back(input);
        CTxOut assetOutput = MakeAssetTransferOutput("RAVENTEST", COIN);
        assetOutput.nValue = COIN + 1;
        mutableTx.vout.emplace_back(assetOutput);
        const CTransaction tx(mutableTx);

        const TxAssetDeploymentContext before{false, false, false, false, false, false};
        const TxAssetDeploymentContext after{false, true, false, false, false, false};
        CAmount fee = -1;
        CValidationState beforeState;
        BOOST_REQUIRE(Consensus::CheckTxInputs(tx, beforeState, coins, 20, fee, &before));
        BOOST_CHECK_EQUAL(fee, COIN);

        CValidationState afterState;
        BOOST_CHECK(!Consensus::CheckTxInputs(tx, afterState, coins, 20, fee, &after));
        BOOST_CHECK_EQUAL(afterState.GetRejectReason(), "bad-txns-in-belowout");
    }

    BOOST_AUTO_TEST_CASE(check_tx_assets_uses_candidate_transfer_parser_state)
    {
        SelectParams(CBaseChainParams::MAIN);
        const std::string assetName(30, 'A');
        CCoinsView base;
        CCoinsViewCache coins(&base);
        const COutPoint input(uint256S("06"), 0);
        AddAssetCoin(coins, input, assetName, COIN);

        CScript script = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        CAssetTransfer(assetName, COIN, std::string(34, 'a')).ConstructTransaction(script);
        BOOST_REQUIRE(script.size() > 75);
        CMutableTransaction mutableTx;
        mutableTx.vin.emplace_back(input);
        mutableTx.vout.emplace_back(0, script);
        const CTransaction tx(mutableTx);

        const TxAssetDeploymentContext before{false, false, false, true, false, false};
        const TxAssetDeploymentContext after{true, false, false, true, false, false};
        std::vector<std::pair<std::string, uint256>> reissues;
        CValidationState beforeState;
        BOOST_CHECK(!Consensus::CheckTxAssets(tx, beforeState, coins, nullptr, false,
                                              reissues, false, true, nullptr, 0, nullptr, &before));
        BOOST_CHECK_EQUAL(beforeState.GetRejectReason(), "bad-tx-asset-transfer-bad-deserialize");

        CValidationState afterState;
        BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(tx, afterState, coins, nullptr, false,
                                                    reissues, false, true, nullptr, 0, nullptr, &after),
                            afterState.GetRejectReason());
    }

    BOOST_AUTO_TEST_CASE(check_tx_assets_uses_candidate_messaging_state)
    {
        SelectParams(CBaseChainParams::MAIN);
        const std::string assetName = "RAVENTEST~CH";
        CCoinsView base;
        CCoinsViewCache coins(&base);
        const COutPoint input(uint256S("07"), 0);
        AddAssetCoin(coins, input, assetName, COIN);

        CMutableTransaction mutableTx;
        mutableTx.vin.emplace_back(input);
        mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, COIN));
        const CTransaction tx(mutableTx);

        const TxAssetDeploymentContext before{false, false, false, true, false, false};
        const TxAssetDeploymentContext after{false, false, false, true, true, true};
        std::vector<std::pair<std::string, uint256>> reissues;
        CValidationState beforeState;
        BOOST_CHECK(!Consensus::CheckTxAssets(tx, beforeState, coins, nullptr, false,
                                              reissues, false, true, nullptr, 0, nullptr, &before));
        BOOST_CHECK_EQUAL(beforeState.GetRejectReason(), "bad-txns-transfer-msgchannel-before-messaging-is-active");

        CValidationState afterState;
        BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(tx, afterState, coins, nullptr, false,
                                                    reissues, false, true, nullptr, 0, nullptr, &after),
                            afterState.GetRejectReason());
    }

    BOOST_AUTO_TEST_CASE(asset_destination_scope_test)
    {
        SelectParams(CBaseChainParams::MAIN);

        const CKeyID p2pkh;
        const CScriptID p2sh;
        const WitnessV2PQDestination pq(uint256S("03"));

        BOOST_CHECK(IsSupportedAssetDestination(p2pkh));
        BOOST_CHECK(!IsSupportedAssetDestination(p2sh));
        BOOST_CHECK(!IsSupportedAssetDestination(pq));
        BOOST_CHECK(!IsSupportedAssetDestination(CNoDestination()));

        BOOST_CHECK(IsSupportedNullAssetDestination(p2pkh));
        BOOST_CHECK(IsSupportedNullAssetDestination(p2sh));
        BOOST_CHECK(!IsSupportedNullAssetDestination(pq));
        BOOST_CHECK(!IsSupportedNullAssetDestination(CNoDestination()));
    }

    BOOST_AUTO_TEST_CASE(pq_asset_envelope_is_not_witness_v2_test)
    {
        SelectParams(CBaseChainParams::MAIN);

        CScript script = GetScriptForWitnessV2PQ(uint256S("03"));
        int witnessVersion = -1;
        std::vector<unsigned char> witnessProgram;
        BOOST_REQUIRE(script.IsWitnessProgram(witnessVersion, witnessProgram));
        BOOST_CHECK_EQUAL(witnessVersion, 2);

        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(script);

        int assetType = 0;
        bool isOwner = false;
        BOOST_CHECK(!script.IsWitnessProgram(witnessVersion, witnessProgram));
        BOOST_CHECK(!script.IsAssetScript(assetType, isOwner));

        CMutableTransaction mutableTx;
        mutableTx.vin.emplace_back(COutPoint(uint256S("04"), 0));
        mutableTx.vout.emplace_back(0, script);

        const CTransaction tx(mutableTx);
        CValidationState state;
        BOOST_CHECK(!CheckTransaction(tx, state));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-op-rvn-asset-not-in-right-script-location");
    }

    BOOST_AUTO_TEST_CASE(legacy_asset_parser_accepts_tagged_program_message_test)
    {
        SelectParams(CBaseChainParams::MAIN);

        const std::vector<unsigned char> program = ToByteVector(uint256S("03"));
        CScript script = MakeTaggedAssetTransferScript("RAVENTEST", COIN, program);

        int assetType = 0;
        bool isOwner = false;
        BOOST_REQUIRE(script.IsAssetScript(assetType, isOwner));
        BOOST_CHECK_EQUAL(assetType, TX_TRANSFER_ASSET);
        BOOST_CHECK(!isOwner);

        CAssetOutputEntry data;
        BOOST_REQUIRE(GetAssetData(script, data));
        BOOST_CHECK_EQUAL(data.assetName, "RAVENTEST");
        BOOST_CHECK_EQUAL(data.nAmount, COIN);
        BOOST_CHECK_EQUAL(EncodeDestination(data.destination), GetParams().GlobalBurnAddress());

        CAssetTransfer transfer;
        std::string address;
        BOOST_REQUIRE(TransferAssetFromScript(script, transfer, address));
        BOOST_CHECK_EQUAL(transfer.message.size(), program.size());
        BOOST_CHECK_EQUAL_COLLECTIONS(transfer.message.begin(), transfer.message.end(), program.begin(), program.end());
        std::string error;
        BOOST_CHECK(ContextualCheckTransferAsset(nullptr, transfer, address, error));

        CCoinsView view;
        CCoinsViewCache coins(&view);
        const COutPoint source(uint256S("04"), 0);
        AddAssetCoin(coins, source, "RAVENTEST", COIN);

        CMutableTransaction mutableTx;
        mutableTx.vin.emplace_back(source);
        mutableTx.vout.emplace_back(0, script);
        CValidationState state;
        BOOST_CHECK(CheckTransaction(CTransaction(mutableTx), state));
        std::vector<std::pair<std::string, uint256>> reissues;
        BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(CTransaction(mutableTx), state, coins,
                            nullptr, false, reissues, true, true), state.GetDebugMessage());

        int witnessVersion = -1;
        std::vector<unsigned char> witnessProgram;
        BOOST_CHECK(!script.IsWitnessProgram(witnessVersion, witnessProgram));
    }

    BOOST_AUTO_TEST_CASE(legacy_asset_parser_32_byte_tail_class_matrix_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const std::vector<unsigned char> program = ToByteVector(uint256S("03"));
        const std::vector<std::string> names = {
            "RAVENTEST", "RAVENTEST!", "RAVENTEST#ONE",
            "$RAVENTEST", "#RAVENTEST"
        };
        for (const std::string& name : names) {
            BOOST_TEST_CONTEXT(name) {
                CScript script = MakeAssetTransferOutput(name, COIN).scriptPubKey;
                BOOST_REQUIRE_EQUAL(script.back(), OP_DROP);
                script.pop_back();
                script.insert(script.end(), program.begin(), program.end());
                CAssetOutputEntry data;
                BOOST_REQUIRE(GetAssetData(script, data));
                BOOST_CHECK_EQUAL(data.assetName, name);
                BOOST_CHECK_EQUAL(data.nAmount, COIN);
                CAssetTransfer transfer;
                std::string address;
                BOOST_REQUIRE(TransferAssetFromScript(script, transfer, address));
                BOOST_CHECK(transfer.message.empty());
                BOOST_CHECK_EQUAL(transfer.nExpireTime, 0);
            }
        }
    }

    BOOST_AUTO_TEST_CASE(legacy_asset_parsers_accept_32_byte_tail_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const std::vector<unsigned char> program = ToByteVector(uint256S("03"));
        const CScript destination = GetScriptForDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        const auto replaceDropWithProgram = [&program](CScript& script) {
            BOOST_REQUIRE_EQUAL(script.back(), OP_DROP);
            script.pop_back();
            script.insert(script.end(), program.begin(), program.end());
        };

        CScript transferScript = destination;
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(transferScript);
        replaceDropWithProgram(transferScript);
        CAssetTransfer transfer;
        std::string address;
        BOOST_REQUIRE(TransferAssetFromScript(transferScript, transfer, address));
        BOOST_CHECK(transfer.message.empty());
        BOOST_CHECK_EQUAL(transfer.nExpireTime, 0);
        CAssetOutputEntry transferData;
        BOOST_REQUIRE(GetAssetData(transferScript, transferData));
        BOOST_CHECK_EQUAL(transferData.assetName, "RAVENTEST");
        txnouttype standardType = TX_NONSTANDARD;
        BOOST_CHECK(IsStandard(transferScript, standardType, true));
        BOOST_CHECK_EQUAL(standardType, TX_TRANSFER_ASSET);
        CCoinsView view;
        CCoinsViewCache coins(&view);
        const COutPoint source(uint256S("05"), 0);
        AddAssetCoin(coins, source, "RAVENTEST", COIN);
        CMutableTransaction mutableTx;
        mutableTx.vin.emplace_back(source);
        mutableTx.vout.emplace_back(0, transferScript);
        const CTransaction tx(mutableTx);
        CValidationState state;
        BOOST_REQUIRE(CheckTransaction(tx, state));
        std::vector<std::pair<std::string, uint256>> reissues;
        BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(tx, state, coins, nullptr,
                            false, reissues, true, true), state.GetDebugMessage());

        const std::string message(32, 'x');
        CScript messageScript = destination;
        CAssetTransfer("RAVENTEST", COIN, message).ConstructTransaction(messageScript);
        BOOST_REQUIRE_EQUAL(messageScript.back(), OP_DROP);
        messageScript.pop_back();
        for (int i = 0; i < 8; ++i)
            messageScript.push_back(0);
        messageScript.insert(messageScript.end(), program.begin(), program.end());
        CAssetTransfer parsedMessage;
        BOOST_REQUIRE(TransferAssetFromScript(messageScript, parsedMessage, address));
        BOOST_CHECK_EQUAL(parsedMessage.message, message);
        BOOST_CHECK_EQUAL(parsedMessage.nExpireTime, 0);

        CScript expiryScript = destination;
        CAssetTransfer("RAVENTEST", COIN, message, 123456789).ConstructTransaction(expiryScript);
        replaceDropWithProgram(expiryScript);
        CAssetTransfer parsedExpiry;
        BOOST_REQUIRE(TransferAssetFromScript(expiryScript, parsedExpiry, address));
        BOOST_CHECK_EQUAL(parsedExpiry.message, message);
        BOOST_CHECK_EQUAL(parsedExpiry.nExpireTime, 123456789);

        CScript reissueScript = destination;
        CReissueAsset("RAVENTEST", COIN, 0, 1, "").ConstructTransaction(reissueScript);
        replaceDropWithProgram(reissueScript);
        CReissueAsset reissue;
        BOOST_REQUIRE(ReissueAssetFromScript(reissueScript, reissue, address));
        BOOST_CHECK(reissue.strIPFSHash.empty());

        CScript newAssetScript = destination;
        CNewAsset("RAVENTEST", COIN, 0, 1, 0, "").ConstructTransaction(newAssetScript);
        replaceDropWithProgram(newAssetScript);
        CNewAsset newAsset;
        BOOST_REQUIRE(AssetFromScript(newAssetScript, newAsset, address));
        BOOST_CHECK_EQUAL(newAsset.strName, "RAVENTEST");

        CScript ownerScript = destination;
        CNewAsset("RAVENTEST", COIN, 0, 1, 0, "").ConstructOwnerTransaction(ownerScript);
        replaceDropWithProgram(ownerScript);
        std::string ownerName;
        BOOST_REQUIRE(OwnerAssetFromScript(ownerScript, ownerName, address));
        BOOST_CHECK_EQUAL(ownerName, "RAVENTEST!");
    }

    BOOST_AUTO_TEST_CASE(legacy_p2pkh_asset_script_accepts_32_byte_tail_spend_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        CKey key;
        key.MakeNewKey(true);
        CBasicKeyStore keystore;
        BOOST_REQUIRE(keystore.AddKey(key));

        CScript assetScript = GetScriptForDestination(key.GetPubKey().GetID());
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(assetScript);
        BOOST_REQUIRE_EQUAL(assetScript.back(), OP_DROP);
        assetScript.pop_back();
        const std::vector<unsigned char> program = ToByteVector(uint256S("03"));
        assetScript.insert(assetScript.end(), program.begin(), program.end());

        CMutableTransaction funding;
        funding.vin.emplace_back(COutPoint(uint256S("06"), 0));
        funding.vout.emplace_back(0, assetScript);
        const CTransaction funded(funding);

        CMutableTransaction spending;
        spending.vin.emplace_back(COutPoint(funded.GetHash(), 0));
        spending.vout.emplace_back(0, MakeAssetTransferOutput("RAVENTEST", COIN).scriptPubKey);
        BOOST_REQUIRE(SignSignature(keystore, funded, spending, 0, SIGHASH_ALL));
        ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;
        BOOST_CHECK(VerifyScript(spending.vin[0].scriptSig, assetScript,
                    &spending.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS,
                    MutableTransactionSignatureChecker(&spending, 0, 0), &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    }

    BOOST_AUTO_TEST_CASE(pq_asset_tail_cannot_bypass_classical_signature_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        CKey key;
        key.MakeNewKey(true);
        const CPubKey pubkey = key.GetPubKey();

        CScript assetScript = GetScriptForDestination(pubkey.GetID());
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(assetScript);
        BOOST_REQUIRE_EQUAL(assetScript.back(), OP_DROP);
        assetScript.pop_back();
        std::vector<unsigned char> program(32, 1);
        program[0] = OP_2DROP;
        program[1] = 30;
        assetScript.insert(assetScript.end(), program.begin(), program.end());
        uint256 parsed;
        BOOST_REQUIRE(GetPQAssetProgram(assetScript, parsed));

        CMutableTransaction funded;
        funded.vin.emplace_back(COutPoint(uint256S("06"), 0));
        funded.vout.emplace_back(0, assetScript);
        CMutableTransaction spending;
        spending.vin.emplace_back(COutPoint(CTransaction(funded).GetHash(), 0));
        spending.vout.emplace_back(0, MakeAssetTransferOutput("RAVENTEST", COIN).scriptPubKey);
        spending.vin[0].scriptSig = CScript() << OP_0
            << std::vector<unsigned char>(pubkey.begin(), pubkey.end());

        ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;
        BOOST_CHECK_MESSAGE(!VerifyScript(spending.vin[0].scriptSig, assetScript,
                            &spending.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS,
                            MutableTransactionSignatureChecker(&spending, 0, 0), &error),
                            "empty classical signature accepted by bit13 asset tail");
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_EVAL_FALSE);

        CBasicKeyStore keystore;
        BOOST_REQUIRE(keystore.AddKey(key));
        CMutableTransaction signedSpending = spending;
        BOOST_REQUIRE(SignSignature(keystore, CTransaction(funded), signedSpending, 0, SIGHASH_ALL));
        error = SCRIPT_ERR_UNKNOWN_ERROR;
        BOOST_CHECK(VerifyScript(signedSpending.vin[0].scriptSig, assetScript,
                    &signedSpending.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS,
                    MutableTransactionSignatureChecker(&signedSpending, 0, 0), &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

        CScript inertTailScript = assetScript;
        inertTailScript.erase(inertTailScript.end() - 32, inertTailScript.end());
        std::vector<unsigned char> inertProgram(32, 0xff);
        inertProgram[0] = OP_RETURN;
        inertTailScript.insert(inertTailScript.end(), inertProgram.begin(), inertProgram.end());
        BOOST_REQUIRE(GetPQAssetProgram(inertTailScript, parsed));
        CMutableTransaction inertFunded = funded;
        inertFunded.vout[0].scriptPubKey = inertTailScript;
        CMutableTransaction inertSpending = spending;
        inertSpending.vin[0].prevout.hash = CTransaction(inertFunded).GetHash();
        BOOST_REQUIRE(SignSignature(keystore, CTransaction(inertFunded), inertSpending, 0, SIGHASH_ALL));
        error = SCRIPT_ERR_UNKNOWN_ERROR;
        BOOST_CHECK(VerifyScript(inertSpending.vin[0].scriptSig, inertTailScript,
                    &inertSpending.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS,
                    MutableTransactionSignatureChecker(&inertSpending, 0, 0), &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);
    }

    BOOST_AUTO_TEST_CASE(pq_only_asset_prefix_is_legacy_valid_and_program_bound_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const uint256 program = uint256S("03");
        const CScript destination = GetScriptForDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        CScript legacy = destination;
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(legacy);
        CScript tagged;
        BOOST_REQUIRE(BuildPQAssetTaggedScript(legacy, program, tagged));

        const uint160 addressHash = Hash160(program.begin(), program.end());
        CScript pqOnly = CScript() << OP_1 << OP_1
            << ToByteVector(addressHash) << OP_DROP << OP_DROP;
        BOOST_REQUIRE_EQUAL(pqOnly.size(), 25U);
        pqOnly.insert(pqOnly.end(), tagged.begin() + 25, tagged.end());
        BOOST_REQUIRE(pqOnly.IsAssetScript());

        CMutableTransaction policyCandidate;
        policyCandidate.vout.emplace_back(0, pqOnly);
        BOOST_CHECK(HasPQOnlyAssetOutput(CTransaction(policyCandidate)));
        policyCandidate.vout[0].scriptPubKey = legacy;
        BOOST_CHECK(!HasPQOnlyAssetOutput(CTransaction(policyCandidate)));

        CMutableTransaction spending;
        spending.vin.emplace_back(COutPoint(uint256S("06"), 0));
        spending.vout.emplace_back(0, legacy);
        ScriptError error = SCRIPT_ERR_UNKNOWN_ERROR;
        BOOST_CHECK(VerifyScript(CScript(), pqOnly,
                    &spending.vin[0].scriptWitness, STANDARD_SCRIPT_VERIFY_FLAGS,
                    MutableTransactionSignatureChecker(&spending, 0, 0), &error));
        BOOST_CHECK_EQUAL(error, SCRIPT_ERR_OK);

        uint256 parsed;
        BOOST_REQUIRE(GetPQAssetProgram(pqOnly, parsed));
        BOOST_CHECK(parsed == program);
        CScript wrongAddress = pqOnly;
        wrongAddress[3] ^= 1;
        BOOST_CHECK(!GetPQAssetProgram(wrongAddress, parsed));
    }

    BOOST_AUTO_TEST_CASE(pq_only_asset_builder_preserves_transfer_and_owner_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const uint256 program = uint256S("03");
        const CScript destination = GetScriptForDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        const TxAssetDeploymentContext parserContext{true, false, false, true, true, true};
        const auto check = [&program, &parserContext](const CScript& legacy) {
            CScript pqOnly;
            BOOST_REQUIRE(BuildPQOnlyAssetScript(legacy, program, pqOnly));
            CAssetOutputEntry before;
            CAssetOutputEntry after;
            BOOST_REQUIRE(GetAssetData(legacy, before, &parserContext));
            BOOST_REQUIRE(GetAssetData(pqOnly, after, &parserContext));
            BOOST_CHECK_EQUAL(before.type, after.type);
            BOOST_CHECK_EQUAL(before.assetName, after.assetName);
            BOOST_CHECK_EQUAL(before.nAmount, after.nAmount);
            if (before.type == TX_TRANSFER_ASSET) {
                BOOST_CHECK_EQUAL(before.message, after.message);
                BOOST_CHECK_EQUAL(before.expireTime, after.expireTime);
            }
            const CKeyID* pseudoAddress = boost::get<CKeyID>(&after.destination);
            BOOST_REQUIRE(pseudoAddress);
            BOOST_CHECK(*pseudoAddress == CKeyID(Hash160(program.begin(), program.end())));
            uint256 parsed;
            BOOST_REQUIRE(GetPQAssetProgram(pqOnly, parsed));
            BOOST_CHECK(parsed == program);
        };
        CScript transfer = destination;
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(transfer);
        check(transfer);
        CScript owner = destination;
        CNewAsset("RAVENTEST", COIN, 0, 1, 0, "").ConstructOwnerTransaction(owner);
        check(owner);
        CScript message = destination;
        CAssetTransfer("RAVENTEST", COIN, std::string(32, 'x'))
            .ConstructTransaction(message);
        check(message);
        for (const std::string& name : {"RAVENTEST", "RAVENTEST/SUB",
                                        "RAVENTEST#ONE", "$RAVENTEST",
                                        "#RAVENTEST", "#RAVENTEST/SUB"}) {
            CScript issue = destination;
            CNewAsset(name, COIN, 0, 1, 0, "").ConstructTransaction(issue);
            check(issue);
        }
        CScript reissue = destination;
        CReissueAsset("RAVENTEST", COIN, 0, 1, "").ConstructTransaction(reissue);
        check(reissue);
    }

    BOOST_AUTO_TEST_CASE(pq_asset_program_canonical_parser_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const uint256 expected = uint256S("03");
        const std::vector<unsigned char> program = ToByteVector(expected);
        const CScript destination = GetScriptForDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        const auto addProgram = [&program](CScript script) {
            BOOST_REQUIRE_EQUAL(script.back(), OP_DROP);
            script.pop_back();
            script.insert(script.end(), program.begin(), program.end());
            return script;
        };
        const auto checkProgram = [&expected](const CScript& script) {
            uint256 parsed;
            BOOST_REQUIRE(GetPQAssetProgram(script, parsed));
            BOOST_CHECK(parsed == expected);
        };

        CScript transfer = destination;
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(transfer);
        uint256 parsed;
        BOOST_CHECK(!GetPQAssetProgram(transfer, parsed));
        transfer = addProgram(transfer);
        checkProgram(transfer);

        for (const std::string& name : {"RAVENTEST!", "RAVENTEST#ONE",
                                         "$RAVENTEST", "#RAVENTEST"}) {
            CScript assetTransfer = destination;
            CAssetTransfer(name, COIN).ConstructTransaction(assetTransfer);
            checkProgram(addProgram(assetTransfer));
        }

        CScript issue = destination;
        CNewAsset("RAVENTEST", COIN, 0, 1, 0, "").ConstructTransaction(issue);
        checkProgram(addProgram(issue));
        for (const std::string& name : {"RAVENTEST/SUB", "RAVENTEST#ONE",
                                         "$RAVENTEST", "#RAVENTEST",
                                         "#RAVENTEST/SUB"}) {
            CScript classIssue = destination;
            CNewAsset(name, COIN, 0, 1, 0, "").ConstructTransaction(classIssue);
            checkProgram(addProgram(classIssue));
        }
        CScript owner = destination;
        CNewAsset("RAVENTEST", COIN, 0, 1, 0, "").ConstructOwnerTransaction(owner);
        checkProgram(addProgram(owner));
        CScript reissue = destination;
        CReissueAsset("RAVENTEST", COIN, 0, 1, "").ConstructTransaction(reissue);
        checkProgram(addProgram(reissue));

        const std::string ipfs = std::string("\x12\x20", 2) + std::string(32, 'h');
        CScript ipfsTransfer = destination;
        CAssetTransfer("RAVENTEST", COIN, ipfs, 7).ConstructTransaction(ipfsTransfer);
        ipfsTransfer = addProgram(ipfsTransfer);
        checkProgram(ipfsTransfer);
        CAssetTransfer legacyIpfsTransfer;
        std::string address;
        BOOST_REQUIRE(TransferAssetFromScript(ipfsTransfer, legacyIpfsTransfer, address));
        BOOST_CHECK_EQUAL(legacyIpfsTransfer.message, ipfs);
        BOOST_CHECK_EQUAL(legacyIpfsTransfer.nExpireTime, 7);
        CScript ipfsIssue = destination;
        CNewAsset(std::string(30, 'A'), COIN, 0, 1, 1, ipfs)
            .ConstructTransaction(ipfsIssue);
        BOOST_REQUIRE_EQUAL(ipfsIssue[26], OP_PUSHDATA1);
        ipfsIssue = addProgram(ipfsIssue);
        checkProgram(ipfsIssue);
        CNewAsset legacyIpfsIssue;
        BOOST_REQUIRE(AssetFromScript(ipfsIssue, legacyIpfsIssue, address));
        BOOST_CHECK_EQUAL(legacyIpfsIssue.strIPFSHash, ipfs);
        CScript ipfsReissue = destination;
        CReissueAsset("RAVENTEST", COIN, 0, 1, ipfs).ConstructTransaction(ipfsReissue);
        ipfsReissue = addProgram(ipfsReissue);
        checkProgram(ipfsReissue);
        CReissueAsset legacyIpfsReissue;
        BOOST_REQUIRE(ReissueAssetFromScript(ipfsReissue, legacyIpfsReissue, address));
        BOOST_CHECK_EQUAL(legacyIpfsReissue.strIPFSHash, ipfs);

        CScript message = destination;
        CAssetTransfer("RAVENTEST", COIN, std::string(32, 'x')).ConstructTransaction(message);
        CScript noExpiry = addProgram(message);
        BOOST_CHECK(!GetPQAssetProgram(noExpiry, parsed));
        CAssetTransfer legacyNoExpiry;
        BOOST_REQUIRE(TransferAssetFromScript(noExpiry, legacyNoExpiry, address));
        BOOST_CHECK_EQUAL(legacyNoExpiry.nExpireTime, 3);
        BOOST_REQUIRE_EQUAL(message[26], message.size() - 28);
        message[26] += 8;
        const std::vector<unsigned char> zeroExpiry(8, 0);
        message.insert(message.end() - 1, zeroExpiry.begin(), zeroExpiry.end());
        message = addProgram(message);
        checkProgram(message);
        CAssetTransfer legacyMessage;
        BOOST_REQUIRE(TransferAssetFromScript(message, legacyMessage, address));
        BOOST_CHECK_EQUAL(legacyMessage.message, std::string(32, 'x'));
        BOOST_CHECK_EQUAL(legacyMessage.nExpireTime, 0);

        CScript expiry = destination;
        CAssetTransfer("RAVENTEST", COIN, std::string(32, 'x'), 123456789)
            .ConstructTransaction(expiry);
        checkProgram(addProgram(expiry));

        CScript tooShort = transfer;
        tooShort.pop_back();
        BOOST_CHECK(!GetPQAssetProgram(tooShort, parsed));
        CScript tooLong = transfer;
        tooLong.push_back(0);
        BOOST_CHECK(!GetPQAssetProgram(tooLong, parsed));
        CScript overlongPush = transfer;
        ++overlongPush[26];
        BOOST_CHECK(!GetPQAssetProgram(overlongPush, parsed));
        CScript wrongPrefix = transfer;
        wrongPrefix[0] = OP_0;
        BOOST_CHECK(!GetPQAssetProgram(wrongPrefix, parsed));
        CScript nonMinimalPush = transfer;
        const unsigned char oldLength = nonMinimalPush[26];
        BOOST_REQUIRE(oldLength < OP_PUSHDATA1);
        nonMinimalPush[26] = OP_PUSHDATA1;
        nonMinimalPush.insert(nonMinimalPush.begin() + 27, oldLength);
        BOOST_CHECK(!GetPQAssetProgram(nonMinimalPush, parsed));
        CScript nonMinimalPush2 = transfer;
        nonMinimalPush2[26] = OP_PUSHDATA2;
        const std::vector<unsigned char> declaredLength{oldLength, 0};
        nonMinimalPush2.insert(nonMinimalPush2.begin() + 27,
                               declaredLength.begin(), declaredLength.end());
        BOOST_CHECK(!GetPQAssetProgram(nonMinimalPush2, parsed));
        CScript malformedName = transfer;
        malformedName[31] = 0xff;
        BOOST_CHECK(!GetPQAssetProgram(malformedName, parsed));
        CScript wrongPushLength = ipfsIssue;
        --wrongPushLength[27];
        BOOST_CHECK(!GetPQAssetProgram(wrongPushLength, parsed));

        // This old-valid lookalike proves that any future spend rule needs a
        // creation-height gate. Script bytes alone cannot establish intent.
        CScript historical = destination;
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(historical);
        const std::vector<unsigned char> filler(31, 0);
        historical.insert(historical.end(), filler.begin(), filler.end());
        BOOST_CHECK(GetPQAssetProgram(historical, parsed));
        BOOST_CHECK_EQUAL(parsed.begin()[0], OP_DROP);
    }

    BOOST_AUTO_TEST_CASE(pq_asset_history_destination_uses_origin_height_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const CTxDestination destination = DecodeDestination(GetParams().GlobalBurnAddress());
        const CKeyID* classicalKey = boost::get<CKeyID>(&destination);
        BOOST_REQUIRE(classicalKey);
        CScript legacy = GetScriptForDestination(destination);
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(legacy);
        const uint256 program = uint256S("03");
        CScript tagged;
        BOOST_REQUIRE(BuildPQAssetTaggedScript(legacy, program, tagged));
        const std::string classicalAddress = EncodeDestination(destination);
        const std::string protectedAddress =
            EncodePQAssetDestination(*classicalKey, program);

        BOOST_CHECK_EQUAL(EncodeContextualAssetDestination(tagged, 899, 900),
                          classicalAddress);
        BOOST_CHECK_EQUAL(EncodeContextualAssetDestination(tagged, 900, 900),
                          protectedAddress);
        BOOST_CHECK_EQUAL(EncodeContextualAssetDestination(tagged, -1, 900),
                          protectedAddress);
        BOOST_CHECK_EQUAL(EncodeContextualAssetDestination(tagged, 900, -1),
                          classicalAddress);
        BOOST_CHECK(EncodeContextualAssetDestination(legacy, 900, 900).empty());
    }

    BOOST_AUTO_TEST_CASE(pq_asset_tagged_script_builder_preserves_legacy_data_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const uint256 program = uint256S("03");
        const CScript destination = GetScriptForDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        const TxAssetDeploymentContext parserContext{true, false, false, true, true, true};
        const auto check = [&](const CScript& legacy, const std::string& name) {
            CAssetOutputEntry before;
            BOOST_REQUIRE(GetAssetData(legacy, before, &parserContext));
            CScript tagged;
            BOOST_REQUIRE(BuildPQAssetTaggedScript(legacy, program, tagged));
            uint256 parsedProgram;
            BOOST_REQUIRE(GetPQAssetProgram(tagged, parsedProgram));
            BOOST_CHECK(parsedProgram == program);

            CAssetOutputEntry after;
            BOOST_REQUIRE(GetAssetData(legacy, before, &parserContext));
            BOOST_REQUIRE(GetAssetData(tagged, after, &parserContext));
            BOOST_CHECK_EQUAL(after.assetName, name);
            BOOST_CHECK_EQUAL(before.assetName, after.assetName);
            BOOST_CHECK_EQUAL(before.nAmount, after.nAmount);
            BOOST_CHECK(before.destination == after.destination);
            BOOST_CHECK_EQUAL(before.type, after.type);
            if (before.type == TX_TRANSFER_ASSET) {
                BOOST_CHECK_EQUAL(before.message, after.message);
                BOOST_CHECK_EQUAL(before.expireTime, after.expireTime);
            }
        };

        CScript transfer = destination;
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(transfer);
        check(transfer, "RAVENTEST");

        const std::string txidMessage(32, 'x');
        CScript message = destination;
        CAssetTransfer("RAVENTEST", COIN, txidMessage).ConstructTransaction(message);
        CScript naiveMessageTag = message;
        naiveMessageTag.pop_back();
        const std::vector<unsigned char> programBytes = ToByteVector(program);
        naiveMessageTag.insert(naiveMessageTag.end(), programBytes.begin(), programBytes.end());
        uint256 parsedNaiveProgram;
        BOOST_CHECK(!GetPQAssetProgram(naiveMessageTag, parsedNaiveProgram));
        CScript taggedMessage;
        BOOST_REQUIRE(BuildPQAssetTaggedScript(message, program, taggedMessage));
        BOOST_CHECK_EQUAL(taggedMessage.size(), message.size() + 39);
        BOOST_CHECK(std::all_of(taggedMessage.end() - 40, taggedMessage.end() - 32,
                                [](unsigned char byte) { return byte == 0; }));
        check(message, "RAVENTEST");

        CScript expiringMessage = destination;
        CAssetTransfer("RAVENTEST", COIN, txidMessage, 123456789)
            .ConstructTransaction(expiringMessage);
        check(expiringMessage, "RAVENTEST");

        const std::string ipfsMessage = std::string("\x12\x20", 2) + std::string(32, 'h');
        CScript ipfsTransfer = destination;
        CAssetTransfer("RAVENTEST", COIN, ipfsMessage)
            .ConstructTransaction(ipfsTransfer);
        check(ipfsTransfer, "RAVENTEST");

        for (const std::string& name : {"RAVENTEST!", "RAVENTEST#ONE",
                                         "$RAVENTEST", "#RAVENTEST"}) {
            CScript classTransfer = destination;
            CAssetTransfer(name, COIN).ConstructTransaction(classTransfer);
            check(classTransfer, name);
        }

        for (const std::string& name : {"RAVENTEST", "RAVENTEST#ONE",
                                         "$RAVENTEST", "#RAVENTEST"}) {
            CScript issue = destination;
            CNewAsset(name, COIN, 0, 1, 0, "").ConstructTransaction(issue);
            check(issue, name);
        }

        CScript owner = destination;
        CNewAsset("RAVENTEST", COIN, 0, 1, 0, "").ConstructOwnerTransaction(owner);
        check(owner, "RAVENTEST!");

        CScript reissue = destination;
        CReissueAsset("RAVENTEST", COIN, 0, 1, "").ConstructTransaction(reissue);
        check(reissue, "RAVENTEST");
    }

    BOOST_AUTO_TEST_CASE(pq_asset_tagged_script_builder_rejects_malformed_legacy_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const uint256 program = uint256S("03");
        CScript legacy = GetScriptForDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        CAssetTransfer("RAVENTEST", COIN).ConstructTransaction(legacy);

        const CScript sentinel = CScript() << OP_1;
        const auto reject = [&](const CScript& input) {
            CScript output = sentinel;
            BOOST_CHECK(!BuildPQAssetTaggedScript(input, program, output));
            BOOST_CHECK(output == sentinel);
        };

        CScript tagged;
        BOOST_REQUIRE(BuildPQAssetTaggedScript(legacy, program, tagged));
        reject(tagged);

        CScript missingDrop = legacy;
        missingDrop.pop_back();
        reject(missingDrop);

        CScript wrongEnvelope = legacy;
        wrongEnvelope[0] = OP_0;
        reject(wrongEnvelope);

        CScript wrongMarker = legacy;
        wrongMarker[30] = 0xff;
        reject(wrongMarker);

        CScript wrongLength = legacy;
        ++wrongLength[26];
        reject(wrongLength);

        CScript nonMinimalPush = legacy;
        const unsigned char payloadSize = nonMinimalPush[26];
        BOOST_REQUIRE(payloadSize < OP_PUSHDATA1);
        nonMinimalPush[26] = OP_PUSHDATA1;
        nonMinimalPush.insert(nonMinimalPush.begin() + 27, payloadSize);
        reject(nonMinimalPush);

        CScript extraPayload = legacy;
        ++extraPayload[26];
        extraPayload.insert(extraPayload.end() - 1, 0);
        reject(extraPayload);

        CScript message = GetScriptForDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        CAssetTransfer("RAVENTEST", COIN, std::string(32, 'x'))
            .ConstructTransaction(message);
        CScript explicitZeroExpiry = message;
        explicitZeroExpiry[26] += 8;
        const std::vector<unsigned char> zeroExpiry(8, 0);
        explicitZeroExpiry.insert(explicitZeroExpiry.end() - 1,
                                  zeroExpiry.begin(), zeroExpiry.end());
        reject(explicitZeroExpiry);
    }

    BOOST_AUTO_TEST_CASE(pq_asset_creation_height_and_anchor_rule_test)
    {
        SelectParams(CBaseChainParams::MAIN);
        const int activationHeight = 20;
        const uint256 program = uint256S("03");
        const uint256 otherProgram = uint256S("04");

        CTxOut legacy = MakeAssetTransferOutput("RAVENTEST!", OWNER_ASSET_AMOUNT);
        CTxOut tagged = legacy;
        BOOST_REQUIRE(BuildPQAssetTaggedScript(legacy.scriptPubKey, program,
                                              tagged.scriptPubKey));
        CTxOut pqOnly = legacy;
        BOOST_REQUIRE(BuildPQOnlyAssetScript(legacy.scriptPubKey, program,
                                            pqOnly.scriptPubKey));
        uint256 parsed;
        BOOST_REQUIRE(GetPQAssetProgram(pqOnly.scriptPubKey, parsed));
        BOOST_CHECK(parsed == program);

        CCoinsView base;
        CCoinsViewCache coins(&base);
        const COutPoint legacyOut(uint256S("11"), 0);
        const COutPoint historicalLookalike(uint256S("12"), 0);
        const COutPoint protectedOut(uint256S("13"), 0);
        const COutPoint unconfirmedParent(uint256S("14"), 0);
        const COutPoint matchingAnchor(uint256S("15"), 0);
        const COutPoint wrongAnchor(uint256S("16"), 0);
        const COutPoint invalidDualOut(uint256S("17"), 0);
        const COutPoint preActivationPQOnlyOut(uint256S("18"), 0);
        coins.AddCoin(legacyOut, Coin(legacy, 19, false), true);
        coins.AddCoin(historicalLookalike, Coin(tagged, 19, false), true);
        coins.AddCoin(protectedOut, Coin(pqOnly, activationHeight, false), true);
        coins.AddCoin(unconfirmedParent, Coin(pqOnly, MEMPOOL_HEIGHT, false), true);
        coins.AddCoin(invalidDualOut, Coin(tagged, activationHeight, false), true);
        coins.AddCoin(preActivationPQOnlyOut,
                      Coin(pqOnly, activationHeight - 1, false), true);
        coins.AddCoin(matchingAnchor, Coin(CTxOut(1000, GetScriptForWitnessV2PQ(program)), 19, false), true);
        coins.AddCoin(wrongAnchor, Coin(CTxOut(1000, GetScriptForWitnessV2PQ(otherProgram)), 19, false), true);

        unsigned char classIndex = 0x80;
        const auto checkClassRule = [&](CScript script) {
            CMutableTransaction check;
            check.vin.emplace_back(legacyOut);
            check.vout.emplace_back(0, script);
            CValidationState oldOutput;
            BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(check), oldOutput,
                                                    coins, activationHeight));
            BOOST_CHECK_EQUAL(oldOutput.GetRejectReason(), "bad-pq-asset-output");

            CScript taggedClassScript;
            BOOST_REQUIRE(BuildPQAssetTaggedScript(script, program,
                                                  taggedClassScript));
            check.vout[0].scriptPubKey = taggedClassScript;
            CValidationState dualOutput;
            BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(check), dualOutput,
                                                    coins, activationHeight));
            BOOST_CHECK_EQUAL(dualOutput.GetRejectReason(), "bad-pq-asset-output");

            CScript pqOnlyClassScript;
            BOOST_REQUIRE(BuildPQOnlyAssetScript(script, program,
                                                 pqOnlyClassScript));
            script = pqOnlyClassScript;
            check.vout[0].scriptPubKey = script;
            uint256 hash;
            hash.SetNull();
            hash.begin()[0] = classIndex++;
            const COutPoint classOut(hash, 0);
            coins.AddCoin(classOut, Coin(CTxOut(0, script), activationHeight, false), true);
            check.vin[0].prevout = classOut;
            CValidationState noAnchor;
            BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(check), noAnchor,
                                                    coins, activationHeight));
            BOOST_CHECK_EQUAL(noAnchor.GetRejectReason(), "bad-pq-asset-anchor");
            check.vin.emplace_back(matchingAnchor);
            CValidationState withAnchor;
            BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(check), withAnchor,
                                                   coins, activationHeight));
        };
        const CScript classDestination = GetScriptForDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        for (const std::string& name : {"RAVENTEST", "RAVENTEST/SUB",
                                         "RAVENTEST#ONE", "$RAVENTEST",
                                         "#RAVENTEST", "#RAVENTEST/SUB"}) {
            CScript script = classDestination;
            CNewAsset(name, COIN, 0, 1, 0, "").ConstructTransaction(script);
            checkClassRule(script);
            script = classDestination;
            CAssetTransfer(name, COIN).ConstructTransaction(script);
            checkClassRule(script);
        }
        CScript ownerIssue = classDestination;
        CNewAsset("RAVENTEST", COIN, 0, 1, 0, "").ConstructOwnerTransaction(ownerIssue);
        checkClassRule(ownerIssue);
        CScript reissueOutput = classDestination;
        CReissueAsset("RAVENTEST", COIN, 0, 1, "").ConstructTransaction(reissueOutput);
        checkClassRule(reissueOutput);
        CScript messageTransfer = classDestination;
        CAssetTransfer("RAVENTEST", COIN, std::string(32, 'x'))
            .ConstructTransaction(messageTransfer);
        checkClassRule(messageTransfer);

        CMutableTransaction spend;
        spend.vin.emplace_back(legacyOut);
        spend.vout.push_back(legacy);
        CValidationState inactive;
        BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(spend), inactive, coins, -1));
        CValidationState untagged;
        BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(spend), untagged, coins, activationHeight));
        BOOST_CHECK_EQUAL(untagged.GetRejectReason(), "bad-pq-asset-output");

        spend.vout[0] = pqOnly;
        bool protectedInput = true;
        CValidationState migration;
        BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(spend), migration, coins,
                                               activationHeight, &protectedInput));
        BOOST_CHECK(!protectedInput);

        spend.vout[0] = tagged;
        CValidationState dualOutput;
        BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(spend), dualOutput,
                                                coins, activationHeight));
        BOOST_CHECK_EQUAL(dualOutput.GetRejectReason(), "bad-pq-asset-output");
        spend.vout[0] = pqOnly;
        spend.vin[0].prevout = historicalLookalike;
        CValidationState historical;
        BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(spend), historical, coins,
                                               activationHeight, &protectedInput));
        BOOST_CHECK(!protectedInput);
        spend.vin[0].scriptSig = CScript() << OP_1;
        CValidationState historicalScriptSig;
        BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(spend), historicalScriptSig,
                                               coins, activationHeight));
        spend.vin[0].scriptSig.clear();

        // A lookalike created before activation remains outside the anchor
        // rule by origin height. Wallets must not label it as PQ custody.
        spend.vin[0].prevout = preActivationPQOnlyOut;
        CValidationState preActivationSpend;
        BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(spend),
                                               preActivationSpend, coins, -1));
        CValidationState preActivationPQOnly;
        BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(spend),
                                               preActivationPQOnly, coins,
                                               activationHeight, &protectedInput));
        BOOST_CHECK(!protectedInput);

        spend.vin[0].prevout = protectedOut;
        CValidationState missing;
        BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(spend), missing, coins, activationHeight));
        BOOST_CHECK_EQUAL(missing.GetRejectReason(), "bad-pq-asset-anchor");
        spend.vin.emplace_back(wrongAnchor);
        CValidationState wrong;
        BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(spend), wrong, coins, activationHeight));
        BOOST_CHECK_EQUAL(wrong.GetRejectReason(), "bad-pq-asset-anchor");
        spend.vin[1].prevout = matchingAnchor;
        CValidationState matched;
        BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(spend), matched, coins,
                                               activationHeight, &protectedInput));
        BOOST_CHECK(protectedInput);
        spend.vin[0].scriptSig = CScript() << OP_1;
        CValidationState malleable;
        BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(spend), malleable,
                                                coins, activationHeight));
        BOOST_CHECK_EQUAL(malleable.GetRejectReason(), "bad-pq-asset-scriptsig");
        spend.vin[0].scriptSig.clear();

        spend.vin[0].prevout = invalidDualOut;
        CValidationState invalidDual;
        BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(spend), invalidDual,
                                                coins, activationHeight));
        BOOST_CHECK_EQUAL(invalidDual.GetRejectReason(), "bad-pq-asset-prevout");

        spend.vin[0].prevout = unconfirmedParent;
        CValidationState mempoolParent;
        BOOST_CHECK(Consensus::CheckTxPQAssets(CTransaction(spend), mempoolParent, coins,
                                               activationHeight, &protectedInput));
        BOOST_CHECK(protectedInput);

        spend.vin.pop_back();
        spend.vout[0] = legacy;
        CValidationState downgrade;
        BOOST_CHECK(!Consensus::CheckTxPQAssets(CTransaction(spend), downgrade, coins, activationHeight));
        BOOST_CHECK_EQUAL(downgrade.GetRejectReason(), "bad-pq-asset-output");
    }

    BOOST_AUTO_TEST_CASE(asset_tx_valid_test)
    {
        BOOST_TEST_MESSAGE("Running Asset TX Valid Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the asset scriptPubKey
        CAssetTransfer asset("RAVENTEST", 1000);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        asset.ConstructTransaction(scriptPubKey);

        CCoinsView view;
        CCoinsViewCache coins(&view);

        CAssetsCache assetCache;

        // Create CTxOut and add it to a coin
        CTxOut txOut;
        txOut.nValue = 0;
        txOut.scriptPubKey = scriptPubKey;

        // Create a random hash
        uint256 hash = uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A2");

        // Add the coin to the cache
        COutPoint outpoint(hash, 1);
        coins.AddCoin(outpoint, Coin(txOut, 10, 0), true);

        // Create transaction and input for the outpoint of the coin we just created
        CMutableTransaction mutTx;

        CTxIn in;
        in.prevout = outpoint;

        // Add the input, and an output into the transaction
        mutTx.vin.emplace_back(in);
        mutTx.vout.emplace_back(txOut);

        CTransaction tx(mutTx);
        CValidationState state;

        // The inputs are spending 1000 Assets
        // The outputs are assigning a destination to 1000 Assets
        // This test should pass because all assets are assigned a destination
        std::vector<std::pair<std::string, uint256>> vReissueAssets;
        BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(tx, state, coins, nullptr, false, vReissueAssets, true, true), "CheckTxAssets Failed");
    }

    BOOST_AUTO_TEST_CASE(asset_tx_not_valid_test)
    {
        BOOST_TEST_MESSAGE("Running Asset TX Not Valid Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the asset scriptPubKey
        CAssetTransfer asset("RAVENTEST", 1000);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        asset.ConstructTransaction(scriptPubKey);

        CCoinsView view;
        CCoinsViewCache coins(&view);
        CAssetsCache assetCache;

        // Create CTxOut and add it to a coin
        CTxOut txOut;
        txOut.nValue = 0;
        txOut.scriptPubKey = scriptPubKey;

        // Create a random hash
        uint256 hash = uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A2");

        // Add the coin to the cache
        COutPoint outpoint(hash, 1);
        coins.AddCoin(outpoint, Coin(txOut, 10, 0), true);

        // Create transaction and input for the outpoint of the coin we just created
        CMutableTransaction mutTx;

        CTxIn in;
        in.prevout = outpoint;

        // Create CTxOut that will only send 100 of the asset
        // This should fail because 900 RAVEN doesn't have a destination
        CAssetTransfer assetTransfer("RAVENTEST", 100);
        CScript scriptLess = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        assetTransfer.ConstructTransaction(scriptLess);

        CTxOut txOut2;
        txOut2.nValue = 0;
        txOut2.scriptPubKey = scriptLess;

        // Add the input, and an output into the transaction
        mutTx.vin.emplace_back(in);
        mutTx.vout.emplace_back(txOut2);

        CTransaction tx(mutTx);
        CValidationState state;

        // The inputs of this transaction are spending 1000 Assets
        // The outputs are assigning a destination to only 100 Assets
        // This should fail because 900 Assets aren't being assigned a destination (Trying to burn 900 Assets)
        std::vector<std::pair<std::string, uint256>> vReissueAssets;
        BOOST_CHECK_MESSAGE(!Consensus::CheckTxAssets(tx, state, coins, nullptr, false, vReissueAssets, true, true), "CheckTxAssets should have failed");
    }

    BOOST_AUTO_TEST_CASE(asset_tx_valid_multiple_outs_test)
    {
        BOOST_TEST_MESSAGE("Running Asset TX Valid Multiple Outs Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the asset scriptPubKey
        CAssetTransfer asset("RAVENTEST", 1000);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        asset.ConstructTransaction(scriptPubKey);

        CCoinsView view;
        CCoinsViewCache coins(&view);
        CAssetsCache assetCache;

        // Create CTxOut and add it to a coin
        CTxOut txOut;
        txOut.nValue = 0;
        txOut.scriptPubKey = scriptPubKey;

        // Create a random hash
        uint256 hash = uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A2");

        // Add the coin to the cache
        COutPoint outpoint(hash, 1);
        coins.AddCoin(outpoint, Coin(txOut, 10, 0), true);

        // Create transaction and input for the outpoint of the coin we just created
        CMutableTransaction mutTx;

        CTxIn in;
        in.prevout = outpoint;

        // Create CTxOut that will only send 100 of the asset 10 times total = 1000
        for (int i = 0; i < 10; i++)
        {
            CAssetTransfer asset2("RAVENTEST", 100);
            CScript scriptPubKey2 = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
            asset2.ConstructTransaction(scriptPubKey2);

            CTxOut txOut2;
            txOut2.nValue = 0;
            txOut2.scriptPubKey = scriptPubKey2;

            // Add the output into the transaction
            mutTx.vout.emplace_back(txOut2);
        }

        // Add the input, and an output into the transaction
        mutTx.vin.emplace_back(in);

        CTransaction tx(mutTx);
        CValidationState state;

        // The inputs are spending 1000 Assets
        // The outputs are assigned 100 Assets to 10 destinations (10 * 100) = 1000
        // This test should pass all assets that are being spent are assigned to a destination
        std::vector<std::pair<std::string, uint256>> vReissueAssets;
        BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(tx, state, coins, nullptr, false, vReissueAssets, true, true), "CheckTxAssets failed");
    }

    BOOST_AUTO_TEST_CASE(asset_tx_multiple_outs_invalid_test)
    {
        BOOST_TEST_MESSAGE("Running Asset TX Multiple Outs Invalid Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the asset scriptPubKey
        CAssetTransfer asset("RAVENTEST", 1000);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        asset.ConstructTransaction(scriptPubKey);

        CCoinsView view;
        CCoinsViewCache coins(&view);
        CAssetsCache assetCache;

        // Create CTxOut and add it to a coin
        CTxOut txOut;
        txOut.nValue = 0;
        txOut.scriptPubKey = scriptPubKey;

        // Create a random hash
        uint256 hash = uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A2");

        // Add the coin to the cache
        COutPoint outpoint(hash, 1);
        coins.AddCoin(outpoint, Coin(txOut, 10, 0), true);

        // Create transaction and input for the outpoint of the coin we just created
        CMutableTransaction mutTx;

        CTxIn in;
        in.prevout = outpoint;

        // Create CTxOut that will only send 100 of the asset 12 times, total = 1200
        for (int i = 0; i < 12; i++)
        {
            CAssetTransfer asset2("RAVENTEST", 100);
            CScript scriptPubKey2 = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
            asset2.ConstructTransaction(scriptPubKey2);

            CTxOut txOut2;
            txOut2.nValue = 0;
            txOut2.scriptPubKey = scriptPubKey2;

            // Add the output into the transaction
            mutTx.vout.emplace_back(txOut2);
        }

        // Add the input, and an output into the transaction
        mutTx.vin.emplace_back(in);

        CTransaction tx(mutTx);
        CValidationState state;

        // The inputs are spending 1000 Assets
        // The outputs are assigning 100 Assets to 12 destinations (12 * 100 = 1200)
        // This test should fail because the Outputs are greater than the inputs
        std::vector<std::pair<std::string, uint256>> vReissueAssets;
        BOOST_CHECK_MESSAGE(!Consensus::CheckTxAssets(tx, state, coins, nullptr, false, vReissueAssets, true, true), "CheckTxAssets passed when it should have failed");
    }

    BOOST_AUTO_TEST_CASE(asset_tx_multiple_assets_test)
    {
        BOOST_TEST_MESSAGE("Running Asset TX Multiple Assets Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the asset scriptPubKeys
        CAssetTransfer asset("RAVENTEST", 1000);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        asset.ConstructTransaction(scriptPubKey);

        CAssetTransfer asset2("RAVENTESTTEST", 1000);
        CScript scriptPubKey2 = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        asset2.ConstructTransaction(scriptPubKey2);

        CAssetTransfer asset3("RAVENTESTTESTTEST", 1000);
        CScript scriptPubKey3 = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        asset3.ConstructTransaction(scriptPubKey3);

        CCoinsView view;
        CCoinsViewCache coins(&view);
        CAssetsCache assetCache;

        // Create CTxOuts
        CTxOut txOut;
        txOut.nValue = 0;
        txOut.scriptPubKey = scriptPubKey;

        CTxOut txOut2;
        txOut2.nValue = 0;
        txOut2.scriptPubKey = scriptPubKey2;

        CTxOut txOut3;
        txOut3.nValue = 0;
        txOut3.scriptPubKey = scriptPubKey3;

        // Create a random hash
        uint256 hash = uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A2");
        uint256 hash2 = uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A3");
        uint256 hash3 = uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A4");

        // Add the coins to the cache
        COutPoint outpoint(hash, 1);
        coins.AddCoin(outpoint, Coin(txOut, 10, 0), true);

        COutPoint outpoint2(hash2, 1);
        coins.AddCoin(outpoint2, Coin(txOut2, 10, 0), true);

        COutPoint outpoint3(hash3, 1);
        coins.AddCoin(outpoint3, Coin(txOut3, 10, 0), true);

        Coin coinTemp;
        BOOST_CHECK_MESSAGE(coins.GetCoin(outpoint, coinTemp), "Failed to get coin 1");
        BOOST_CHECK_MESSAGE(coins.GetCoin(outpoint2, coinTemp), "Failed to get coin 2");
        BOOST_CHECK_MESSAGE(coins.GetCoin(outpoint3, coinTemp), "Failed to get coin 3");

        // Create transaction and input for the outpoint of the coin we just created
        CMutableTransaction mutTx;

        CTxIn in;
        in.prevout = outpoint;

        CTxIn in2;
        in2.prevout = outpoint2;

        CTxIn in3;
        in3.prevout = outpoint3;

        // Create CTxOut for each asset that spends 100 assets 10 time = 1000 asset in total
        for (int i = 0; i < 10; i++)
        {
            // Add the first asset
            CAssetTransfer outAsset("RAVENTEST", 100);
            CScript outScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
            outAsset.ConstructTransaction(outScript);

            CTxOut txOutNew;
            txOutNew.nValue = 0;
            txOutNew.scriptPubKey = outScript;

            mutTx.vout.emplace_back(txOutNew);

            // Add the second asset
            CAssetTransfer outAsset2("RAVENTESTTEST", 100);
            CScript outScript2 = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
            outAsset2.ConstructTransaction(outScript2);

            CTxOut txOutNew2;
            txOutNew2.nValue = 0;
            txOutNew2.scriptPubKey = outScript2;

            mutTx.vout.emplace_back(txOutNew2);

            // Add the third asset
            CAssetTransfer outAsset3("RAVENTESTTESTTEST", 100);
            CScript outScript3 = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
            outAsset3.ConstructTransaction(outScript3);

            CTxOut txOutNew3;
            txOutNew3.nValue = 0;
            txOutNew3.scriptPubKey = outScript3;

            mutTx.vout.emplace_back(txOutNew3);
        }

        // Add the inputs
        mutTx.vin.emplace_back(in);
        mutTx.vin.emplace_back(in2);
        mutTx.vin.emplace_back(in3);

        CTransaction tx(mutTx);
        CValidationState state;

        // The inputs are spending 3000 Assets (1000 of each RAVEN, RAVENTEST, RAVENTESTTEST)
        // The outputs are spending 100 Assets to 10 destinations (10 * 100 = 1000) (of each RAVEN, RAVENTEST, RAVENTESTTEST)
        // This test should pass because for each asset that is spent. It is assigned a destination
        std::vector<std::pair<std::string, uint256>> vReissueAssets;
        BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(tx, state, coins, nullptr, false, vReissueAssets, true, true), state.GetDebugMessage());


        // Try it not but only spend 900 of each asset instead of 1000
        CMutableTransaction mutTx2;

        // Create CTxOut for each asset that spends 100 assets 9 time = 900 asset in total
        for (int i = 0; i < 9; i++)
        {
            // Add the first asset
            CAssetTransfer outAsset("RAVENTEST", 100);
            CScript outScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
            outAsset.ConstructTransaction(outScript);

            CTxOut txOutNew;
            txOutNew.nValue = 0;
            txOutNew.scriptPubKey = outScript;

            mutTx2.vout.emplace_back(txOutNew);

            // Add the second asset
            CAssetTransfer outAsset2("RAVENTESTTEST", 100);
            CScript outScript2 = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
            outAsset2.ConstructTransaction(outScript2);

            CTxOut txOutNew2;
            txOutNew2.nValue = 0;
            txOutNew2.scriptPubKey = outScript2;

            mutTx2.vout.emplace_back(txOutNew2);

            // Add the third asset
            CAssetTransfer outAsset3("RAVENTESTTESTTEST", 100);
            CScript outScript3 = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
            outAsset3.ConstructTransaction(outScript3);

            CTxOut txOutNew3;
            txOutNew3.nValue = 0;
            txOutNew3.scriptPubKey = outScript3;

            mutTx2.vout.emplace_back(txOutNew3);
        }

        // Add the inputs
        mutTx2.vin.emplace_back(in);
        mutTx2.vin.emplace_back(in2);
        mutTx2.vin.emplace_back(in3);

        CTransaction tx2(mutTx2);

        // Check the transaction that contains inputs that are spending 1000 Assets for 3 different assets
        // While only outputs only contain 900 Assets being sent to a destination
        // This should fail because 100 of each Asset isn't being sent to a destination (Trying to burn 100 Assets each)
        BOOST_CHECK_MESSAGE(!Consensus::CheckTxAssets(tx2, state, coins, nullptr, false, vReissueAssets, true, true), "CheckTxAssets should have failed");
    }

    BOOST_AUTO_TEST_CASE(transfer_overflow_checks_follow_explicit_context)
    {
        SelectParams(CBaseChainParams::MAIN);
        const std::string assetName = "OVERFLOW";
        std::vector<std::pair<std::string, uint256>> vReissueAssets;
        constexpr uint64_t wrapPartA = 8173372036854775857ULL;
        constexpr uint64_t wrapPartB = 2100000000000000002ULL;
        static_assert(wrapPartA + wrapPartA + wrapPartB == 100ULL,
                      "overflow vector must equal 100 modulo 2^64");

        // Preserve the historical preactivation behavior independently of the
        // process's prior BIP9 state. These outputs sum mathematically to
        // 2^64 + 100; consensus explicitly evaluates the modulo result as 100.
        {
            CCoinsView base;
            CCoinsViewCache coins(&base);
            const COutPoint input(uint256S("01"), 0);
            AddAssetCoin(coins, input, assetName, 100);

            CMutableTransaction mutableTx;
            mutableTx.vin.emplace_back(input);
            mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, static_cast<CAmount>(wrapPartA)));
            mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, static_cast<CAmount>(wrapPartA)));
            mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, static_cast<CAmount>(wrapPartB)));
            const CTransaction tx(mutableTx);

            CValidationState preactivationState;
            BOOST_REQUIRE_MESSAGE(Consensus::CheckTxAssets(tx, preactivationState, coins, nullptr, false,
                                                           vReissueAssets, false, true),
                                  preactivationState.GetRejectReason());

            CValidationState activeState;
            BOOST_CHECK(!Consensus::CheckTxAssets(tx, activeState, coins, nullptr, false,
                                                  vReissueAssets, true, true));
            BOOST_CHECK_EQUAL(activeState.GetRejectReason(), "bad-txns-transfer-asset-amount-toolarge");
        }

        // Mirror the modulo vector through the input accumulator. This is a
        // separate consensus path and must be defined under sanitizers too.
        {
            CCoinsView base;
            CCoinsViewCache coins(&base);
            const COutPoint first(uint256S("06"), 0);
            const COutPoint second(uint256S("07"), 0);
            const COutPoint third(uint256S("08"), 0);
            AddAssetCoin(coins, first, assetName, static_cast<CAmount>(wrapPartA));
            AddAssetCoin(coins, second, assetName, static_cast<CAmount>(wrapPartA));
            AddAssetCoin(coins, third, assetName, static_cast<CAmount>(wrapPartB));

            CMutableTransaction mutableTx;
            mutableTx.vin.emplace_back(first);
            mutableTx.vin.emplace_back(second);
            mutableTx.vin.emplace_back(third);
            mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, 100));
            const CTransaction tx(mutableTx);

            CValidationState preactivationState;
            BOOST_REQUIRE_MESSAGE(Consensus::CheckTxAssets(tx, preactivationState, coins, nullptr, false,
                                                           vReissueAssets, false, true),
                                  preactivationState.GetRejectReason());

            CValidationState activeState;
            BOOST_CHECK(!Consensus::CheckTxAssets(tx, activeState, coins, nullptr, false,
                                                  vReissueAssets, true, true));
            BOOST_CHECK_EQUAL(activeState.GetRejectReason(), "bad-txns-input-asset-amount-toolarge");
        }

        // An oversized historical UTXO is spendable under preactivation rules
        // but rejected under ACTIVE rules. Calling ACTIVE first must not latch
        // the result for the following preactivation check.
        {
            CCoinsView base;
            CCoinsViewCache coins(&base);
            const COutPoint input(uint256S("02"), 0);
            AddAssetCoin(coins, input, assetName, MAX_MONEY + 1);

            CMutableTransaction mutableTx;
            mutableTx.vin.emplace_back(input);
            mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, MAX_MONEY + 1));
            const CTransaction tx(mutableTx);

            CValidationState activeState;
            BOOST_CHECK(!Consensus::CheckTxAssets(tx, activeState, coins, nullptr, false,
                                                  vReissueAssets, true, true));
            BOOST_CHECK_EQUAL(activeState.GetRejectReason(), "bad-txns-input-asset-amount-toolarge");

            CValidationState preactivationState;
            BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(tx, preactivationState, coins, nullptr, false,
                                                         vReissueAssets, false, true),
                                preactivationState.GetRejectReason());
        }

        // Independently exercise the aggregate input and output guards required
        // by the Ravencoin Core 4.8.0 security baseline.
        {
            CCoinsView base;
            CCoinsViewCache coins(&base);
            const COutPoint first(uint256S("03"), 0);
            const COutPoint second(uint256S("04"), 0);
            AddAssetCoin(coins, first, assetName, MAX_MONEY);
            AddAssetCoin(coins, second, assetName, 1);

            CMutableTransaction mutableTx;
            mutableTx.vin.emplace_back(first);
            mutableTx.vin.emplace_back(second);
            mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, MAX_MONEY));

            CValidationState state;
            BOOST_CHECK(!Consensus::CheckTxAssets(CTransaction(mutableTx), state, coins, nullptr, false,
                                                  vReissueAssets, true, true));
            BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-input-asset-totalInputs-toolarge");
        }

        {
            CCoinsView base;
            CCoinsViewCache coins(&base);
            const COutPoint input(uint256S("05"), 0);
            AddAssetCoin(coins, input, assetName, MAX_MONEY);

            CMutableTransaction mutableTx;
            mutableTx.vin.emplace_back(input);
            mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, MAX_MONEY));
            mutableTx.vout.emplace_back(MakeAssetTransferOutput(assetName, 1));

            CValidationState state;
            BOOST_CHECK(!Consensus::CheckTxAssets(CTransaction(mutableTx), state, coins, nullptr, false,
                                                  vReissueAssets, true, true));
            BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-transfer-asset-totalOutputs-toolarge");
        }
    }

    BOOST_AUTO_TEST_CASE(asset_tx_issue_units_test)
    {
        BOOST_TEST_MESSAGE("Running Asset TX Issue Units Test");

        std::string error;
        CAssetsCache cache;

        // Amount = 1.00000000
        CNewAsset asset("ASSET", CAmount(100000000), 8, false, false, "");

        BOOST_CHECK_MESSAGE(CheckNewAsset(asset, error), "Test1: " + error);

        // Amount = 1.00000000
        asset = CNewAsset("ASSET", CAmount(100000000), 0, false, false, "");
        BOOST_CHECK_MESSAGE(CheckNewAsset(asset, error), "Test2: " + error);

        // Amount = 0.10000000
        asset = CNewAsset("ASSET", CAmount(10000000), 8, false, false, "");
        BOOST_CHECK_MESSAGE(CheckNewAsset(asset, error), "Test3: " + error);

        // Amount = 0.10000000
        asset = CNewAsset("ASSET", CAmount(10000000), 2, false, false, "");
        BOOST_CHECK_MESSAGE(CheckNewAsset(asset, error), "Test4: " + error);

        // Amount = 0.10000000
        asset = CNewAsset("ASSET", CAmount(10000000), 0, false, false, "");
        BOOST_CHECK_MESSAGE(!CheckNewAsset(asset, error), "Test5: " + error);

        // Amount = 0.01000000
        asset = CNewAsset("ASSET", CAmount(1000000), 0, false, false, "");
        BOOST_CHECK_MESSAGE(!CheckNewAsset(asset, error), "Test6: " + error);

        // Amount = 0.01000000
        asset = CNewAsset("ASSET", CAmount(1000000), 1, false, false, "");
        BOOST_CHECK_MESSAGE(!CheckNewAsset(asset, error), "Test7: " + error);

        // Amount = 0.01000000
        asset = CNewAsset("ASSET", CAmount(1000000), 2, false, false, "");
        BOOST_CHECK_MESSAGE(CheckNewAsset(asset, error), "Test8: " + error);

        // Amount = 0.00000001
        asset = CNewAsset("ASSET", CAmount(1), 8, false, false, "");
        BOOST_CHECK_MESSAGE(CheckNewAsset(asset, error), "Test9: " + error);

        // Amount = 0.00000010
        asset = CNewAsset("ASSET", CAmount(10), 7, false, false, "");
        BOOST_CHECK_MESSAGE(CheckNewAsset(asset, error), "Test10: " + error);

        // Amount = 0.00000001
        asset = CNewAsset("ASSET", CAmount(1), 7, false, false, "");
        BOOST_CHECK_MESSAGE(!CheckNewAsset(asset, error), "Test11: " + error);

        // Amount = 0.00000100
        asset = CNewAsset("ASSET", CAmount(100), 6, false, false, "");
        BOOST_CHECK_MESSAGE(CheckNewAsset(asset, error), "Test12: " + error);

        // Amount = 0.00000100
        asset = CNewAsset("ASSET", CAmount(100), 5, false, false, "");
        BOOST_CHECK_MESSAGE(!CheckNewAsset(asset, error), "Test13: " + error);
    }

    BOOST_AUTO_TEST_CASE(asset_tx_enforce_value_test)
    {
        BOOST_TEST_MESSAGE("Running Asset TX Enforce Value Test");

        SelectParams(CBaseChainParams::MAIN);

        // Create the reissue asset
        CReissueAsset reissueAsset("ENFORCE_VALUE", 100, 8, true, "");
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        reissueAsset.ConstructTransaction(scriptPubKey);

        // Create an invalid reissue asset with nValue not equal to zero
        CTxOut txOut;
        txOut.nValue = 500;
        txOut.scriptPubKey = scriptPubKey;

        // Create views
        CCoinsView view;
        CCoinsViewCache coins(&view);
        CAssetsCache assetCache;

        // Create a random hash
        uint256 hash = uint256S("BF50CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A2");

        // Add the coin to the cache
        COutPoint outpoint(hash, 1);
        coins.AddCoin(outpoint, Coin(txOut, 10, 0), true);

        // Create input
        CTxIn in;
        in.prevout = outpoint;

        // Create transaction and input for the outpoint of the coin we just created
        CMutableTransaction mutTx;

        // Add the input, and an output into the transaction
        mutTx.vin.emplace_back(in);
        mutTx.vout.emplace_back(txOut);

        CTransaction tx(mutTx);
        CValidationState state;

        bool fCheckMempool = true;
        bool fCheckBlock = false;

        // Check that the CheckTransaction will fail when trying to add it to the mempool
        bool fCheck = !CheckTransaction(tx, state, true, fCheckMempool, fCheckBlock);

        BOOST_CHECK(fCheck);
        BOOST_CHECK(state.GetRejectReason() == "bad-mempool-txns-asset-reissued-amount-isn't-zero");

        // Check that the CheckTransaction will fail when trying to add it to a block
        fCheckMempool = false;
        fCheckBlock = true;
        // Turn on the BIP that enforces the block check
        SetEnforcedValues(true);

        fCheck = !CheckTransaction(tx, state, true, fCheckMempool, fCheckBlock);
        BOOST_CHECK(fCheck);
        BOOST_CHECK(state.GetRejectReason() == "bad-txns-asset-reissued-amount-isn't-zero");
    }

#ifdef ENABLE_WALLET
    BOOST_AUTO_TEST_CASE(asset_tx_enforce_coinbase_test)
    {
        BOOST_TEST_MESSAGE("Running Asset TX Enforce Coinbase Test");

        SelectParams(CBaseChainParams::MAIN);

        // Build wallet
        bitdb.MakeMock();
        std::unique_ptr<CWalletDBWrapper> dbw(new CWalletDBWrapper(&bitdb, "wallet_test.dat"));
        CWallet wallet(std::move(dbw));
        bool firstRun;
        wallet.LoadWallet(firstRun);

        // Build coinbasescript
        std::shared_ptr<CReserveScript> coinbaseScript;
        wallet.GetScriptForMining(coinbaseScript);

        // Create coinbase transaction.
        CMutableTransaction coinbaseTx;
        coinbaseTx.vin.resize(1);
        coinbaseTx.vin[0].prevout.SetNull();

        // Resize the coinbase vout to allow for an additional transaction
        coinbaseTx.vout.resize(2);

        // Add in the initial coinbase data
        coinbaseTx.vout[0].scriptPubKey = coinbaseScript->reserveScript;
        coinbaseTx.vout[0].nValue = GetBlockSubsidy(100, GetParams().GetConsensus());
        coinbaseTx.vin[0].scriptSig = CScript() << 100 << OP_0;

        // Create a transfer asset
        CAssetTransfer transferAsset("COINBASE_TEST", 100);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        transferAsset.ConstructTransaction(scriptPubKey);

        // Add the transfer asset script into the coinbase
        coinbaseTx.vout[1].scriptPubKey = scriptPubKey;
        coinbaseTx.vout[1].nValue = 0;

        // Create the transaction and state objects
        CTransaction tx(coinbaseTx);
        CValidationState state;

        // Setting the coinbase check to true
        // This check should now fail on the CheckTransaction call
        SetEnforcedCoinbase(true);
        bool fCheck = CheckTransaction(tx, state, true);
        BOOST_CHECK(!fCheck);
        BOOST_CHECK(state.GetRejectReason() == "bad-txns-coinbase-contains-asset-txes");

        // Setting the coinbase check to false
        // This check should now pass the CheckTransaction call
        SetEnforcedCoinbase(false);
        fCheck = CheckTransaction(tx, state, true);
        BOOST_CHECK(fCheck);

        // Remove wallet used for testing
        bitdb.Flush(true);
        bitdb.Reset();
    }
#endif

BOOST_AUTO_TEST_SUITE_END()
