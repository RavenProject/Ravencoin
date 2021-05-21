// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>

#include <test/test_raven.h>

#include <boost/test/unit_test.hpp>

#include <amount.h>
#include <script/standard.h>
#include <base58.h>
#include <consensus/validation.h>
#include <consensus/tx_verify.h>
#include <validation.h>

BOOST_FIXTURE_TEST_SUITE(asset_p2sh_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(asset_is_p2sh_test)
    {
        BOOST_TEST_MESSAGE("Running asset_is_p2sh_test");

        SelectParams(CBaseChainParams::MAIN);

        // Starting with mainnet addresses
        // RRxxGQw8PWuGCDDcbF5x5euwiqn15zJUgD
        // RQ5mGdscLeKpEbg1H4AgcW7Ke8M7R9CCP8
        // RJmpQfoDzgMv9p8utZwE5LC2diu8Eus3QP
        // createmultisig 2 "[\"RRxxGQw8PWuGCDDcbF5x5euwiqn15zJUgD\",\"RQ5mGdscLeKpEbg1H4AgcW7Ke8M7R9CCP8\",\"RJmpQfoDzgMv9p8utZwE5LC2diu8Eus3QP\"]"
        // address = r9THA4gUtPzmZ9WGcRoba4pDZ2oGLFtHKn
        // redeemScript = 522103ed288afb520cc40f885c8957638e13a2fd2c94ddd1f59ab7b9594de2b9dfcd9c21022d5fcf6afa30023b394526e11f4e45b43c4f36a429270170cc25d7bf12916eb32103ce679ae00fc1541cc4e6be5bd8fbe116fca24885284daacc3338fd36f87996c853ae

        // Create a transfer asset script to a P2SH address
        CAssetTransfer asset("P2SHTEST", 1000);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination("r9THA4gUtPzmZ9WGcRoba4pDZ2oGLFtHKn"));
        asset.ConstructTransaction(scriptPubKey);

        BOOST_CHECK_MESSAGE(scriptPubKey.IsP2SHAssetScript(), "IsP2SHAssetScript failed to find P2SH asset");
    }

    BOOST_AUTO_TEST_CASE(asset_is_not_p2sh_test)
    {
        BOOST_TEST_MESSAGE("Running asset_is_p2sh_test");

        SelectParams(CBaseChainParams::MAIN);

        // Starting with mainnet addresses
        // RRxxGQw8PWuGCDDcbF5x5euwiqn15zJUgD

        // Create a transfer asset script to a non P2SH address
        CAssetTransfer asset("NOTP2SHTEST", 1000);
        CScript scriptPubKey = GetScriptForDestination(DecodeDestination("RRxxGQw8PWuGCDDcbF5x5euwiqn15zJUgD"));
        asset.ConstructTransaction(scriptPubKey);

        BOOST_CHECK_MESSAGE(!scriptPubKey.IsP2SHAssetScript(), "IsP2SHAssetScript should of failed to file a P2SH asset");
    }

BOOST_AUTO_TEST_SUITE_END()