// Copyright (c) 2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>

#include <test/test_raven.h>

#include <boost/test/unit_test.hpp>

#include <amount.h>
#include <base58.h>
#include <chainparams.h>
#include <script/standard.h>
#include <consensus/validation.h>
#include <consensus/tx_verify.h>
#include <validation.h>
#include <undo.h>
#include <arith_uint256.h>
#include <keystore.h>
#include <chrono>
#include <algorithm>

BOOST_FIXTURE_TEST_SUITE(assetauth_tests, BasicTestingSetup)

    // Helper: build a preimage for the given names (sorted automatically)
    static CAssetAuthPreimage MakePreimage(uint8_t nRequired, std::vector<std::string> vNames)
    {
        std::sort(vNames.begin(), vNames.end());
        return CAssetAuthPreimage(nRequired, vNames);
    }

    // Helper: build a P2AH base scriptPubKey for a preimage
    static CScript MakeP2AHScript(const CAssetAuthPreimage& preimage)
    {
        CScript script;
        preimage.ConstructTransaction(script);
        return script;
    }

    // Helper: build the scriptSig (single push of the serialized preimage)
    static CScript MakeP2AHScriptSig(const CAssetAuthPreimage& preimage)
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << preimage;
        std::vector<unsigned char> vchPreimage(ss.begin(), ss.end());
        return CScript() << vchPreimage;
    }

    // Helper: a P2PKH script to use as a generic key-protected destination
    static CScript MakeKeyScript()
    {
        return GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
    }

    // Helper: add a coin to the view, returning its outpoint
    static COutPoint AddCoin(CCoinsViewCache& coins, const CScript& scriptPubKey, CAmount nValue, uint32_t n)
    {
        CTxOut txOut;
        txOut.nValue = nValue;
        txOut.scriptPubKey = scriptPubKey;
        uint256 hash = uint256S(strprintf("%064x", 0xdeadbeef00 + n));
        COutPoint outpoint(hash, n);
        coins.AddCoin(outpoint, Coin(txOut, 10, 0), true);
        return outpoint;
    }

    BOOST_AUTO_TEST_CASE(preimage_validation_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Preimage Validation Test");

        std::string strError;

        // Valid 1-of-1
        CAssetAuthPreimage p1 = MakePreimage(1, {"ALPHA!"});
        BOOST_CHECK_MESSAGE(p1.IsValid(strError), strError);

        // Valid 2-of-3
        CAssetAuthPreimage p2 = MakePreimage(2, {"ALPHA!", "BETA!", "GAMMA!"});
        BOOST_CHECK_MESSAGE(p2.IsValid(strError), strError);

        // Invalid: zero required
        CAssetAuthPreimage p3 = MakePreimage(0, {"ALPHA!"});
        BOOST_CHECK(!p3.IsValid(strError));

        // Invalid: required more than available
        CAssetAuthPreimage p4 = MakePreimage(2, {"ALPHA!"});
        BOOST_CHECK(!p4.IsValid(strError));

        // Invalid: empty list
        CAssetAuthPreimage p5(1, {});
        BOOST_CHECK(!p5.IsValid(strError));

        // Invalid: not an owner asset name
        CAssetAuthPreimage p6 = MakePreimage(1, {"ALPHA"});
        BOOST_CHECK(!p6.IsValid(strError));

        // Invalid: duplicate names
        CAssetAuthPreimage p7(1, {"ALPHA!", "ALPHA!"});
        BOOST_CHECK(!p7.IsValid(strError));

        // Invalid: unsorted names
        CAssetAuthPreimage p8(1, {"BETA!", "ALPHA!"});
        BOOST_CHECK(!p8.IsValid(strError));

        // Invalid: more than MAX_ASSET_AUTH_NAMES
        std::vector<std::string> vTooMany;
        for (int i = 0; i < MAX_ASSET_AUTH_NAMES + 1; i++)
            vTooMany.push_back(strprintf("ASSET_%02d!", i));
        CAssetAuthPreimage p9 = MakePreimage(1, vTooMany);
        BOOST_CHECK(!p9.IsValid(strError));
    }

    BOOST_AUTO_TEST_CASE(preimage_serialization_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Preimage Serialization Test");

        CAssetAuthPreimage p1 = MakePreimage(2, {"ALPHA!", "BETA!", "GAMMA!"});

        // Round trip through serialization
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << p1;
        CAssetAuthPreimage p2;
        ss >> p2;

        BOOST_CHECK_EQUAL(p1.nRequired, p2.nRequired);
        BOOST_CHECK(p1.vOwnerAssetNames == p2.vOwnerAssetNames);
        BOOST_CHECK(p1.GetHash() == p2.GetHash());

        // Same set of names always produces the same hash (canonical)
        CAssetAuthPreimage p3 = MakePreimage(2, {"GAMMA!", "ALPHA!", "BETA!"});
        BOOST_CHECK(p1.GetHash() == p3.GetHash());

        // Different m produces a different hash
        CAssetAuthPreimage p4 = MakePreimage(3, {"ALPHA!", "BETA!", "GAMMA!"});
        BOOST_CHECK(p1.GetHash() != p4.GetHash());
    }

    BOOST_AUTO_TEST_CASE(script_recognition_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Script Recognition Test");

        SelectParams(CBaseChainParams::MAIN);

        CAssetAuthPreimage preimage = MakePreimage(1, {"ALPHA!"});
        CScript script = MakeP2AHScript(preimage);

        // The base script is exactly 25 bytes
        BOOST_CHECK_EQUAL(script.size(), 25);

        // Recognized as P2AH, not as P2PKH or P2SH
        BOOST_CHECK(script.IsPayToAssetAuthHash());
        BOOST_CHECK(script.IsAssetAuthScript());
        BOOST_CHECK(!script.IsPayToPublicKeyHash());
        BOOST_CHECK(!script.IsPayToScriptHash());

        // Solver classifies it as TX_ASSET_AUTH and extracts the hash
        txnouttype whichType;
        std::vector<std::vector<unsigned char> > vSolutions;
        BOOST_CHECK(Solver(script, whichType, vSolutions));
        BOOST_CHECK_EQUAL(whichType, TX_ASSET_AUTH);
        BOOST_CHECK(uint160(vSolutions[0]) == preimage.GetHash());

        // ExtractDestination returns a CAssetAuthID
        CTxDestination dest;
        BOOST_CHECK(ExtractDestination(script, dest));
        const CAssetAuthID* id = boost::get<CAssetAuthID>(&dest);
        BOOST_CHECK(id != nullptr);
        BOOST_CHECK(*id == CAssetAuthID(preimage.GetHash()));

        // GetScriptForDestination round-trips
        CScript script2 = GetScriptForDestination(dest);
        BOOST_CHECK(script == script2);

        // Address encode/decode round-trips
        std::string address = EncodeDestination(dest);
        CTxDestination decoded = DecodeDestination(address);
        BOOST_CHECK(IsValidDestination(decoded));
        const CAssetAuthID* decodedId = boost::get<CAssetAuthID>(&decoded);
        BOOST_CHECK(decodedId != nullptr && *decodedId == *id);

        // AssetAuthHashFromScript extracts the committed hash
        uint160 hash;
        BOOST_CHECK(AssetAuthHashFromScript(script, hash));
        BOOST_CHECK(hash == preimage.GetHash());

        // P2AH script with asset transfer data appended is still an asset auth script
        // and is also recognized as an asset script (so asset accounting works)
        CScript assetScript = MakeP2AHScript(preimage);
        CAssetTransfer transfer("SOMEASSET", 100 * COIN);
        transfer.ConstructTransaction(assetScript);
        BOOST_CHECK(assetScript.IsAssetAuthScript());
        BOOST_CHECK(!assetScript.IsPayToAssetAuthHash()); // not the bare form
        BOOST_CHECK(assetScript.IsAssetScript());
        BOOST_CHECK(assetScript.IsTransferAsset());

        // ExtractDestination on the asset-carrying P2AH script returns the P2AH address
        CTxDestination assetDest;
        BOOST_CHECK(ExtractDestination(assetScript, assetDest));
        const CAssetAuthID* assetId = boost::get<CAssetAuthID>(&assetDest);
        BOOST_CHECK(assetId != nullptr && *assetId == *id);
    }

    BOOST_AUTO_TEST_CASE(scriptsig_parsing_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH ScriptSig Parsing Test");

        CAssetAuthPreimage preimage = MakePreimage(2, {"ALPHA!", "BETA!", "GAMMA!"});
        CScript scriptSig = MakeP2AHScriptSig(preimage);

        // Round-trips through the scriptSig parser
        CAssetAuthPreimage parsed;
        BOOST_CHECK(AssetAuthPreimageFromScriptSig(scriptSig, parsed));
        BOOST_CHECK_EQUAL(parsed.nRequired, preimage.nRequired);
        BOOST_CHECK(parsed.vOwnerAssetNames == preimage.vOwnerAssetNames);

        // Empty scriptSig fails
        CAssetAuthPreimage dummy;
        BOOST_CHECK(!AssetAuthPreimageFromScriptSig(CScript(), dummy));

        // scriptSig with two pushes fails (must be exactly one)
        CScript twoPushes = MakeP2AHScriptSig(preimage);
        twoPushes << std::vector<unsigned char>{0x01};
        BOOST_CHECK(!AssetAuthPreimageFromScriptSig(twoPushes, dummy));

        // scriptSig with garbage data fails
        CScript garbage = CScript() << std::vector<unsigned char>{0xde, 0xad, 0xbe, 0xef};
        BOOST_CHECK(!AssetAuthPreimageFromScriptSig(garbage, dummy));
    }

    BOOST_AUTO_TEST_CASE(authorization_valid_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Authorization Valid Test");

        SelectParams(CBaseChainParams::MAIN);

        CAssetAuthPreimage preimage = MakePreimage(1, {"ALPHA!"});

        CCoinsView view;
        CCoinsViewCache coins(&view);

        // Coin 1: a P2AH output holding RVN
        COutPoint p2ahOutpoint = AddCoin(coins, MakeP2AHScript(preimage), 50 * COIN, 1);

        // Coin 2: the ALPHA! owner token at a key-protected address
        CScript ownerScript = MakeKeyScript();
        CAssetTransfer ownerTransfer("ALPHA!", OWNER_ASSET_AMOUNT);
        ownerTransfer.ConstructTransaction(ownerScript);
        COutPoint ownerOutpoint = AddCoin(coins, ownerScript, 0, 2);

        // Spending tx: P2AH input + owner token input; owner token moves to an output
        CMutableTransaction mutTx;

        CTxIn p2ahIn;
        p2ahIn.prevout = p2ahOutpoint;
        p2ahIn.scriptSig = MakeP2AHScriptSig(preimage);
        mutTx.vin.push_back(p2ahIn);

        CTxIn ownerIn;
        ownerIn.prevout = ownerOutpoint;
        mutTx.vin.push_back(ownerIn);

        // Outputs: RVN destination + owner token moves on
        mutTx.vout.push_back(CTxOut(50 * COIN, MakeKeyScript()));
        CScript ownerOutScript = MakeKeyScript();
        ownerTransfer.ConstructTransaction(ownerOutScript);
        mutTx.vout.push_back(CTxOut(0, ownerOutScript));

        CTransaction tx(mutTx);

        // The P2AH input is authorized: ALPHA! is present from a key-protected input
        std::string strError;
        BOOST_CHECK_MESSAGE(CheckTxAssetAuthInputs(tx, coins, strError), strError);
    }

    BOOST_AUTO_TEST_CASE(authorization_no_movement_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Authorization No Movement Test");

        SelectParams(CBaseChainParams::MAIN);

        CAssetAuthPreimage preimage = MakePreimage(1, {"ALPHA!"});

        CCoinsView view;
        CCoinsViewCache coins(&view);

        COutPoint p2ahOutpoint = AddCoin(coins, MakeP2AHScript(preimage), 50 * COIN, 1);

        // Spending tx: ONLY the P2AH input, no owner token anywhere
        CMutableTransaction mutTx;
        CTxIn p2ahIn;
        p2ahIn.prevout = p2ahOutpoint;
        p2ahIn.scriptSig = MakeP2AHScriptSig(preimage);
        mutTx.vin.push_back(p2ahIn);
        mutTx.vout.push_back(CTxOut(50 * COIN, MakeKeyScript()));

        CTransaction tx(mutTx);

        std::string strError;
        BOOST_CHECK(!CheckTxAssetAuthInputs(tx, coins, strError));
        BOOST_CHECK_EQUAL(strError, "bad-txns-assetauth-insufficient-owner-movement");
    }

    BOOST_AUTO_TEST_CASE(authorization_wrong_preimage_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Authorization Wrong Preimage Test");

        SelectParams(CBaseChainParams::MAIN);

        CAssetAuthPreimage preimage = MakePreimage(1, {"ALPHA!"});
        CAssetAuthPreimage wrongPreimage = MakePreimage(1, {"BETA!"});

        CCoinsView view;
        CCoinsViewCache coins(&view);

        COutPoint p2ahOutpoint = AddCoin(coins, MakeP2AHScript(preimage), 50 * COIN, 1);

        // Spend revealing the WRONG preimage
        CMutableTransaction mutTx;
        CTxIn p2ahIn;
        p2ahIn.prevout = p2ahOutpoint;
        p2ahIn.scriptSig = MakeP2AHScriptSig(wrongPreimage);
        mutTx.vin.push_back(p2ahIn);
        mutTx.vout.push_back(CTxOut(50 * COIN, MakeKeyScript()));

        CTransaction tx(mutTx);

        std::string strError;
        BOOST_CHECK(!CheckTxAssetAuthInputs(tx, coins, strError));
        BOOST_CHECK_EQUAL(strError, "bad-txns-assetauth-hash-mismatch");
    }

    BOOST_AUTO_TEST_CASE(authorization_m_of_n_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Authorization M of N Test");

        SelectParams(CBaseChainParams::MAIN);

        // 2-of-3 P2AH
        CAssetAuthPreimage preimage = MakePreimage(2, {"AAA!", "BBB!", "CCC!"});

        // Helper to build the spending tx with the given owner tokens moving
        auto buildAndCheck = [&](const std::vector<std::string>& vMovingTokens) -> bool {
            CCoinsView view;
            CCoinsViewCache coins(&view);

            COutPoint p2ahOutpoint = AddCoin(coins, MakeP2AHScript(preimage), 50 * COIN, 1);

            CMutableTransaction mutTx;

            CTxIn p2ahIn;
            p2ahIn.prevout = p2ahOutpoint;
            p2ahIn.scriptSig = MakeP2AHScriptSig(preimage);
            mutTx.vin.push_back(p2ahIn);

            // Add the owner token inputs and matching outputs
            uint32_t n = 10;
            for (const std::string& name : vMovingTokens) {
                CScript ownerScript = MakeKeyScript();
                CAssetTransfer ownerTransfer(name, OWNER_ASSET_AMOUNT);
                ownerTransfer.ConstructTransaction(ownerScript);
                COutPoint ownerOutpoint = AddCoin(coins, ownerScript, 0, n++);

                CTxIn ownerIn;
                ownerIn.prevout = ownerOutpoint;
                mutTx.vin.push_back(ownerIn);

                CScript ownerOutScript = MakeKeyScript();
                ownerTransfer.ConstructTransaction(ownerOutScript);
                mutTx.vout.push_back(CTxOut(0, ownerOutScript));
            }

            mutTx.vout.push_back(CTxOut(50 * COIN, MakeKeyScript()));

            CTransaction tx(mutTx);
            std::string strError;
            return CheckTxAssetAuthInputs(tx, coins, strError);
        };

        // 0 or 1 of 3 moving: not enough
        BOOST_CHECK(!buildAndCheck({}));
        BOOST_CHECK(!buildAndCheck({"AAA!"}));

        // 2 of 3 moving: authorized
        BOOST_CHECK(buildAndCheck({"AAA!", "BBB!"}));
        BOOST_CHECK(buildAndCheck({"AAA!", "CCC!"}));
        BOOST_CHECK(buildAndCheck({"BBB!", "CCC!"}));

        // All 3 moving: authorized
        BOOST_CHECK(buildAndCheck({"AAA!", "BBB!", "CCC!"}));

        // Wrong tokens moving: not authorized
        BOOST_CHECK(!buildAndCheck({"XXX!", "YYY!"}));
    }

    BOOST_AUTO_TEST_CASE(authorization_chaining_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Authorization Chaining Test");

        SelectParams(CBaseChainParams::MAIN);

        // ROOT! authorizes P2AH(ROOT!) which holds LEAF!
        // LEAF! authorizes P2AH(LEAF!) which holds RVN
        CAssetAuthPreimage rootPreimage = MakePreimage(1, {"ROOT!"});
        CAssetAuthPreimage leafPreimage = MakePreimage(1, {"LEAF!"});

        CCoinsView view;
        CCoinsViewCache coins(&view);

        // Coin 1: ROOT! at a key address
        CScript rootScript = MakeKeyScript();
        CAssetTransfer rootTransfer("ROOT!", OWNER_ASSET_AMOUNT);
        rootTransfer.ConstructTransaction(rootScript);
        COutPoint rootOutpoint = AddCoin(coins, rootScript, 0, 1);

        // Coin 2: LEAF! held at P2AH(ROOT!)
        CScript leafScript = MakeP2AHScript(rootPreimage);
        CAssetTransfer leafTransfer("LEAF!", OWNER_ASSET_AMOUNT);
        leafTransfer.ConstructTransaction(leafScript);
        COutPoint leafOutpoint = AddCoin(coins, leafScript, 0, 2);

        // Coin 3: RVN held at P2AH(LEAF!)
        COutPoint rvnOutpoint = AddCoin(coins, MakeP2AHScript(leafPreimage), 5 * COIN, 3);

        // The chained spending transaction
        auto buildTx = [&](bool fIncludeRoot) -> CTransaction {
            CMutableTransaction mutTx;

            if (fIncludeRoot) {
                CTxIn rootIn;
                rootIn.prevout = rootOutpoint;
                mutTx.vin.push_back(rootIn);
            }

            CTxIn leafIn;
            leafIn.prevout = leafOutpoint;
            leafIn.scriptSig = MakeP2AHScriptSig(rootPreimage);
            mutTx.vin.push_back(leafIn);

            CTxIn rvnIn;
            rvnIn.prevout = rvnOutpoint;
            rvnIn.scriptSig = MakeP2AHScriptSig(leafPreimage);
            mutTx.vin.push_back(rvnIn);

            // Outputs: both owner tokens move on, RVN to a destination
            if (fIncludeRoot) {
                CScript rootOutScript = MakeKeyScript();
                rootTransfer.ConstructTransaction(rootOutScript);
                mutTx.vout.push_back(CTxOut(0, rootOutScript));
            }

            // LEAF! returns to P2AH(ROOT!) so the chain can be spent again from the leaf P2AH
            CScript leafOutScript = MakeP2AHScript(rootPreimage);
            leafTransfer.ConstructTransaction(leafOutScript);
            mutTx.vout.push_back(CTxOut(0, leafOutScript));

            mutTx.vout.push_back(CTxOut(5 * COIN, MakeKeyScript()));

            return CTransaction(mutTx);
        };

        // With ROOT! in the tx: the whole chain is authorized
        std::string strError;
        BOOST_CHECK_MESSAGE(CheckTxAssetAuthInputs(buildTx(true), coins, strError), strError);

        // Without ROOT!: LEAF! can't move, so the RVN can't be spent either
        BOOST_CHECK(!CheckTxAssetAuthInputs(buildTx(false), coins, strError));
        BOOST_CHECK_EQUAL(strError, "bad-txns-assetauth-insufficient-owner-movement");
    }

    BOOST_AUTO_TEST_CASE(authorization_cycle_rejected_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH Authorization Cycle Rejected Test");

        SelectParams(CBaseChainParams::MAIN);

        // Cycle: AAA! held at P2AH(BBB!), BBB! held at P2AH(AAA!).
        // Neither has a key-protected root, so neither can ever be authorized
        CAssetAuthPreimage aPreimage = MakePreimage(1, {"AAA!"});
        CAssetAuthPreimage bPreimage = MakePreimage(1, {"BBB!"});

        CCoinsView view;
        CCoinsViewCache coins(&view);

        // AAA! held at P2AH(BBB!)
        CScript aScript = MakeP2AHScript(bPreimage);
        CAssetTransfer aTransfer("AAA!", OWNER_ASSET_AMOUNT);
        aTransfer.ConstructTransaction(aScript);
        COutPoint aOutpoint = AddCoin(coins, aScript, 0, 1);

        // BBB! held at P2AH(AAA!)
        CScript bScript = MakeP2AHScript(aPreimage);
        CAssetTransfer bTransfer("BBB!", OWNER_ASSET_AMOUNT);
        bTransfer.ConstructTransaction(bScript);
        COutPoint bOutpoint = AddCoin(coins, bScript, 0, 2);

        // Try to spend both in one tx (each token "moves")
        CMutableTransaction mutTx;

        CTxIn aIn;
        aIn.prevout = aOutpoint;
        aIn.scriptSig = MakeP2AHScriptSig(bPreimage);
        mutTx.vin.push_back(aIn);

        CTxIn bIn;
        bIn.prevout = bOutpoint;
        bIn.scriptSig = MakeP2AHScriptSig(aPreimage);
        mutTx.vin.push_back(bIn);

        CScript aOutScript = MakeKeyScript();
        aTransfer.ConstructTransaction(aOutScript);
        mutTx.vout.push_back(CTxOut(0, aOutScript));

        CScript bOutScript = MakeKeyScript();
        bTransfer.ConstructTransaction(bOutScript);
        mutTx.vout.push_back(CTxOut(0, bOutScript));

        CTransaction tx(mutTx);

        // The cycle has no key-protected root: rejected
        std::string strError;
        BOOST_CHECK(!CheckTxAssetAuthInputs(tx, coins, strError));
        BOOST_CHECK_EQUAL(strError, "bad-txns-assetauth-insufficient-owner-movement");
    }

    BOOST_AUTO_TEST_CASE(authorization_one_token_many_inputs_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH One Token Authorizes Many Inputs Test");

        SelectParams(CBaseChainParams::MAIN);

        CAssetAuthPreimage preimage = MakePreimage(1, {"ALPHA!"});

        CCoinsView view;
        CCoinsViewCache coins(&view);

        // Three P2AH UTXOs at the same address
        COutPoint p2ah1 = AddCoin(coins, MakeP2AHScript(preimage), 10 * COIN, 1);
        COutPoint p2ah2 = AddCoin(coins, MakeP2AHScript(preimage), 20 * COIN, 2);
        COutPoint p2ah3 = AddCoin(coins, MakeP2AHScript(preimage), 30 * COIN, 3);

        // The owner token
        CScript ownerScript = MakeKeyScript();
        CAssetTransfer ownerTransfer("ALPHA!", OWNER_ASSET_AMOUNT);
        ownerTransfer.ConstructTransaction(ownerScript);
        COutPoint ownerOutpoint = AddCoin(coins, ownerScript, 0, 4);

        // One tx spends all three P2AH UTXOs with a single ALPHA! movement
        CMutableTransaction mutTx;
        for (const COutPoint& outpoint : {p2ah1, p2ah2, p2ah3}) {
            CTxIn in;
            in.prevout = outpoint;
            in.scriptSig = MakeP2AHScriptSig(preimage);
            mutTx.vin.push_back(in);
        }

        CTxIn ownerIn;
        ownerIn.prevout = ownerOutpoint;
        mutTx.vin.push_back(ownerIn);

        mutTx.vout.push_back(CTxOut(60 * COIN, MakeKeyScript()));
        CScript ownerOutScript = MakeKeyScript();
        ownerTransfer.ConstructTransaction(ownerOutScript);
        mutTx.vout.push_back(CTxOut(0, ownerOutScript));

        CTransaction tx(mutTx);

        std::string strError;
        BOOST_CHECK_MESSAGE(CheckTxAssetAuthInputs(tx, coins, strError), strError);
    }

    // Helper: raw bytes replicating the wire serialization of a preimage,
    // with controllable compactsize encodings for framing-fuzz cases
    static std::vector<unsigned char> RawPreimageBytes(uint8_t nRequired, const std::vector<std::string>& vNames)
    {
        auto pushSize = [](std::vector<unsigned char>& out, uint64_t n) {
            if (n < 253) {
                out.push_back((unsigned char)n);
            } else if (n <= 0xffffU) {
                out.push_back(253);
                out.push_back((unsigned char)(n & 0xff));
                out.push_back((unsigned char)((n >> 8) & 0xff));
            } else {
                out.push_back(254);
                for (int k = 0; k < 4; k++)
                    out.push_back((unsigned char)((n >> (8 * k)) & 0xff));
            }
        };
        std::vector<unsigned char> out;
        out.push_back(nRequired);
        pushSize(out, vNames.size());
        for (const std::string& name : vNames) {
            pushSize(out, name.size());
            out.insert(out.end(), name.begin(), name.end());
        }
        return out;
    }

    BOOST_AUTO_TEST_CASE(preimage_framing_accepts_canonical_test)
    {
        std::string strError;
        std::vector<unsigned char> raw = RawPreimageBytes(1, {"AAA!", "BBB!"});
        BOOST_CHECK(AssetAuthPreimageFramingValid(raw, strError));

        // A canonical serialized preimage must also pass
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << MakePreimage(2, {"AAA!", "BBB!", "CCC!"});
        std::vector<unsigned char> rawCanonical(ss.begin(), ss.end());
        BOOST_CHECK(AssetAuthPreimageFramingValid(rawCanonical, strError));
    }

    BOOST_AUTO_TEST_CASE(preimage_framing_rejects_malformed_test)
    {
        std::string strError;

        // empty
        std::vector<unsigned char> empty;
        BOOST_CHECK(!AssetAuthPreimageFramingValid(empty, strError));

        // truncated inside a declared name
        std::vector<unsigned char> truncated = RawPreimageBytes(1, {"AAA!", "BBB!"});
        truncated.resize(truncated.size() - 3);
        BOOST_CHECK(!AssetAuthPreimageFramingValid(truncated, strError));

        // missing count entirely
        std::vector<unsigned char> noCount;
        noCount.push_back(1);
        BOOST_CHECK(!AssetAuthPreimageFramingValid(noCount, strError));

        // hostile count prefix: declares far more names than the payload can hold
        std::vector<unsigned char> hugeCount;
        hugeCount.push_back(1);
        hugeCount.push_back(254);
        hugeCount.insert(hugeCount.end(), {0xff, 0x00, 0x00, 0x00}); // 255 names, no data follows
        BOOST_CHECK(!AssetAuthPreimageFramingValid(hugeCount, strError));

        // hostile name length: declares more bytes than remain
        std::vector<unsigned char> hugeLen = RawPreimageBytes(1, {"AAA!"});
        // overwrite the name length byte with a large 254-prefixed value
        size_t lenPos = 2; // threshold + count(=1 byte)
        hugeLen[lenPos] = 254;
        const unsigned char bigLen[4] = {0x00, 0x00, 0x10, 0x00}; // 1MB > remaining
        hugeLen.insert(hugeLen.begin() + lenPos + 1, bigLen, bigLen + 4);
        BOOST_CHECK(!AssetAuthPreimageFramingValid(hugeLen, strError));
        BOOST_CHECK(strError.find("declared name length exceeds payload") != std::string::npos);

        // trailing data after an otherwise canonical payload
        std::vector<unsigned char> trailing = RawPreimageBytes(1, {"AAA!"});
        trailing.push_back(0x00);
        BOOST_CHECK(!AssetAuthPreimageFramingValid(trailing, strError));

        // oversize overall payload (> MAX_SCRIPT_ELEMENT_SIZE)
        std::vector<unsigned char> oversized(600, 'a');
        BOOST_CHECK(!AssetAuthPreimageFramingValid(oversized, strError));
    }

    BOOST_AUTO_TEST_CASE(preimage_decoder_rejects_malformed_scriptsig_test)
    {
        CAssetAuthPreimage preimage;

        // Canonical preimage in a single push decodes
        BOOST_CHECK(AssetAuthPreimageFromScriptSig(MakeP2AHScriptSig(MakePreimage(1, {"AAA!"})), preimage));
        BOOST_CHECK_EQUAL(preimage.nRequired, 1);
        BOOST_CHECK_EQUAL(preimage.vOwnerAssetNames.size(), 1u);

        // Truncated nested framing is rejected by the decoder before allocation
        std::vector<unsigned char> badRaw = RawPreimageBytes(1, {"AAAAA!"});
        badRaw.resize(badRaw.size() - 4);
        CScript badScriptSig;
        badScriptSig << badRaw;
        BOOST_CHECK(!AssetAuthPreimageFromScriptSig(badScriptSig, preimage));

        // Oversized single push (> MAX_SCRIPT_ELEMENT_SIZE) is rejected
        std::vector<unsigned char> bigPush(600, 'b');
        CScript bigScriptSig;
        bigScriptSig << bigPush;
        BOOST_CHECK(!AssetAuthPreimageFromScriptSig(bigScriptSig, preimage));

        // Two pushes are rejected (must be exactly one push)
        CScript twoPush;
        twoPush << std::vector<unsigned char>{0x01} << std::vector<unsigned char>{0x02};
        BOOST_CHECK(!AssetAuthPreimageFromScriptSig(twoPush, preimage));
    }

    BOOST_AUTO_TEST_CASE(checktxassets_activation_context_test)
    {
        // F-01 regression: a spend of a P2AH-shaped output is legacy-script
        // semantics before activation (must be ACCEPTED like an old node) and
        // authorization-enforced once the deployment is active for the
        // validation context.
        SelectParams(CBaseChainParams::MAIN);

        CAssetAuthPreimage pre = MakePreimage(1, {"UNIT!"});
        CScript p2ahScript;
        pre.ConstructTransaction(p2ahScript);

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << pre;
        std::vector<unsigned char> raw(ss.begin(), ss.end());

        CCoinsView baseView;
        CCoinsViewCache coins(&baseView);
        CTxOut txOut(1 * COIN, p2ahScript);
        COutPoint outpoint(uint256S("BB21CB9A63BE0019171456252989A459A7D0A5F494735278290079D22AB704A"), 0);
        coins.AddCoin(outpoint, Coin(txOut, 100, 0), true);

        CMutableTransaction mutTx;
        CTxIn in;
        in.prevout = outpoint;
        CScript sig;
        sig << raw;
        in.scriptSig = sig;
        mutTx.vin.push_back(in);
        mutTx.vout.emplace_back(CTxOut(1 * COIN - 5000,
            GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()))));

        CTransaction tx(mutTx);
        CValidationState state;
        std::vector<std::pair<std::string, uint256>> reissue;

        // Legacy (pre-activation) context: old nodes accept this hashlock spend.
        BOOST_CHECK_MESSAGE(Consensus::CheckTxAssets(tx, state, coins, nullptr, false, reissue, true,
                                nullptr, 0, nullptr, /*fAssetAuthDeployed=*/false),
                            "pre-activation legacy-semantics spend must be accepted (F-01)");

        // Deployed context with no owner movement: authorization must fail.
        CValidationState state2;
        BOOST_CHECK_MESSAGE(!Consensus::CheckTxAssets(tx, state2, coins, nullptr, false, reissue, true,
                                nullptr, 0, nullptr, /*fAssetAuthDeployed=*/true),
                            "deployed context must enforce owner movement");
    }

    BOOST_AUTO_TEST_CASE(preimage_strict_decoder_canonical_test)
    {
        auto sz = [](std::vector<unsigned char>& v, uint64_t n) {
            if (n < 253) { v.push_back((unsigned char)n); }
            else if (n <= 0xffff) { v.push_back(253); v.push_back(n & 0xff); v.push_back((n >> 8) & 0xff); }
            else { v.push_back(254); for (int k = 0; k < 4; k++) v.push_back((n >> (8 * k)) & 0xff); }
        };
        // Canonical payload accepted
        CAssetAuthPreimage canon = MakePreimage(1, {"AAAA!"});
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << canon;
        std::vector<unsigned char> rawCanon(ss.begin(), ss.end());
        CAssetAuthPreimage out;
        std::string err;
        BOOST_CHECK(AssetAuthPreimageStrictFromRaw(rawCanon, out, err));

        // Trailing byte: decodable but not canonical -> rejected
        std::vector<unsigned char> trailing(rawCanon);
        trailing.push_back(0x00);
        BOOST_CHECK(!AssetAuthPreimageStrictFromRaw(trailing, out, err));

        // Non-canonical CompactSize count (253-prefixed small value) -> rejected
        std::vector<unsigned char> nc;
        nc.push_back(1);
        nc.push_back(253); nc.push_back(0x01); nc.push_back(0x00);
        sz(nc, 5);
        const unsigned char* nm = (const unsigned char*)"AAAA!";
        nc.insert(nc.end(), nm, nm + 5);
        BOOST_CHECK(!AssetAuthPreimageStrictFromRaw(nc, out, err));
        BOOST_CHECK(err.find("canonical") != std::string::npos);
    }

    BOOST_AUTO_TEST_CASE(undo_input_bound_era_independent_test)
    {
        // R-01 regression: the parse-time undo input ceiling must stay at the
        // RIP2 bound regardless of process-local deployment state. A record
        // sized just above the old pre-RIP2 limit must deserialize cleanly.
        const size_t nOldLimit =
            (size_t)(MAX_BLOCK_WEIGHT / MIN_TRANSACTION_INPUT_WEIGHT);
        const size_t nCount = nOldLimit + 1;

        CTxUndo undo;
        undo.vprevout.resize(nCount); // default (spent) coins serialize tiny

        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << undo;

        CTxUndo decoded;
        try {
            ss << undo; // keep stream reusable
        } catch (...) {}
        CDataStream ss2(SER_DISK, CLIENT_VERSION);
        ss2 << undo;
        BOOST_CHECK_NO_THROW(ss2 >> decoded);
        BOOST_CHECK_EQUAL(decoded.vprevout.size(), nCount);
    }

    BOOST_AUTO_TEST_CASE(keystore_strict_preimage_contract_test)
    {
        // F-07 regression: the keystore enforces the same strict contract as
        // consensus, which also covers the wallet DB load path.
        CBasicKeyStore ks;
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << MakePreimage(1, {"AAAA!"});
        std::vector<unsigned char> canon(ss.begin(), ss.end());

        uint160 id = Hash160(canon);
        BOOST_CHECK(ks.AddAssetAuthPreimage(canon));
        std::vector<unsigned char> got;
        BOOST_CHECK(ks.GetAssetAuthPreimage(id, got));
        BOOST_CHECK(got == canon);

        // Trailing byte: decodable but non-canonical -> rejected at ingress
        std::vector<unsigned char> bad(canon);
        bad.push_back(0x00);
        BOOST_CHECK(!ks.AddAssetAuthPreimage(bad));
    }

    BOOST_AUTO_TEST_CASE(authorization_worklist_scaling_test)
    {
        // F-04: deep reverse-order dependency chains authorize via the
        // name->pending reverse index. Correctness at several depths plus a
        // loose wall-clock scaling sanity check (warn-only to avoid flaky CI).
        SelectParams(CBaseChainParams::MAIN);

        auto padName = [](size_t i) {
            std::string n = "WLIST";
            std::string num = std::to_string(i);
            while (num.size() < 6) num.insert(num.begin(), '0');
            return n + num + "!";
        };

        double tPrev = 0.0;
        for (size_t nDepth : {250u, 500u, 1000u}) {
            CCoinsView baseView;
            CCoinsViewCache coins(&baseView);

            auto mkName = [&](size_t i) { return padName(i); };

            // root seed coin (non-P2AH) holding R_0
            CScript rootScript = GetScriptForDestination(
                DecodeDestination(GetParams().GlobalBurnAddress()));
            {
                // The seed root supplies the FIRST requirement; each pending
                // then propagates the NEXT name via its held token.
                CAssetTransfer tr(mkName(1), 1);
                tr.ConstructTransaction(rootScript);
            }
            COutPoint rootOp(uint256S("1111111111111111111111111111111111111111111111111111111111111111"), 0);
            coins.AddCoin(rootOp, Coin(CTxOut(0, rootScript), 100, 0), true);

            std::vector<CAssetAuthPreimage> pres;
            pres.reserve(nDepth);
            for (size_t i = 1; i <= nDepth; i++) {
                pres.push_back(MakePreimage(1, {mkName(i)}));
                CScript sc;
                pres.back().ConstructTransaction(sc);
                CAssetTransfer tr(mkName(i + 1), 1); // token HELD at this output
                tr.ConstructTransaction(sc);
                COutPoint op(ArithToUint256(UintToArith256(
                    uint256S("2222222222222222222222222222222222222222222222222222222222222222")) +
                    arith_uint256(i)), 0);
                coins.AddCoin(op, Coin(CTxOut(0, sc), 100, 0), true);
            }

            CMutableTransaction mutTx;
            CTxIn rootIn; rootIn.prevout = rootOp;
            mutTx.vin.push_back(rootIn);
            for (size_t i = 1; i <= nDepth; i++) {
                CTxIn in;
                in.prevout = COutPoint(ArithToUint256(UintToArith256(
                    uint256S("2222222222222222222222222222222222222222222222222222222222222222")) +
                    arith_uint256(i)), 0);
                CScript sig;
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss << pres[i - 1];
                sig << std::vector<unsigned char>(ss.begin(), ss.end());
                in.scriptSig = sig;
                mutTx.vin.push_back(in);
            }
            CTransaction tx(mutTx);

            auto t0 = std::chrono::steady_clock::now();
            std::string err;
            std::vector<CAssetAuthInputInfo> vInfo;
            bool ok = CheckTxAssetAuthInputs(tx, coins, err, &vInfo);
            auto ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();

            BOOST_CHECK_MESSAGE(ok, "deep chain must authorize: " + err);
            BOOST_TEST_MESSAGE("worklist depth=" << nDepth << " elapsed=" << ms << "ms");
            if (tPrev > 0)
                BOOST_WARN_MESSAGE(ms < tPrev * 12.0,
                                   "scaling looks worse than linear-ish: " << tPrev << "ms -> " << ms << "ms");
            tPrev = std::max(ms, 0.05);
        }
    }

BOOST_AUTO_TEST_SUITE_END()
