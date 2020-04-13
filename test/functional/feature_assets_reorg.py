#!/usr/bin/env python3
# Copyright (c) 2017 The Bitcoin Core developers
# Copyright (c) 2017-2020 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Testing asset reorg use cases"""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, disconnect_all_nodes, connect_all_nodes_bi

class AssetReorgTest(RavenTestFramework):
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
        assert_equal("active", n0.getblockchaininfo()['bip9_softforks']['messaging_restricted']['status'])


    def issue_reorg_test(self):
        self.log.info("Testing issue reorg 2...")
        n0, n1 = self.nodes[0], self.nodes[1]

        disconnect_all_nodes(self.nodes)

        asset_name = "DOUBLE_TROUBLE"

        # Issue asset on chain 1
        n0.issue(asset_name)
        n0.generate(15)

        # Issue asset on chain 2
        n1.generate(5)
        n1.issue(asset_name)
        n1.generate(5)

        # Do some validity checks, make sure we have separate chains
        node_0_hash = n0.getbestblockhash()
        node_0_height = n0.getblockcount()

        # They should each on their chains the asset DOUBLE_TROUBLE and DOUBLE_TROUBLE!
        assert_equal(True, n0.listmyassets() == n1.listmyassets())
        assert_equal(True, node_0_hash is not n1.getbestblockhash())

        # Uncomment to debug
        # self.log.info(f"{n0.getblockcount()}: {n0.getbestblockhash()}")
        # self.log.info(f"{n1.getblockcount()}: {n1.getbestblockhash()}")
        # self.log.info(n0.listmyassets())
        # self.log.info(n1.listmyassets())
        # Connect the nodes together, and force a reorg to occur
        connect_all_nodes_bi(self.nodes, True)

        # Uncomment to debug
        # self.log.info(f"{n0.getblockcount()}: {n0.getbestblockhash()}")
        # self.log.info(f"{n1.getblockcount()}: {n1.getbestblockhash()}")
        # self.log.info(n0.listmyassets())
        # self.log.info(n1.listmyassets())

        # Verify that node1 reorged to the node0 chain and that node0 has the asset and not node1
        assert_equal(node_0_height, n1.getblockcount())
        assert_equal(node_0_hash, n1.getbestblockhash())
        assert_equal(True, n0.listmyassets() != n1.listmyassets())

        # Disconnect all nodes
        disconnect_all_nodes(self.nodes)

        # Issue second asset on chain 1 and mine 5 blocks
        n0.issue(asset_name + '2')
        n0.generate(5)

        # Mine 5 blocks, issue second asset on chain 2, mine 5 more blocks, giving node1 more chain weight
        n1.generate(5)
        n1.issue(asset_name + '2')
        n1.generate(5)

        node_1_hash = n1.getbestblockhash()
        node_1_height = n1.getblockcount()
        assert_equal(True, n0.getbestblockhash() is not node_1_hash)
        assert_equal(True, n0.listmyassets('DOUBLE_TROUBLE2*') == n1.listmyassets('DOUBLE_TROUBLE2*'))

        # Connect the nodes together, and force a reorg to occur
        connect_all_nodes_bi(self.nodes, True)

        # Verify that node0 reorged to the node1 chain and that node1 has the asset and not node0
        assert_equal(n0.getblockcount(), node_1_height)
        assert_equal(n0.getbestblockhash(), node_1_hash)
        assert_equal(True, n0.listmyassets() != n1.listmyassets())

    def reorg_chain_state_test(self):
        self.log.info("Testing issue reorg to invalid chain...")
        n0, n1 = self.nodes[0], self.nodes[1]

        disconnect_all_nodes(self.nodes)

        # Mine 40 blocks
        n0.generate(40)
        node_0_hash = n0.getbestblockhash()
        node_0_height = n0.getblockcount()

        # Mine 44 blocks on chain 2
        n1.generate(20)
        node_1_hash_20 = n1.getbestblockhash()

        n1.generate(24)
        node_1_hash_44 = n1.getbestblockhash()
        node_1_height_44 = n1.getblockcount()

        # Do some validity checks, make sure we have separate chains
        assert_equal(True, node_0_hash is not n1.getbestblockhash())
        assert_equal(True, (node_1_height_44 - node_0_height) == 4)

        # Connect the nodes together, and force a reorg to occur
        connect_all_nodes_bi(self.nodes, True)

        node_0_hash_after_reorg = n0.getbestblockhash()
        node_0_height_after_reorg = n0.getblockcount()

        # Uncomment to debug
        # self.log.info('Node 0 after reorg ' + str(node_0_height_after_reorg) + ' ' + node_0_hash_after_reorg)
        # self.log.info('Node 1 after reorg ' + str(node_1_height_44) + ' ' + node_1_hash_44)

        # Make sure the reorg was successful
        assert_equal(True, node_0_hash_after_reorg == node_1_hash_44)
        assert_equal(True, node_0_height_after_reorg == node_1_height_44)

        # Invalidate node 1 block height at 20
        n0.invalidateblock(node_1_hash_20)

        # When node0 invalidated the block, it should automatically connect to the longest valid chain
        # which is the chain that node0 mined at the start of the test
        node_0_hash_invalidated = n0.getbestblockhash()
        node_0_height_invalidated = n0.getblockcount()
        assert_equal(True, node_0_height == node_0_height_invalidated)
        assert_equal(True, node_0_hash == node_0_hash_invalidated)

    def run_test(self):
        self.activate_assets()
        self.issue_reorg_test()
        self.reorg_chain_state_test()

if __name__ == '__main__':
    AssetReorgTest().main()