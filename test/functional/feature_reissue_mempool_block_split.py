#!/usr/bin/env python3
# Copyright (c) 2026 The Raven Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Accept a block reissue while another reissue of that asset is in the mempool."""

from test_framework.test_framework import RavenTestFramework
from test_framework.util import assert_equal, disconnect_nodes


class ReissueMempoolBlockSplitTest(RavenTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 2

    def run_test(self):
        pending_node, mining_node = self.nodes
        pending_node.generate(216)
        self.sync_all()
        mining_node.generate(216)
        self.sync_all()
        assert_equal(pending_node.getblockchaininfo()['bip9_softforks']['assets']['status'], 'active')

        asset_name = 'BLOCKREISSUESPLIT'
        owner_address = pending_node.getnewaddress()
        mining_node.importprivkey(pending_node.dumpprivkey(owner_address), 'shared owner', False)
        pending_node.issue(asset_name, 100, owner_address)
        pending_node.generate(1)
        self.sync_all()
        assert_equal(pending_node.listmyassets(asset_name + '!')[asset_name + '!'], 1)
        assert_equal(mining_node.listmyassets(asset_name + '!')[asset_name + '!'], 1)
        shared_tip = pending_node.getbestblockhash()
        assert_equal(mining_node.getbestblockhash(), shared_tip)

        disconnect_nodes(pending_node, 1)
        disconnect_nodes(mining_node, 0)

        pending_txid = pending_node.reissue(asset_name, 2, pending_node.getnewaddress())[0]
        assert pending_txid in pending_node.getrawmempool()
        mined_txid = mining_node.reissue(asset_name, 3, mining_node.getnewaddress())[0]
        assert mined_txid != pending_txid
        block_hash = mining_node.generate(1)[0]
        assert mined_txid in mining_node.getblock(block_hash)['tx']
        assert_equal(mining_node.getassetdata(asset_name)['amount'], 103)

        assert_equal(pending_node.submitblock(mining_node.getblock(block_hash, 0)), None)
        assert_equal(pending_node.getbestblockhash(), block_hash)
        assert_equal(pending_node.getassetdata(asset_name)['amount'], 103)
        assert pending_txid not in pending_node.getrawmempool()


if __name__ == '__main__':
    ReissueMempoolBlockSplitTest().main()
