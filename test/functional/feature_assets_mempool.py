#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2018 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Testing asset mempool use cases

"""
from test_framework.test_framework import RavenTestFramework
from test_framework.util import *


import string

class AssetMempoolTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2


    def activate_assets(self):
        self.log.info("Generating RVN and activating assets...")
        n0, n1 = self.nodes[0], self.nodes[1]

        n0.generate(1)
        self.sync_all()
        n0.generate(216)
        self.sync_all()
        n1.generate(216)
        self.sync_all()
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['assets']['status'])


    def issue_mempool_test(self):
        self.log.info("Testing issue mempool...")

        n0, n1 = self.nodes[0], self.nodes[1]

        disconnect_all_nodes(self.nodes)

        asset_name = "MEMPOOL"

        # Issue asset on chain 1 and mine it into the blocks
        n0.issue(asset_name)
        n0.generate(15)

        # Issue asset on chain 2 but keep it in the mempool. No mining
        txid = n1.issue(asset_name)
        print(txid)

        connect_all_nodes_bi(self.nodes)

        assert_equal(n0.getblockcount(), n1.getblockcount())
        assert_equal(n0.getbestblockhash(), n1.getbestblockhash())

    def run_test(self):
        self.activate_assets()
        self.issue_mempool_test()

if __name__ == '__main__':
    AssetMempoolTest().main()