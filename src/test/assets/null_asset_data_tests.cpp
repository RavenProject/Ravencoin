// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <assets/assets.h>
#include <test/test_raven.h>
#include <boost/test/unit_test.hpp>
#include <amount.h>
#include <base58.h>
#include <chainparams.h>
#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <primitives/transaction.h>

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

    BOOST_AUTO_TEST_CASE(empty_per_address_asset_name_rejected)
    {
        CScript nullDataScript = GetScriptForNullAssetDataDestination(
            DecodeDestination(GetParams().GlobalBurnAddress()));
        CNullAssetTxData("", 0).ConstructTransaction(nullDataScript);
        BOOST_REQUIRE(nullDataScript.IsNullAssetTxDataScript());

        CMutableTransaction mutableTx;
        mutableTx.vin.resize(1);
        mutableTx.vin[0].prevout.n = 0;
        mutableTx.vout.emplace_back(0, nullDataScript);
        const CTransaction tx(mutableTx);
        CValidationState state;

        BOOST_CHECK(!CheckTransaction(tx, state));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "bad-txns-null-data-asset-name-empty");
    }


BOOST_AUTO_TEST_SUITE_END()
