// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <assets/assets.h>
#include <assets/assettypes.h>
#include <test/test_raven.h>
#include <boost/test/unit_test.hpp>
#include <amount.h>
#include <base58.h>
#include <chainparams.h>
#include <script/standard.h>

BOOST_FIXTURE_TEST_SUITE(null_asset_data_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(null_data_from_script_test)
    {
        BOOST_TEST_MESSAGE("Running Null data from script");

        // Create the correct script
        CScript nullDataScript = GetScriptForNullAssetDataDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNullAssetTxData nullData("#ADDTAG", (int)QualifierType::ADD_QUALIFIER);
        nullData.ConstructTransaction(nullDataScript);

        CNullAssetTxData fetchedData;
        std::string fetchedAddress;

        BOOST_CHECK_MESSAGE(AssetNullDataFromScript(nullDataScript, fetchedData, fetchedAddress), "Null Data From Script Test 1: Failed to get NullDataFromScript");

    }

    BOOST_AUTO_TEST_CASE(null_data_from_script_fail_test)
    {
        BOOST_TEST_MESSAGE("Running Null data from script failure");

        // Create an invalid script
        CScript nullDataScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNullAssetTxData fetchedData;
        std::string fetchedAddress;
        BOOST_CHECK_MESSAGE(!AssetNullDataFromScript(nullDataScript, fetchedData, fetchedAddress), "Null Data Failure Test 1: should have failed");
    }

    BOOST_AUTO_TEST_CASE(global_null_data_from_script_test)
    {
        BOOST_TEST_MESSAGE("Running Global Null data from script");

        // Create the correct script
        CScript nullGlobalDataScript;

        CNullAssetTxData nullGlobalData("$ADDRESTRICTION", (int)RestrictedType::GLOBAL_FREEZE);
        nullGlobalData.ConstructGlobalRestrictionTransaction(nullGlobalDataScript);

        CNullAssetTxData fetchedData;
        BOOST_CHECK_MESSAGE(GlobalAssetNullDataFromScript(nullGlobalDataScript, fetchedData), "Null Global Data From Script Test 1: Failed to get NullGlobalDataFromScript");
    }

    BOOST_AUTO_TEST_CASE(global_null_data_from_script_fail_test)
    {
        BOOST_TEST_MESSAGE("Running Global Null data from script");

        // Create the correct script
        CScript nullGlobalDataScript;

        CNullAssetTxData nullGlobalData("$ADDRESTRICTION", (int)RestrictedType::GLOBAL_FREEZE);

        // Construct the wrong type of script
        nullGlobalData.ConstructTransaction(nullGlobalDataScript);

        CNullAssetTxData fetchedData;

        BOOST_CHECK_MESSAGE(!GlobalAssetNullDataFromScript(nullGlobalDataScript, fetchedData), "Null Global Data From Script Failure Test 1: should have failed");
    }

    BOOST_AUTO_TEST_CASE(p2ah_typed_null_data_roundtrip_test)
    {
        BOOST_TEST_MESSAGE("Running P2AH typed null-data roundtrip");

        CAssetAuthPreimage preimage(1, {"TAGTEST!"});
        CAssetAuthID p2ahId(preimage.GetHash());
        CTxDestination p2ahDest = p2ahId;

        CScript nullDataScript = GetScriptForNullAssetDataDestination(p2ahDest);

        CNullAssetTxData nullData("#TAGTEST", (int)QualifierType::ADD_QUALIFIER);
        nullData.ConstructTransaction(nullDataScript);

        BOOST_CHECK(NullAssetDataScriptUsesTypedDestination(nullDataScript));
        BOOST_CHECK_EQUAL(NullAssetTxDataPayloadOffset(nullDataScript), NULL_ASSET_DATA_PAYLOAD_OFFSET_TYPED);

        CNullAssetTxData fetchedData;
        std::string fetchedAddress;
        BOOST_CHECK(AssetNullDataFromScript(nullDataScript, fetchedData, fetchedAddress));
        BOOST_CHECK_EQUAL(fetchedAddress, EncodeDestination(p2ahDest));
        BOOST_CHECK_EQUAL(fetchedData.asset_name, "#TAGTEST");
        BOOST_CHECK_EQUAL(fetchedData.flag, (int)QualifierType::ADD_QUALIFIER);

        CTxDestination decoded;
        BOOST_CHECK(ExtractDestination(nullDataScript, decoded));
        const CAssetAuthID* decodedId = boost::get<CAssetAuthID>(&decoded);
        BOOST_REQUIRE(decodedId != nullptr);
        BOOST_CHECK(*decodedId == p2ahId);
    }

    BOOST_AUTO_TEST_CASE(legacy_p2pkh_null_data_still_p2pkh_test)
    {
        BOOST_TEST_MESSAGE("Running legacy P2PKH null-data identity");

        CTxDestination p2pkh = DecodeDestination(GetParams().GlobalBurnAddress());
        CScript nullDataScript = GetScriptForNullAssetDataDestination(p2pkh);

        CNullAssetTxData nullData("#LEGACY", (int)QualifierType::ADD_QUALIFIER);
        nullData.ConstructTransaction(nullDataScript);

        BOOST_CHECK(!NullAssetDataScriptUsesTypedDestination(nullDataScript));
        BOOST_CHECK_EQUAL(NullAssetTxDataPayloadOffset(nullDataScript), NULL_ASSET_DATA_PAYLOAD_OFFSET_LEGACY);

        std::string fetchedAddress;
        CNullAssetTxData fetchedData;
        BOOST_CHECK(AssetNullDataFromScript(nullDataScript, fetchedData, fetchedAddress));
        BOOST_CHECK_EQUAL(fetchedAddress, EncodeDestination(p2pkh));

        CTxDestination decoded;
        BOOST_CHECK(ExtractDestination(nullDataScript, decoded));
        BOOST_CHECK(decoded.type() == typeid(CKeyID));
    }


BOOST_AUTO_TEST_SUITE_END()
