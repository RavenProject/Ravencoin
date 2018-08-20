#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Testing unique asset use cases

"""
import random

from test_framework.test_framework import RavenTestFramework
from test_framework.util import (
    assert_equal,
)


def get_random_unique_asset_name():
    tag_ab = "-ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@$%&*()[]{}<>_.;?\\:"
    size = random.randint(3, 14)
    name = ""
    for _ in range(1, size+1):
        ch = random.randint(65, 65+25)
        name += chr(ch)
    name += "#"
    tag_size = random.randint(1, 15)
    for _ in range(1, tag_size+1):
        tag_c = tag_ab[random.randint(0, len(tag_ab) - 1)]
        name += tag_c
    return name


class UniqueAssetTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3

    def activate_assets(self):
        self.log.info("Generating RVN for node[0] and activating assets...")
        n0 = self.nodes[0]
        n0.generate(432)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['assets']['status'])

    def issue_unique_asset(self):
        n0 = self.nodes[0]
        asset_name = get_random_unique_asset_name()
        print(asset_name)
        root_name = asset_name.split("#")[0]
        address0 = n0.getnewaddress()
        n0.issue(asset_name=root_name)
        n0.generate(1)
        tx_hash = n0.issue(asset_name=asset_name)
        print(tx_hash)
        n0.generate(1)
        return asset_name

    def test_issue_one(self):
        self.log.info("Issuing a unique asset...")
        n0 = self.nodes[0]
        asset_name = self.issue_unique_asset()
        print(n0.listmyassets())
        print(n0.listaddressesbyasset(asset_name.split("#")[0] + "*"))
        print(n0.listassets())
        assert_equal(1, n0.listmyassets()[asset_name])

    # TODO: try issuing where we don't own the parent or the parent doesn't exist
    # TODO: test decodescript
    # TODO: test raw transactions (both properly and improperly constructed)

    def run_test(self):
        self.activate_assets()
        self.test_issue_one()


if __name__ == '__main__':
    UniqueAssetTest().main()
