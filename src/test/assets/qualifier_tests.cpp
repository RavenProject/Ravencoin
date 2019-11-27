// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <assets/assets.h>
#include <test/test_raven.h>
#include <boost/test/unit_test.hpp>
#include <amount.h>
#include <base58.h>
#include <chainparams.h>

BOOST_FIXTURE_TEST_SUITE(qualifier_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(qualifier_from_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Qualifier From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newQualifierScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNewAsset qualifier_asset("#QUALIFIER_NAME", 5 * COIN);
        qualifier_asset.ConstructTransaction(newQualifierScript);

        CTxOut out(0, newQualifierScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewAsset fetched_asset;
        BOOST_CHECK_MESSAGE(QualifierAssetFromTransaction(tx, fetched_asset,address), "Failed to get qualifier from transaction");
        BOOST_CHECK_MESSAGE(fetched_asset.strName == qualifier_asset.strName, "Qualifier Tests: Failed asset names check");
        BOOST_CHECK_MESSAGE(fetched_asset.nAmount== qualifier_asset.nAmount, "Qualifier Tests: Failed amount check");
        BOOST_CHECK_MESSAGE(address == GetParams().GlobalBurnAddress(), "Qualifier Tests: Failed address check");
    }

    BOOST_AUTO_TEST_CASE(qualifier_from_transaction__fail_test)
    {
        BOOST_TEST_MESSAGE("Running Qualifier From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newQualifierScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNewAsset qualifier_asset("NOT_QUALIFIER_NAME", 5 * COIN);
        qualifier_asset.ConstructTransaction(newQualifierScript);

        CTxOut out(0, newQualifierScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewAsset fetched_asset;
        BOOST_CHECK_MESSAGE(!QualifierAssetFromTransaction(tx, fetched_asset,address), "should have failed to get QualifierAssetFromTransaction");
    }


    BOOST_AUTO_TEST_CASE(verify_new_qualifier_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Verify New Qualifier From Transaction Test");

        // Create transaction and add burn to it
        CMutableTransaction mutableTransaction;
        CScript burnScript = GetScriptForDestination(DecodeDestination(GetBurnAddress(AssetType::QUALIFIER)));
        CTxOut burnOut(GetBurnAmount(AssetType::QUALIFIER), burnScript);
        mutableTransaction.vout.push_back(burnOut);

        // Create the new Qualifier Script
        CScript newQualifierScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        CNewAsset qualifier_asset("#QUALIFIER_NAME", 5 * COIN, 0, 0, 0, "");
        qualifier_asset.ConstructTransaction(newQualifierScript);
        CTxOut assetOut(0, newQualifierScript);
        mutableTransaction.vout.push_back(assetOut);

        CTransaction tx(mutableTransaction);

        std::string error;
        BOOST_CHECK_MESSAGE(tx.VerifyNewQualfierAsset(error), "Failed to Verify New Qualifier Asset" + error);
    }

    BOOST_AUTO_TEST_CASE(verify_new_sub_qualifier_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Verify New Sub Qualifier From Transaction Test");

        // Create transaction and add burn to it
        CMutableTransaction mutableTransaction;
        CScript burnScript = GetScriptForDestination(DecodeDestination(GetBurnAddress(AssetType::SUB_QUALIFIER)));
        CTxOut burnOut(GetBurnAmount(AssetType::SUB_QUALIFIER), burnScript);
        mutableTransaction.vout.push_back(burnOut);

        // Add the parent transaction for sub qualifier tx
        CAssetTransfer parentTransfer("#QUALIFIER_NAME", OWNER_ASSET_AMOUNT);
        CScript parentScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        parentTransfer.ConstructTransaction(parentScript);
        CTxOut parentOut(0, parentScript);
        mutableTransaction.vout.push_back(parentOut);

        // Create the new Qualifier Script
        CScript newQualifierScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));
        CNewAsset qualifier_asset("#QUALIFIER_NAME/#SUB1", 5 * COIN, 0, 0, 0, "");
        qualifier_asset.ConstructTransaction(newQualifierScript);
        CTxOut assetOut(0, newQualifierScript);
        mutableTransaction.vout.push_back(assetOut);

        CTransaction tx(mutableTransaction);

        std::string strError = "";

        tx.VerifyNewQualfierAsset(strError);
        BOOST_CHECK_MESSAGE(tx.VerifyNewQualfierAsset(strError), "Failed to Verify New Sub Qualifier Asset " + strError);
    }




BOOST_AUTO_TEST_SUITE_END()
