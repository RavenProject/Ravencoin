// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <assets/assets.h>
#include <test/test_raven.h>
#include <boost/test/unit_test.hpp>
#include <amount.h>
#include <base58.h>
#include <chainparams.h>

BOOST_FIXTURE_TEST_SUITE(unique_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(unique_from_transaction_test)
    {
        BOOST_TEST_MESSAGE("Running Unique From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newUniqueScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNewAsset unique_asset("ROOT#UNIQUE1", 1 , 0 , 0, 0, "");
        unique_asset.ConstructTransaction(newUniqueScript);

        CTxOut out(0, newUniqueScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewAsset fetched_asset;
        BOOST_CHECK_MESSAGE(UniqueAssetFromTransaction(tx, fetched_asset,address), "Failed to get qualifier from transaction");
        BOOST_CHECK_MESSAGE(fetched_asset.strName == unique_asset.strName, "Unique Tests: Failed asset names check");
        BOOST_CHECK_MESSAGE(fetched_asset.nAmount== unique_asset.nAmount, "Unique Tests: Failed amount check");
        BOOST_CHECK_MESSAGE(address == GetParams().GlobalBurnAddress(), "Unique Tests: Failed address check");
        BOOST_CHECK_MESSAGE(fetched_asset.nReissuable == unique_asset.nReissuable, "Unique Tests: Failed reissuable check");
    }

    BOOST_AUTO_TEST_CASE(unique_from_transaction_fail_test)
    {
        BOOST_TEST_MESSAGE("Running Unique From Transaction Test");

        CMutableTransaction mutableTransaction;

        CScript newUniqueScript = GetScriptForDestination(DecodeDestination(GetParams().GlobalBurnAddress()));

        CNewAsset unique_asset("$NOT_UNIQUE", 1 , 0 , 0, 0, "");
        unique_asset.ConstructTransaction(newUniqueScript);

        CTxOut out(0, newUniqueScript);

        mutableTransaction.vout.push_back(out);

        CTransaction tx(mutableTransaction);

        std::string address;
        CNewAsset fetched_asset;
        BOOST_CHECK_MESSAGE(!UniqueAssetFromTransaction(tx, fetched_asset,address), "should have failed to get UniqueAssetFromTransaction");
    }


BOOST_AUTO_TEST_SUITE_END()
