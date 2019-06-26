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


    BOOST_AUTO_TEST_CASE(message_encoding_check)
    {
        std::string hash1 = "0000002a7eea17df5164b3dd8f49bbc3dc268d92c39bf62e17b4e07326a11609";
        std::string hash2 = "6d539a227b256e0fce13c57d75a9135aa133533d10a0bde055e9322c6bac9435";

        std::string ipfs1 = "QmVUXZ1UiwGVuKMPuBagveHexGiRRTQLN8JDrBKauECSFQ";
        std::string ipfs2 = "QmX6972nFtqu1Y15qy1jyQm5mkDQx7JSoF2LEAqtnvGYyv";

        auto decoded1 = DecodeAssetData(hash1);
        auto decoded2 = DecodeAssetData(hash2);

        auto ipfsdecoded1 = DecodeAssetData(ipfs1);
        auto ipfsdecoded2 = DecodeAssetData(ipfs2);

        std::string error = "";
        BOOST_CHECK_MESSAGE(IsHex(hash1) && hash1.length() == 64, "Test 1 Failed"); // need to check the inside of CheckEncoded for regular hashes
        BOOST_CHECK_MESSAGE(IsHex(hash2) && hash2.length() == 64, "Test 2 Failed "); // need to check the inside of CheckEncoded for regular hashes
        BOOST_CHECK_MESSAGE(CheckEncoded(ipfsdecoded1, error), "Test 3 Failed - " +  error);
        BOOST_CHECK_MESSAGE(CheckEncoded(ipfsdecoded2, error), "Test 4 Failed - " +  error);

    }


BOOST_AUTO_TEST_SUITE_END()