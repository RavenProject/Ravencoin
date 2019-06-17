// Copyright (c) 2017-2019 The Raven Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <assets/assets.h>

#include <test/test_raven.h>

#include <boost/test/unit_test.hpp>

#include <amount.h>
#include <base58.h>
#include <chainparams.h>
#include "consensus/consensus.h"


BOOST_FIXTURE_TEST_SUITE(messaging_tests, BasicTestingSetup)

    BOOST_AUTO_TEST_CASE(transfer_hashes_test)
    {
        std::string error = "";

        CAssetTransfer transfer1("ASSET", 1 * COIN, DecodeAssetData("QmRAQB6YaCyidP37UdDnjFY5vQuiBrcqdyoW1CuDgwxkD4"));
        BOOST_CHECK_MESSAGE(transfer1.IsValid(error), "Transfer Valid Test 1 - failed -" + error);

        // TODO Once Messages are active
//        CAssetTransfer transfer2("ASSET", 1 * COIN, "000000000000499bf4ebbe61541b02e4692b33defc7109d8f12d2825d4d2dfa0");
//        BOOST_CHECK_MESSAGE(transfer2.IsValid(error), "Transfer Valid Test 2 - failed -" + error);

        // Asset transfer is Zero failure
        CAssetTransfer transfer3("ASSET", 0);
        BOOST_CHECK_MESSAGE(!transfer3.IsValid(error), "Transfer Valid Test 3 did not fail");

        // empty message with an expiration date failure
        std::string message = "";
        int64_t date = 15555555;
        CAssetTransfer transfer4("ASSET", 1 * COIN, message, date);
        transfer4.nExpireTime = date;
        BOOST_CHECK_MESSAGE(!transfer4.IsValid(error), "Transfer Valid Test 4 did not fail");

        // negative expiration date failure
        int64_t date2 = -1;
        CAssetTransfer transfer5("ASSET", 1 * COIN, message, date2);
        transfer5.nExpireTime = date2;
        BOOST_CHECK_MESSAGE(!transfer5.IsValid(error), "Transfer Valid Test 5 did not fail");

        // TODO Once Messages are active
        // contains an l which isn't base 58
//        CAssetTransfer transfer6("ASSET", 1 * COIN, "l00000000000499bf4ebbe61541b02e4692b33defc7109d8f12d2825d4d2dfa0");
//        BOOST_CHECK_MESSAGE(!transfer6.IsValid(error), "Transfer Valid Test 6 did not fail");


        // TODO, once messaging goes active on mainnet, we can move check from ContextualCheckTransfer to CheckTransfer
        
    }


BOOST_AUTO_TEST_SUITE_END()