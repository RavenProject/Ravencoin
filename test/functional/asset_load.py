#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Testing *loaded* asset use cases

"""
import random

from test_framework.test_framework import RavenTestFramework
from test_framework.util import (
    assert_equal,
)


def get_random_asset_name():
    size = random.randint(2,28)
    name = "TT"
    for _ in range(1,size):
        ch = random.randint(65,65+25)
        name += chr(ch)
    return name


def get_random_asset_qty():
    return random.randint(1,21000000000)


def get_random_asset_units():
    return random.randint(0,8)


class AssetLoadTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def activate_assets(self):
        self.log.info("Generating RVN for node[0] and activating assets...")
        n0 = self.nodes[0]
        n0.generate(432)
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['assets']['status'])

    # starting with one rvn utxo, issue a bunch of assets
    def spam_issues_chained(self):
        self.log.info("Spamming issues!")

        n0 = self.nodes[0]
        asset_count = 400
        rvn_required = 400 * 500

        while n0.getbalance() < rvn_required:
            n0.generate(20)

        bank_balance = n0.getbalance()
        assert(bank_balance > rvn_required)

        # now combine them into one utxo (minus fee)
        bank_address = n0.getnewaddress()
        n0.sendtoaddress(bank_address, bank_balance - 1)

        # spend it all on sweet, sweet assets
        for i in range(1,asset_count):
            print(i)
            n0.issue(asset_name=get_random_asset_name(), qty=get_random_asset_qty(), to_address="", change_address="", \
                     units=get_random_asset_units())

        assert_equal(asset_count * 2, n0.listmyassets())

    def run_test(self):
        self.activate_assets()
        self.spam_issues_chained()

if __name__ == '__main__':
    AssetLoadTest().main()
