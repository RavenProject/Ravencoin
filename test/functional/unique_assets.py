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


def gen_root_asset_name():
    size = random.randint(3, 14)
    name = ""
    for _ in range(1, size+1):
        ch = random.randint(65, 65+25)
        name += chr(ch)
    return name

def gen_unique_asset_name(root):
    tag_ab = "-ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@$%&*()[]{}<>_.;?\\:"
    name = root + "#"
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

    def issue_unique_asset(self, asset_name):
        n0 = self.nodes[0]
        tx_hash = n0.issue(asset_name=asset_name)
        n0.generate(1)
        return tx_hash

    def test_issue_one(self):
        self.log.info("Issuing a unique asset...")
        n0 = self.nodes[0]
        root = gen_root_asset_name()
        n0.issue(asset_name=root)
        n0.generate(1)
        asset_name = gen_unique_asset_name(root)
        self.issue_unique_asset(asset_name)
        assert_equal(1, n0.listmyassets()[asset_name])

    # TODO: try issuing where we don't own the parent or the parent doesn't exist
    # TODO: test decodescript
    # TODO: test raw transactions (both properly and improperly constructed)
    # TODO: test block invalidation
    # TODO: try issuing multiple assets at the same time (via raw)
    # TODO: work on issue_uniqu rpc call to issue multiples

    def run_test(self):
        self.activate_assets()
        self.test_issue_one()


if __name__ == '__main__':
    UniqueAssetTest().main()
