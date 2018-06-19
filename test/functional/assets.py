#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Testing asset use cases

"""
from functools import reduce
from test_framework.mininode import COIN
from test_framework.test_framework import RavenTestFramework
from test_framework.util import (
    assert_equal,
    assert_is_hash_string,
)

class AssetTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def run_test(self):
        self.log.info("Running test!")

        n0, n1, n2 = self.nodes[0], self.nodes[1], self.nodes[2]

        self.log.info("Generating RVN for node[0]...")
        n0.generate(1)
        self.sync_all()
        n2.generate(100)
        self.sync_all()
        assert_equal(n0.getbalance(), 5000)

        self.log.info("Calling issue()...")
        address0 = n0.getnewaddress()
        ipfs_hash = "uPMS6aXCbZKpEjPHPUZN2NjB3YrhJTHsV4X3vb2t"
        n0.issue(asset_name="MY_ASSET", qty=1000, to_address=address0, \
                 units=4, reissuable=True, has_ipfs=True, ipfs_hash=ipfs_hash)

        self.log.info("Waiting for ten confirmations after issue...")
        n0.generate(10)
        self.sync_all()

        self.log.info("Checkout getassetdata()...")
        assetdata = n0.getassetdata("MY_ASSET")
        assert_equal(len(assetdata), 1)
        assert_equal(assetdata[0]["name"], "MY_ASSET")
        assert_equal(assetdata[0]["amount"], 1000 * COIN)
        assert_equal(assetdata[0]["units"], 4)
        assert_equal(assetdata[0]["reissuable"], 1)
        assert_equal(assetdata[0]["has_ipfs"], 1)
        assert_equal(assetdata[0]["ipfs_hash"], ipfs_hash)

        self.log.info("Checking getmyassets()...")
        myassets = n0.getmyassets()
        assert_equal(len(myassets), 1)
        assert_equal(len(myassets[0]), 2)
        asset_names = list(reduce((lambda a1, a2: a1 + a2), map(lambda a: a.keys(), myassets)))
        assert_equal(asset_names.count("MY_ASSET"), 1)
        assert_equal(asset_names.count("MY_ASSET!"), 1)
        assert_equal(len(myassets[0]["MY_ASSET"]), 1)
        assert_equal(len(myassets[0]["MY_ASSET!"]), 1)
        assert_is_hash_string(myassets[0]["MY_ASSET"][0]["txid"])
        assert_equal(myassets[0]["MY_ASSET"][0]["txid"], myassets[0]["MY_ASSET!"][0]["txid"])
        assert(int(myassets[0]["MY_ASSET"][0]["index"]) >= 0)
        assert(int(myassets[0]["MY_ASSET!"][0]["index"]) >= 0)

        self.log.info("Checking getaddressbalances()...")
        assert_equal(n0.getaddressbalances(address0)["MY_ASSET"], 1000 * COIN)
        assert_equal(n0.getaddressbalances(address0)["MY_ASSET!"], COIN)

        self.log.info("Calling transfer()...")
        address1 = n1.getnewaddress()
        n0.transfer("MY_ASSET", address1, 200.0000)

        self.log.info("Waiting for ten confirmations after transfer...")
        n0.generate(10)
        self.sync_all()

        self.log.info("Checking getmyassets()...")
        myassets = n1.getmyassets()
        assert_equal(len(myassets), 1)
        assert_equal(len(myassets[0]), 1)
        asset_names = list(reduce((lambda a1, a2: a1 + a2), map(lambda a: a.keys(), myassets)))
        assert_equal(asset_names.count("MY_ASSET"), 1)
        assert_equal(asset_names.count("MY_ASSET!"), 0)
        assert_equal(len(myassets[0]["MY_ASSET"]), 1)
        assert_is_hash_string(myassets[0]["MY_ASSET"][0]["txid"])
        assert(int(myassets[0]["MY_ASSET"][0]["index"]) >= 0)

        self.log.info("Checking getaddressbalances()...")
        assert_equal(n1.getaddressbalances(address1)["MY_ASSET"], 200 * COIN)
        found_change = False
        # TODO: Uses *considered harmful* getassetaddresses.  Not sure how to get change otherwise w/o work..
        for assaddr in n0.getassetaddresses("MY_ASSET").keys():
            if n0.validateaddress(assaddr)["ismine"] == True:
                found_change = True
                assert_equal(n0.getaddressbalances(assaddr)["MY_ASSET"], 800 * COIN)
        assert(found_change)
        assert_equal(n0.getaddressbalances(address0)["MY_ASSET!"], COIN)


if __name__ == '__main__':
    AssetTest().main()
